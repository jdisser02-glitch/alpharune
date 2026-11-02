#!/usr/bin/env python3
"""The learning loop's matches never replay the same deals: every batch owns a
range of game seeds no other batch touches, except the two seat orders of one
block, which share a range on purpose (same deals, agents swapped).

    python3 lab/learn/test_loop_seeds.py
"""
import itertools
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import loop  # noqa: E402


def args(**kw):
    a = dict(threads=1, sims=50, settle=60, agent="pmcts", worlds=8, gate_min=80, gate_max=240, gate_block=40,
             accept=0.5, yardstick_games=60, panel_games=16, panel_floor=0.45, anchor_games=20)
    a.update(kw)
    return SimpleNamespace(**a)


class FakeEngine:
    """Stands in for run_batch: 11 wins in 20 for the candidate, where the
    SPRT (50% vs 60%) stays undecided, so the gate plays every block up to
    --gate-max."""
    def __init__(self):
        self.calls = 0

    def __call__(self, deck1, deck2, a1, a2, games, threads, seed, value_data=None):
        if max(2, games) >= loop.SEED_BLOCK:
            raise ValueError("too many games for one block")
        self.calls += 1
        g = max(2, games)
        w = round(0.55 * g)
        return (w, g - w) if "cand" in a1 else (g - w, w)


def assert_disjoint(test, batches):
    by_seed = {}
    for seed, games, d1, d2, a1, a2 in batches:
        by_seed.setdefault(seed, []).append((games, d1, d2, a1, a2))
    ranges = []
    for seed, group in by_seed.items():
        # one block = at most its two seat orders: same decks and games, agents swapped
        test.assertLessEqual(len(group), 2, f"seed {seed} reused by {len(group)} batches")
        if len(group) == 2:
            (g1, d1, d2, x1, y1), (g2, e1, e2, x2, y2) = group
            test.assertEqual((g1, d1, d2), (g2, e1, e2), f"seed {seed} shared by two different matchups")
            test.assertEqual((x1, y1), (y2, x2), f"seed {seed} shared by batches that are not one seat swap")
        ranges.append((seed, seed + max(g for g, *_ in group)))
    ranges.sort()
    for (s1, e1), (s2, e2) in zip(ranges, ranges[1:]):
        test.assertLessEqual(e1, s2, f"seed ranges overlap: [{s1}, {e1}) and [{s2}, {e2})")


class SeedPlan(unittest.TestCase):
    def setUp(self):
        self._orig = loop.run_batch
        loop.run_batch = FakeEngine()

    def tearDown(self):
        loop.run_batch = self._orig

    def test_slots_and_blocks_never_overlap(self):
        spans = []
        for it in (0, 1, 2, 999):
            for label, start in loop.SEED_SLOT.items():
                for block in (0, 1, 5):
                    b = loop.seed_base(it, label, block)
                    spans.append((b, b + loop.SEED_BLOCK))
        spans = sorted(set(spans))
        for (s1, e1), (s2, e2) in zip(spans, spans[1:]):
            self.assertLessEqual(e1, s2)

    def test_block_past_its_slot_is_refused(self):
        with self.assertRaises(ValueError):
            loop.seed_base(0, "gate", 10_000)
        with self.assertRaises(ValueError):
            loop.seed_base(3000, "gate", 0)    # past 31-bit seeds

    def test_one_iteration_of_matches_uses_distinct_deals(self):
        L = loop.Loop(args())
        ladder = {"iterations": []}
        for it in (7, 8):
            for k, (d1, d2) in enumerate(L.self_play_pairs(__import__("random").Random(it), ["a.txt", "b.txt"], 50)):
                L.batch(d1, d2, "cur", "cur", 4, loop.seed_base(it, "selfplay", k))
            row = L.gate(ladder, it, "nets/cand.rbpn", "nets/cur.rbpn")
            self.assertEqual(row["games"], 240, "the fake engine should run the gate to its maximum")
            L.panel(ladder, it, L.bot("nets/cand.rbpn"), L.bot("nets/cur.rbpn"))
            L.match(ladder, it, "cand", "random", 60, loop.seed_base(it, "vs_random"), "vs_random")
            L.match(ladder, it, "cand", "ref", 60, loop.seed_base(it, "vs_reference"), "vs_reference")
            for j in range(2):
                L.match(ladder, it, "cand", f"anc{j}", 20, loop.seed_base(it, "anchor", j), "vs_anchor")
        assert_disjoint(self, L.batches)

    def test_the_old_plan_would_have_failed(self):
        # Before 2026-11-02 gate block b started at 77 + it*1000 + 2*b with 20 games
        # per seat order and the second order at +1: blocks overlapped.
        old = []
        for b in range(6):
            s = 77 + 7 * 1000 + 2 * b
            old.append((s, 20, "d", "d", "x", "y"))
            old.append((s + 1, 20, "d", "d", "y", "x"))
        with self.assertRaises(AssertionError):
            assert_disjoint(self, old)

    def test_panel_plays_both_decks_on_both_seats(self):
        L = loop.Loop(args())
        _, n, rows = L.panel({"iterations": []}, 1, "cand", "cur")
        self.assertEqual(set(rows), {name for name, _, _ in loop.PANEL})
        for name, d_a, d_b in loop.PANEL:
            seen = {(d1, d2, a1) for _, _, d1, d2, a1, _ in L.batches if {d1, d2} == {d_a, d_b}}
            self.assertEqual(seen, {(d_a, d_b, "cand"), (d_a, d_b, "cur"), (d_b, d_a, "cand"), (d_b, d_a, "cur")})
        self.assertEqual(n, sum(r["games"] for r in rows.values()))


if __name__ == "__main__":
    unittest.main()
