#!/usr/bin/env python3
"""One iteration of the learning loop (loop.py), split into stages that run on
several machines at once (riftlab.yml, the "p_*" training jobs):

  plan      reads the ladder: iteration number, current / reference / anchor
            nets, the loop settings, and the list of evaluation tasks;
  selfplay  shard k of S plays its share of the self-play games and writes
            data/learn/iter_<it>_s<k>.rbvd;
  train     merges the shards into iter_<it>.rbvd (one file per iteration:
            the trainer weights recency per file) and trains the candidate;
  gate      one evaluation task: a mirror gate block, one panel matchup, or
            one yardstick (random, the reference net, the anchor nets);
  decide    adds the task results up, applies the same rules as loop.py
            (SPRT on all the gate games, then --accept; the panel floor) and
            writes the ladder row.

Every stage reads the loop's settings from the plan, so all machines play and
judge with the same bot; seeds come from loop.seed_base, so no two shards or
tasks replay the same deals.

usage:
  stages.py plan --out plan.json -- [loop.py options]
  stages.py selfplay --plan plan.json --shard K --shards S
  stages.py train --plan plan.json --out cand.json
  stages.py gate --plan plan.json --cand cand.json --task gate:0 --out result.json
  stages.py decide --plan plan.json --cand cand.json RESULT.json [...]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import random
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import loop  # noqa: E402
from loop import seed_base  # noqa: E402

PER_PAIR = 4


def settings(plan: dict):
    return loop.make_parser().parse_args(plan["args"])


def tasks_for(a) -> list[str]:
    blocks = max(1, a.gate_max // a.gate_block)
    out = [f"gate:{b}" for b in range(blocks)]
    if a.panel_games > 0:
        out += [f"panel:{m}" for m in range(len(loop.PANEL))]
    out += ["yard:random", "yard:reference", "yard:anchors"]
    return out


def cmd_plan(args, rest):
    a = loop.make_parser().parse_args(rest)
    ladder = loop.load_ladder()
    current = ladder.get("current_net")
    if current and not (loop.ROOT / current).exists():
        current = None
    reference = ladder.get("reference_net")
    if reference and not (loop.ROOT / reference).exists():
        reference = None
    plan = {"it": len(ladder["iterations"]), "current": current, "reference": reference,
            "anchors": loop.pick_anchors(ladder), "args": rest, "tasks": tasks_for(a),
            "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"), "t0": time.time()}
    Path(args.out).write_text(json.dumps(plan, indent=1))
    print(json.dumps({k: plan[k] for k in ("it", "current", "tasks")}))


def cmd_selfplay(args, _):
    plan = json.loads(Path(args.plan).read_text())
    a = settings(plan)
    it, current = plan["it"], plan["current"]
    L = loop.Loop(a)
    loop.DATA.mkdir(parents=True, exist_ok=True)
    out = loop.DATA / f"iter_{it:03d}_s{args.shard}.rbvd"
    out.unlink(missing_ok=True)
    games = a.games * 3 if current is None else a.games
    pairs = L.self_play_pairs(random.Random(a.seed + it), loop.deck_pool(), max(1, games // PER_PAIR))
    t0 = time.time()
    for k, (d1, d2) in enumerate(pairs):
        if k % args.shards != args.shard:
            continue
        L.batch(d1, d2, L.bot(current), L.bot(current), PER_PAIR, seed_base(it, "selfplay", k), out)
    meta = {"shard": args.shard, "minutes": round((time.time() - t0) / 60, 1), "batches": len(L.batches)}
    out.with_suffix(".json").write_text(json.dumps(meta))
    print(json.dumps(meta))


def merge_shards(it: int) -> Path:
    """iter_<it>_s*.rbvd -> iter_<it>.rbvd: the first file whole, the others
    without their 12-byte header (RBV3 magic, feature dim, vocab size)."""
    shards = sorted(loop.DATA.glob(f"iter_{it:03d}_s*.rbvd"))
    if not shards:
        raise SystemExit(f"no self-play shard for iteration {it} in {loop.DATA}")
    out = loop.DATA / f"iter_{it:03d}.rbvd"
    head = None
    with open(out, "wb") as f:
        for i, s in enumerate(shards):
            b = s.read_bytes()
            if len(b) < 12 or b[:4] != b"RBV3":
                raise SystemExit(f"{s}: not an RBV3 file")
            if head is None:
                head = b[:12]
            elif b[:12] != head:
                raise SystemExit(f"{s}: header differs from {shards[0]}")
            f.write(b if i == 0 else b[12:])
    for s in shards:
        s.unlink()
    return out


def cmd_train(args, _):
    plan = json.loads(Path(args.plan).read_text())
    a = settings(plan)
    it = plan["it"]
    sp = [json.loads(p.read_text()) for p in loop.DATA.glob(f"iter_{it:03d}_s*.json")]
    merge_shards(it)
    for p in loop.DATA.glob(f"iter_{it:03d}_s*.json"):
        p.unlink()
    loop.NETS.mkdir(parents=True, exist_ok=True)
    row = {"iteration": it, "started_utc": plan["started_utc"], "player": loop.Loop(a).bot(plan["current"]),
           "self_play_games": a.games, "sims": a.sims, "agent": a.agent,
           "trainer": "train_policy" if a.policy else "train_value",
           "self_play_minutes": max((m["minutes"] for m in sp), default=None), "shards": len(sp)}
    cand = loop.train_candidate(a, it, plan["current"], row)
    Path(args.out).write_text(json.dumps({"cand": cand, "row": row}, indent=1))
    print(cand)


def cmd_gate(args, _):
    plan = json.loads(Path(args.plan).read_text())
    a = settings(plan)
    it, current, reference = plan["it"], plan["current"], plan["reference"]
    cand = json.loads(Path(args.cand).read_text())["cand"]
    L = loop.Loop(a)
    ladder = {"matches": []}
    kind, _, idx = args.task.partition(":")
    res = {"task": args.task, "kind": kind}
    if kind == "gate":
        w, l, g = L.match(ladder, it, L.bot(cand), L.bot(current), a.gate_block, seed_base(it, "gate", int(idx)), "gate")
        res.update(wins=w, losses=l, games=g)
    elif kind == "panel":
        m = int(idx)
        name, d_a, d_b = loop.PANEL[m]
        per = max(4, a.panel_games // 2)
        w1, l1, g1 = L.match(ladder, it, L.bot(cand), L.bot(current), per, seed_base(it, "panel", 2 * m),
                             "panel:" + name, d_a, d_b, record=False)
        w2, l2, g2 = L.match(ladder, it, L.bot(cand), L.bot(current), per, seed_base(it, "panel", 2 * m + 1),
                             "panel:" + name, d_b, d_a, record=False)
        res.update(name=name, wins=w1 + w2, losses=l1 + l2, games=g1 + g2)
    elif kind == "yard" and idx == "random":
        r_w, _, r_n = L.match(ladder, it, L.bot(cand), "random", a.yardstick_games, seed_base(it, "vs_random"), "vs_random")
        res["vs_random"] = {"games": r_n, "wins": r_w, "rate": round(r_w / r_n, 4)}
    elif kind == "yard" and idx == "reference":
        if reference and reference != cand:
            f_w, _, f_n = L.match(ladder, it, L.bot(cand), L.bot(reference), a.yardstick_games,
                                  seed_base(it, "vs_reference"), "vs_reference")
            res["vs_reference"] = {"opponent": L.bot(reference), "games": f_n, "wins": f_w, "rate": round(f_w / f_n, 4)}
    elif kind == "yard" and idx == "anchors":
        anchors = {}
        if a.anchor_games > 0:
            for j, anc in enumerate(x for x in plan["anchors"] if x != cand):
                g_w, _, g_n = L.match(ladder, it, L.bot(cand), L.bot(anc), a.anchor_games, seed_base(it, "anchor", j), "vs_anchor")
                anchors[anc] = {"games": g_n, "wins": g_w, "rate": round(g_w / g_n, 4)}
        if anchors:
            res["vs_anchors"] = anchors
    else:
        raise SystemExit(f"unknown task {args.task}")
    res["matches"] = ladder["matches"]
    res["batches"] = [list(b) for b in L.batches]
    Path(args.out).write_text(json.dumps(res, indent=1))
    print(json.dumps({k: v for k, v in res.items() if k not in ("matches", "batches")}))


def decide(plan: dict, cand_info: dict, results: list[dict], ladder: dict) -> dict:
    """The ladder row of the iteration, with the same rules as loop.py; updates
    `ladder` (matches, current / reference / anchor nets, Elo, the row)."""
    a = settings(plan)
    it, current, reference = plan["it"], plan["current"], plan["reference"]
    cand, row = cand_info["cand"], dict(cand_info["row"])
    missing = set(plan["tasks"]) - {r["task"] for r in results}
    if missing:
        raise SystemExit(f"missing task results: {sorted(missing)}")
    for r in results:
        ladder.setdefault("matches", []).extend(r.get("matches", []))
    gate = [r for r in results if r["kind"] == "gate"]
    wins, losses, n = (sum(r[k] for r in gate) for k in ("wins", "losses", "games"))
    llr = loop.sprt_llr(wins, losses)
    upper, lower = math.log((1 - 0.05) / 0.05), math.log(0.05 / (1 - 0.05))
    decision = "better" if llr >= upper else "worse" if llr <= lower else ("rate" if n >= a.gate_min else "short")
    lo, hi = loop.riftlab.wilson(wins, n)
    row["vs_current"] = {"opponent": loop.Loop(a).bot(current), "games": n, "wins": wins, "rate": round(wins / n, 4),
                         "ci95": [round(lo, 4), round(hi, 4)], "llr": round(llr, 3), "sprt": decision}
    mirror_ok = current is None or decision == "better" or (decision == "rate" and wins / n >= a.accept)
    panel = [r for r in results if r["kind"] == "panel"]
    panel_ok = True
    if panel and current is not None:
        p_w, p_l, p_n = (sum(r[k] for r in panel) for k in ("wins", "losses", "games"))
        p_lo, p_hi = loop.riftlab.wilson(p_w, p_n)
        panel_ok = p_w / p_n >= a.panel_floor
        row["panel"] = {"games": p_n, "wins": p_w, "rate": round(p_w / p_n, 4), "ci95": [round(p_lo, 4), round(p_hi, 4)],
                        "floor": a.panel_floor, "ok": panel_ok,
                        "matchups": {r["name"]: {"games": r["games"], "wins": r["wins"], "rate": round(r["wins"] / r["games"], 4)}
                                     for r in panel}}
        ladder["matches"].append({"iteration": it, "label": "panel", "a": loop.Loop.key(loop.Loop(a).bot(cand)),
                                  "b": loop.Loop.key(loop.Loop(a).bot(current)), "games": p_n, "wins_a": p_w, "wins_b": p_l})
    for r in results:
        if r["kind"] == "yard":
            for k in ("vs_random", "vs_reference", "vs_anchors"):
                if k in r:
                    row[k] = r[k]
    if plan["anchors"] and not ladder.get("anchor_nets"):
        ladder["anchor_nets"] = plan["anchors"]
    accepted = mirror_ok and panel_ok
    row["net"] = cand
    row["accepted"] = accepted
    if accepted:
        current = cand
        ladder["current_net"] = cand
    if reference is None:
        ladder["reference_net"] = cand
    elo = loop.elo_fit(ladder.get("matches", []))
    ladder["elo"] = elo
    row["elo"] = elo.get(f"net:{cand}")
    row["elo_current"] = elo.get(f"net:{current}") if current else 0.0
    row["minutes"] = round((time.time() - plan["t0"]) / 60, 1)
    row["parallel"] = True
    ladder["iterations"].append(row)
    return row


def cmd_decide(args, _):
    plan = json.loads(Path(args.plan).read_text())
    ladder = loop.load_ladder()
    if len(ladder["iterations"]) != plan["it"]:
        raise SystemExit(f"the ladder moved on: {len(ladder['iterations'])} iterations, the plan is for {plan['it']}")
    results = [json.loads(Path(p).read_text()) for p in args.results]
    row = decide(plan, json.loads(Path(args.cand).read_text()), results, ladder)
    loop.LADDER.write_text(json.dumps(ladder, indent=1))
    print(json.dumps(row))


def main(argv):
    rest = []
    if "--" in argv:
        i = argv.index("--")
        argv, rest = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan"); p.add_argument("--out", required=True)
    p = sub.add_parser("selfplay"); p.add_argument("--plan", required=True)
    p.add_argument("--shard", type=int, required=True); p.add_argument("--shards", type=int, required=True)
    p = sub.add_parser("train"); p.add_argument("--plan", required=True); p.add_argument("--out", required=True)
    p = sub.add_parser("gate"); p.add_argument("--plan", required=True); p.add_argument("--cand", required=True)
    p.add_argument("--task", required=True); p.add_argument("--out", required=True)
    p = sub.add_parser("decide"); p.add_argument("--plan", required=True); p.add_argument("--cand", required=True)
    p.add_argument("results", nargs="+")
    args = ap.parse_args(argv)
    {"plan": cmd_plan, "selfplay": cmd_selfplay, "train": cmd_train, "gate": cmd_gate, "decide": cmd_decide}[args.cmd](args, rest)


if __name__ == "__main__":
    main(sys.argv[1:])
