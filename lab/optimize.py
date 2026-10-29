#!/usr/bin/env python3
"""optimize — search for a better list by successive halving over deck variants.

A spec names a base deck and the changes worth trying, either explicitly
("candidates") or as one-for-one swaps ("swap": cards that may go out x cards
that may come in). Every candidate, and the base deck as the control, plays the
gauntlet a few games per opponent (seats swapped). The better half survives and
plays again with twice the games, and so on. Every candidate in a round uses the
same seeds, so differences between variants come from the lists, not from luck
of the deal (common random numbers).

The result is a normal lab test file (lab/results/<id>.json, readable by the
dashboard) whose decks are all candidates with their accumulated games, plus an
"optimize" block with the rounds.

usage: optimize.py SPEC.json [--dry-run]

spec = {
  "name": "riven-flex-1",
  "base": "decks/riven_sacramento.txt",
  "agent": "ismcts:sims=20",            # pilot for every candidate
  "opp_agent": "ismcts:sims=20",        # pilot for the gauntlet (default: agent)
  "opponents": [{"deck": "...", "weight": 3, "label": "..."}, ...],
  "candidates": [{"name": "...", "changes": {"maindeck": {...}}}, ...],   # and/or
  "swap": {"out": ["Astral Heron", ...], "in": ["Riven, Deserter", ...], "max": 24, "seed": 1},
  "rounds": [4, 8, 16],                 # games per candidate per opponent, round by round
  "keep": 0.5,                          # fraction kept after each round (at least 2)
  "threads": 4, "seed": 11
}
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import datetime as dt
import json
import os
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import riftlab  # noqa: E402
from riftlab import LAB, ROOT  # noqa: E402


def candidates(spec: dict, reg: riftlab.Registry) -> list[dict]:
    base = riftlab.parse_deck(ROOT / spec["base"])
    out = [{"name": f"{spec['name']}--base", "changes": {}, "label": "base (control)"}]
    for c in spec.get("candidates", []):
        out.append({"name": f"{spec['name']}--{riftlab._slug(c['name'])}", "changes": c["changes"],
                    "label": c["name"]})
    sw = spec.get("swap")
    if sw:
        pairs = [(o, i) for o in sw["out"] for i in sw["in"]
                 if o != i and base["maindeck"].get(o, 0) > 0]
        legal = []
        for o, i in pairs:
            ch = {"maindeck": {o: -1, i: 1}}
            if not riftlab.validate(riftlab.apply_changes(base, ch), reg):
                legal.append((o, i, ch))
        random.Random(sw.get("seed", 1)).shuffle(legal)
        for o, i, ch in legal[: sw.get("max", 24)]:
            label = f"-1 {o} +1 {i}"
            out.append({"name": f"{spec['name']}--{riftlab._slug(label)}", "changes": ch, "label": label})
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec")
    ap.add_argument("--dry-run", action="store_true", help="list the candidates and the game budget")
    a = ap.parse_args(argv)
    spec = json.loads(Path(a.spec).read_text())
    reg = riftlab.Registry.load()
    agent = spec.get("agent", "random")
    opp_agent = spec.get("opp_agent", agent)
    threads = int(spec.get("threads", os.cpu_count() or 1))
    seed = int(spec.get("seed", 1))
    rounds = [int(g) for g in spec.get("rounds", [4, 8, 16])]
    keep = float(spec.get("keep", 0.5))
    opponents = []
    for o in spec["opponents"]:
        o = {"deck": o} if isinstance(o, str) else dict(o)
        o.setdefault("label", Path(o["deck"]).stem)
        o.setdefault("weight", 1.0)
        opponents.append(o)

    cands = candidates(spec, reg)
    for c in cands:
        c["path"] = str(riftlab.build_variant({"name": c["name"], "base": spec["base"],
                                               "changes": c["changes"],
                                               "note": f"optimize {spec['name']}: {c['label']}"},
                                              reg).relative_to(ROOT))
        c["results"] = {o["label"]: {"games": 0, "wins": 0} for o in opponents}

    n, budget = len(cands), 0
    for g in rounds:
        budget += n * g * len(opponents)
        n = max(2, int(round(n * keep)))
    print(f"{len(cands)} candidates (incl. the base list), {len(opponents)} opponents, "
          f"rounds {rounds}: at most {budget} games", flush=True)
    for c in cands:
        print(f"  {c['label']}")
    if a.dry_run:
        return 0

    def rate(c):
        ws = sum(o["weight"] for o in opponents)
        return sum(o["weight"] * (c["results"][o["label"]]["wins"] / max(1, c["results"][o["label"]]["games"]))
                   for o in opponents) / ws

    started = dt.datetime.now(dt.timezone.utc)
    test_id = f"{started.strftime('%Y%m%d-%H%M%S')}-{spec['name']}"
    alive = list(cands)
    log = []
    for r, g in enumerate(rounds):
        # One engine process per (candidate, opponent, seat), `threads` at a
        # time: batches are small, so parallelism comes from running many.
        jobs = []
        for c in alive:
            for i, o in enumerate(opponents):
                s1 = seed + 1000003 * r + 10007 * i        # same seeds for every candidate
                half = max(1, g // 2)
                jobs.append((c, o, 1, (c["path"], o["deck"], agent, opp_agent, half, 1, s1)))
                jobs.append((c, o, 2, (o["deck"], c["path"], opp_agent, agent, max(1, g - half), 1, s1 + 50000)))
        with cf.ThreadPoolExecutor(max_workers=threads) as pool:
            futs = {pool.submit(riftlab.play_batch, *args): (c, o, seat) for c, o, seat, args in jobs}
            for f in cf.as_completed(futs):
                c, o, seat = futs[f]
                x = f.result()
                res = c["results"][o["label"]]
                res["games"] += x["p1"] + x["p2"] + x["draws"]
                res["wins"] += x["p1"] if seat == 1 else x["p2"]
        for c in alive:
            print(f"round {r + 1}  {c['label']:<48} {100 * rate(c):5.1f}%  "
                  f"({sum(v['games'] for v in c['results'].values())} games)", flush=True)
        alive.sort(key=rate, reverse=True)
        log.append({"round": r + 1, "games_per_opponent": g,
                    "standings": [{"label": c["label"], "rate": round(rate(c), 4)} for c in alive]})
        if r + 1 < len(rounds):
            k = max(2, int(round(len(alive) * keep)))
            base = next(c for c in alive if c["label"] == "base (control)")
            alive = alive[:k] if base in alive[:k] else alive[: k - 1] + [base]   # the control stays

    result = {"id": test_id, "name": spec["name"], "kind": "optimize",
              "description": spec.get("description", f"List search around {spec['base']} (successive halving)."),
              "started_utc": started.isoformat(timespec="seconds"),
              "finished_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
              "engine_commit": riftlab.git_commit(), "agent": agent, "opp_agent": opp_agent,
              "games_per_matchup": rounds[-1], "threads": threads, "seed": seed, "spec": spec,
              "optimize": {"rounds": log}, "decks": []}
    for c in sorted(cands, key=rate, reverse=True):
        d = riftlab.parse_deck(ROOT / c["path"])
        drec = {"label": c["label"], "path": c["path"], "hash": riftlab.deck_hash(ROOT / c["path"]),
                "stats": riftlab.stats(d, reg), "errors": [], "matchups": [],
                "changes": c["changes"]}
        for o in opponents:
            res = c["results"][o["label"]]
            lo, hi = riftlab.wilson(res["wins"], res["games"])
            drec["matchups"].append({"opponent": o["label"], "opponent_deck": o["deck"], "weight": o["weight"],
                                     "games": res["games"], "wins": res["wins"],
                                     "win_rate": round(res["wins"] / res["games"], 4) if res["games"] else None,
                                     "ci95": [round(lo, 4), round(hi, 4)]})
        drec["weighted_win_rate"] = round(rate(c), 4)
        result["decks"].append(drec)
    out = LAB / "results" / f"{test_id}.json"
    out.write_text(json.dumps(result, indent=1))
    riftlab.write_index(reg)
    print(out.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
