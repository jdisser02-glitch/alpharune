#!/usr/bin/env python3
"""compare_matrix — simulated matchups against real players' win rates.

Reads a lab result (lab/results/<id>.json, kind test) and a real matchup
matrix (lab/meta/riftdecks_winrate_*.json) and prints, for each deck and
opponent whose legends are both in the matrix, the simulated win rate next to
the real one with its 95% interval. A simulated rate outside the real
interval by a wide margin points at a matchup the bot plays badly (one side
misplayed, or an engine bug in one of the decks), so the lab's verdicts on
that matchup are not to be trusted yet.

usage: compare_matrix.py RESULT.json [--matrix lab/meta/riftdecks_winrate_2026-11-03.json] [--json OUT]
"""
from __future__ import annotations

import argparse
import glob
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lab"))
import riftlab  # noqa: E402


def norm(s: str) -> str:
    return re.sub(r"[^a-z0-9]", "", s.lower())


def legend_of(text: str, legends: list[str]) -> str | None:
    """The matrix legend named in a deck path or label (longest match first)."""
    t = norm(text)
    for name in sorted(legends, key=len, reverse=True):
        if norm(name) in t:
            return name
    return None


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("result")
    ap.add_argument("--matrix", default=sorted(glob.glob(str(ROOT / "lab/meta/riftdecks_winrate_*.json")))[-1])
    ap.add_argument("--json", default="")
    a = ap.parse_args(argv)
    res = json.loads(Path(a.result).read_text())
    mx = json.loads(Path(a.matrix).read_text())
    legends = list(dict.fromkeys(list(mx["matchups"]) + mx.get("columns", [])))
    rows = []
    for d in res["decks"]:
        me = legend_of(d["path"] + " " + d["label"], legends) or "Riven"
        for m in d["matchups"]:
            opp = legend_of(m.get("opponent_deck", "") + " " + m["opponent"], legends)
            real = mx["matchups"].get(me, {}).get(opp) if opp and opp != me else None
            if not real or not m["games"]:
                continue
            lo, hi = riftlab.wilson(real["win_rate"] * real["games"], real["games"])
            sim = m["wins"] / m["games"]
            gap = sim - real["win_rate"]
            verdict = "far above" if sim > hi + 0.05 else "far below" if sim < lo - 0.05 else "within"
            rows.append({"deck": d["label"], "legend": me, "opponent": m["opponent"], "opponent_legend": opp,
                         "sim": round(sim, 3), "sim_games": m["games"], "real": real["win_rate"], "real_games": real["games"],
                         "real_ci95": [round(lo, 3), round(hi, 3)], "gap": round(gap, 3), "verdict": verdict})
    print(f"{res.get('agent')}  vs  {Path(a.matrix).name}")
    print(f"{'deck':<30} {'opponent':<34} {'sim':>6} {'real':>6} {'real 95%':>13}  verdict")
    for r in rows:
        print(f"{r['deck'][:30]:<30} {r['opponent'][:34]:<34} {100*r['sim']:5.1f}% {100*r['real']:5.1f}% "
              f"[{100*r['real_ci95'][0]:4.1f}-{100*r['real_ci95'][1]:4.1f}]  {r['verdict']}")
    if a.json:
        Path(a.json).write_text(json.dumps({"result": a.result, "matrix": a.matrix, "rows": rows}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
