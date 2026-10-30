#!/usr/bin/env python3
"""review_games — review games saved by the dashboard's Play tab.

Each saved game (a JSON document from the artifact database, collection
"games") is replayed exactly by riftbound_review; at every real decision of
the player the strong bot says what it would do, and each disagreement is
played out both ways to see which line wins. The result goes under
lab/human_replays/play_<id>/ and into lab/human_replays/index.json, so the
dashboard's Replays tab shows it next to the human games (with the "why" notes
and the win rate of each line).

usage: review_games.py GAME.json [GAME.json ...] [--sims 300] [--rollouts 12] [--rollout-sims 24]
       RIFTBOUND_REVIEW_BIN=build/riftbound_review (default)
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPLAYS = ROOT / "lab" / "human_replays"


def review(doc_path: Path, a) -> dict | None:
    doc = json.loads(doc_path.read_text())
    gid = doc.get("id") or doc_path.stem
    out = REPLAYS / f"play_{gid}"
    out.mkdir(parents=True, exist_ok=True)
    cmd = [os.environ.get("RIFTBOUND_REVIEW_BIN", str(ROOT / "build" / "riftbound_review")),
           "--game", str(doc_path), "--out", str(out), "--sims", str(a.sims), "--rollouts", str(a.rollouts),
           "--rollout-sims", str(a.rollout_sims)]
    if a.eval:
        cmd += ["--eval", a.eval]
    t0 = dt.datetime.now()
    print(f"== {gid}: {' '.join(cmd[1:])}", flush=True)
    r = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stderr.write(r.stderr[-2000:])
        return None
    for line in r.stderr.splitlines():
        if line.startswith("  decision"):
            print(line, flush=True)
    s = json.loads((out / "summary.json").read_text())
    ag = s.get("agreement", {})
    return {"file": f"play_{gid}.json", "game": 1, "players": s.get("players"), "capture_player": "You",
            "legends": s.get("legends"), "bot": s.get("bot"), "decisions": s.get("decisions"),
            "decisions_compared": ag.get("n", 0), "agreement_pct": round(100 * ag.get("rate", 0), 1),
            "agreement_clean_pct": round(100 * ag.get("rate", 0), 1), "compared_clean": ag.get("n", 0),
            "desyncs": 0, "top_desync_causes": [], "silent_passes": 0, "hand_fixes": 0,
            "stop_reason": s.get("stop_reason"), "engine_scores": s["result"]["engine_scores"],
            "log_scores": s["result"]["engine_scores"], "seconds": round((dt.datetime.now() - t0).total_seconds(), 1),
            "path": str((out / "game_1.json").relative_to(ROOT)), "summary": str((out / "summary.json").relative_to(ROOT)),
            "source": "play", "play_level": doc.get("level"), "played": doc.get("finished") or doc.get("started"),
            "rollouts": a.rollouts}


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("games", nargs="+")
    ap.add_argument("--sims", type=int, default=300)
    ap.add_argument("--eval", default="")
    ap.add_argument("--rollouts", type=int, default=12)
    ap.add_argument("--rollout-sims", type=int, default=24)
    a = ap.parse_args(argv)
    idx_path = REPLAYS / "index.json"
    idx = json.loads(idx_path.read_text()) if idx_path.exists() else {"generated": "", "agent": "", "runs": [], "games": []}
    for g in a.games:
        e = review(Path(g), a)
        if not e:
            continue
        idx["games"] = [x for x in idx["games"] if x.get("path") != e["path"]] + [e]
    idx["generated"] = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    idx_path.write_text(json.dumps(idx, indent=1))
    print(f"index: {len(idx['games'])} games")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
