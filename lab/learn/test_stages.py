#!/usr/bin/env python3
"""The parallel iteration (stages.py): shards merge into one data file per
iteration, and the decision follows loop.py's rules on the summed results.

    python3 lab/learn/test_stages.py
"""
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import loop  # noqa: E402
import stages  # noqa: E402

ARGS = ["--games", "200", "--sims", "50", "--policy", "--accept", "0.55", "--gate-min", "80", "--gate-max", "240",
        "--gate-block", "40", "--panel-games", "16", "--panel-floor", "0.45"]


def plan(current="lab/learn/nets/net_a.rbpn"):
    a = loop.make_parser().parse_args(ARGS)
    return {"it": 7, "current": current, "reference": "lab/learn/nets/net_r.rbpn", "anchors": [], "args": ARGS,
            "tasks": stages.tasks_for(a), "started_utc": "x", "t0": 0}


def results(gate_wins_per_block, panel_wins_per_matchup=8):
    out = [{"task": f"gate:{b}", "kind": "gate", "wins": w, "losses": 40 - w, "games": 40}
           for b, w in enumerate(gate_wins_per_block)]
    out += [{"task": f"panel:{m}", "kind": "panel", "name": name, "wins": panel_wins_per_matchup,
             "losses": 16 - panel_wins_per_matchup, "games": 16} for m, (name, _, _) in enumerate(loop.PANEL)]
    out.append({"task": "yard:random", "kind": "yard", "vs_random": {"games": 60, "wins": 58, "rate": 0.967}})
    out += [{"task": "yard:reference", "kind": "yard"}, {"task": "yard:anchors", "kind": "yard"}]
    return out


CAND = {"cand": "lab/learn/nets/net_007.rbpn", "row": {"iteration": 7}}


class Decide(unittest.TestCase):
    def run_decide(self, res, p=None):
        ladder = {"iterations": [], "matches": []}
        return stages.decide(p or plan(), CAND, res, ladder), ladder

    def test_the_tasks_cover_six_gate_blocks_five_matchups_and_three_yardsticks(self):
        self.assertEqual(plan()["tasks"], [f"gate:{b}" for b in range(6)] + [f"panel:{m}" for m in range(5)]
                         + ["yard:random", "yard:reference", "yard:anchors"])

    def test_a_clear_win_is_adopted(self):
        row, ladder = self.run_decide(results([26] * 6))           # 156/240 = 65%
        self.assertEqual(row["vs_current"]["sprt"], "better")
        self.assertTrue(row["accepted"])
        self.assertEqual(ladder["current_net"], CAND["cand"])

    def test_an_undecided_gate_needs_the_accept_rate(self):
        row, _ = self.run_decide(results([22] * 6))                # 132/240 = 55.0%
        self.assertEqual(row["vs_current"]["sprt"], "rate")
        self.assertTrue(row["accepted"])
        row, ladder = self.run_decide(results([21] * 6))           # 126/240 = 52.5%
        self.assertFalse(row["accepted"])
        self.assertNotIn("current_net", ladder)

    def test_the_panel_floor_refuses_a_mirror_winner(self):
        row, _ = self.run_decide(results([26] * 6, panel_wins_per_matchup=6))   # 37.5% on the panel
        self.assertEqual(row["vs_current"]["sprt"], "better")
        self.assertFalse(row["panel"]["ok"])
        self.assertFalse(row["accepted"])

    def test_a_missing_task_stops_the_decision(self):
        with self.assertRaises(SystemExit):
            self.run_decide(results([26] * 6)[1:])

    def test_the_gate_blocks_are_recorded_for_elo(self):
        res = results([26] * 6)
        for r in res:
            if r["kind"] == "gate":
                r["matches"] = [{"iteration": 7, "label": "gate", "a": "net:c", "b": "net:a", "games": 40,
                                 "wins_a": r["wins"], "wins_b": r["losses"]}]
        _, ladder = self.run_decide(res)
        self.assertEqual(sum(m["games"] for m in ladder["matches"] if m["label"] == "gate"), 240)
        self.assertEqual(sum(1 for m in ladder["matches"] if m["label"] == "panel"), 1)


class Merge(unittest.TestCase):
    def test_shards_merge_into_one_file_with_one_header(self):
        with tempfile.TemporaryDirectory() as d:
            old = loop.DATA
            loop.DATA = Path(d)
            try:
                head = b"RBV3" + struct.pack("<ii", 6688, 1234)
                for k, body in enumerate([b"aaaa", b"bb", b"cccccc"]):
                    (Path(d) / f"iter_007_s{k}.rbvd").write_bytes(head + body)
                out = stages.merge_shards(7)
                self.assertEqual(out.read_bytes(), head + b"aaaabbcccccc")
                self.assertEqual(sorted(os.listdir(d)), ["iter_007.rbvd"])
            finally:
                loop.DATA = old

    def test_rbv4_shards_merge_too(self):
        with tempfile.TemporaryDirectory() as d:
            old = loop.DATA
            loop.DATA = Path(d)
            try:
                head = b"RBV4" + struct.pack("<ii", 6688, 1234)
                for k, body in enumerate([b"xx", b"yyy"]):
                    (Path(d) / f"iter_007_s{k}.rbvd").write_bytes(head + body)
                self.assertEqual(stages.merge_shards(7).read_bytes(), head + b"xxyyy")
            finally:
                loop.DATA = old

    def test_a_shard_with_another_header_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            old = loop.DATA
            loop.DATA = Path(d)
            try:
                (Path(d) / "iter_007_s0.rbvd").write_bytes(b"RBV3" + struct.pack("<ii", 6688, 1234))
                (Path(d) / "iter_007_s1.rbvd").write_bytes(b"RBV3" + struct.pack("<ii", 9999, 1234))
                with self.assertRaises(SystemExit):
                    stages.merge_shards(7)
            finally:
                loop.DATA = old



class Specialist(unittest.TestCase):
    def setUp(self):
        self.old = (loop.LADDER, loop.NET_PREFIX, loop.TRACK)

    def tearDown(self):
        loop.LADDER, loop.NET_PREFIX, loop.TRACK = self.old

    def test_a_specialist_has_its_own_ladder_net_names_and_tasks(self):
        a = loop.make_parser().parse_args(ARGS + ["--specialist", "kennen"])
        plan = {"args": ARGS + ["--specialist", "kennen"]}
        stages.settings(plan)
        self.assertEqual(loop.NET_PREFIX, "spec_kennen_")
        self.assertEqual(loop.LADDER.parent.name, "kennen")
        tasks = stages.tasks_for(a)
        self.assertEqual(sum(t.startswith("panel:") for t in tasks), len(loop.SPECIALISTS["kennen"]["jury"]))
        self.assertNotIn("yard:anchors", tasks)
        self.assertEqual(stages.mirror_decks(a), (loop.SPECIALISTS["kennen"]["mirror"],) * 2)
        stages.settings({"args": ARGS})
        self.assertEqual(loop.NET_PREFIX, "net_")
        self.assertEqual(loop.LADDER, loop.MAIN_LADDER)

    def test_a_jury_deck_is_piloted_by_its_legends_specialist_when_there_is_one(self):
        plan = {"general": "g.rbpn", "pilots": {"kennen": "k.rbpn"}}
        kennen = loop.SPECIALISTS["kennen"]["decks"][0]
        self.assertEqual(stages.pilot(plan, kennen), "k.rbpn")
        self.assertEqual(stages.pilot(plan, loop.SPECIALISTS["kennen"]["jury"][2]), "g.rbpn")   # Kai'Sa

    def test_every_specialist_deck_exists(self):
        for s in loop.SPECIALISTS.values():
            for d in s["decks"] + s["jury"]:
                self.assertTrue((loop.ROOT / d).exists(), d)


class Evolution(unittest.TestCase):
    def test_the_first_iteration_tries_the_seed_settings_never_narrower_than_the_current_net(self):
        v = stages.spec_variants({"iterations": []}, 0, 1, 64, 64)
        self.assertEqual(len(v), 4)
        self.assertEqual([x["hidden"] for x in v], [64, 128, 128, 64])
        self.assertTrue(all(x["hidden"] == 128 for x in stages.spec_variants({"iterations": []}, 0, 1, 128, 64)))

    def test_later_iterations_keep_the_screen_winner_and_mutate_it(self):
        win = {"epochs": 3, "lr": 3e-4, "hidden": 128, "q_weight": 0.5}
        ladder = {"iterations": [{"screen": {"2": {"overrides": win}}, "screen_pick": 2}]}
        v = stages.spec_variants(ladder, 1, 1, 128, 64)
        self.assertEqual(v[0], win)
        self.assertEqual(len({json.dumps(x, sort_keys=True) for x in v}), 4)
        for x in v[1:]:
            self.assertGreaterEqual(x["hidden"], 128)
            self.assertTrue(5e-5 <= x["lr"] <= 2e-3 and 1 <= x["epochs"] <= 8)
        self.assertEqual(v, stages.spec_variants(ladder, 1, 1, 128, 64), "every shard plans the same")


class League(unittest.TestCase):
    def test_the_schedule_is_shared_by_every_shard_and_alternates_seats(self):
        lg = ["lab/learn/nets/net_128.rbpn", loop.MATERIAL]
        s1 = loop.league_schedule(1, 184, 400, 0.3, lg)
        self.assertEqual(s1, loop.league_schedule(1, 184, 400, 0.3, lg))
        picked = [x for x in s1 if x]
        self.assertTrue(90 <= len(picked) <= 150, len(picked))
        self.assertEqual([seat for _, seat in picked[:4]], [1, 2, 1, 2])
        self.assertEqual({o for o, _ in picked}, set(lg))
        self.assertEqual(loop.league_schedule(1, 184, 50, 0.0, lg), [None] * 50)
        self.assertEqual(loop.league_schedule(1, 184, 50, 0.3, []), [None] * 50)

    def test_the_league_holds_older_nets_and_the_material_bot_never_the_current(self):
        ladder = {"iterations": [{"net": "lab/learn/nets/net_164_v3.rbpn", "accepted": True},
                                 {"net": "lab/learn/nets/net_128.rbpn", "accepted": True}],
                  "anchor_nets": []}
        out = loop.league_opponents(ladder, "lab/learn/nets/net_164_v3.rbpn", 4)
        self.assertNotIn("lab/learn/nets/net_164_v3.rbpn", out)
        self.assertEqual(out[-1], loop.MATERIAL)

    def test_the_material_bot_has_no_net(self):
        a = loop.make_parser().parse_args(ARGS + ["--settle", "60"])
        spec = loop.Loop(a).material_bot()
        self.assertIn("eval=material", spec)
        self.assertNotIn(".rbpn", spec)


class Population(unittest.TestCase):
    def cands(self):
        return {k: {"cand": f"lab/learn/nets/net_007_v{k}.rbpn", "row": {"iteration": 7, "variant": {"index": k}}}
                for k in range(len(stages.VARIANTS))}

    def test_pick_takes_the_best_screen_rate_and_records_every_variant(self):
        rates = [9, 12, 10, 11]            # wins per 20-game block
        screens = [{"variant": k, "block": b, "wins": w, "losses": 20 - w, "games": 20}
                   for k, w in enumerate(rates) for b in range(stages.SCREEN_BLOCKS)]
        out = stages.pick(plan(), self.cands(), screens)
        self.assertEqual(out["cand"], "lab/learn/nets/net_007_v1.rbpn")
        self.assertEqual(out["row"]["screen_pick"], 1)
        self.assertEqual(set(out["row"]["screen"]), {str(k) for k in range(len(stages.VARIANTS))})
        self.assertAlmostEqual(out["row"]["screen"]["1"]["rate"], 0.6)

    def test_pick_refuses_a_variant_without_screen(self):
        screens = [{"variant": 0, "block": 0, "wins": 10, "losses": 10, "games": 20}]
        with self.assertRaises(SystemExit):
            stages.pick(plan(), self.cands(), screens)

    def test_screen_seeds_never_meet_the_gate_or_each_other(self):
        starts = [loop.seed_base(7, "screen", k * stages.SCREEN_BLOCKS + b)
                  for k in range(len(stages.VARIANTS)) for b in range(stages.SCREEN_BLOCKS)]
        starts += [loop.seed_base(7, "gate", b) for b in range(12)] + [loop.seed_base(7, "selfplay", k) for k in range(400)]
        spans = sorted((s, s + loop.SEED_BLOCK) for s in starts)
        for (s1, e1), (s2, e2) in zip(spans, spans[1:]):
            self.assertLessEqual(e1, s2)


if __name__ == "__main__":
    unittest.main()
