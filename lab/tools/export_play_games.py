#!/usr/bin/env python3
"""export_play_games — teach the bot from the games played in the dashboard's Play tab.

Each saved Play game (a JSON document of the artifact database's "games" collection, kept under
lab/play_games/<id>.json) is replayed by riftbound_review with --sims 0 --export-features: every real
decision of the player becomes an imitation record (source = human), the same kind build_human_dataset.py
makes from RiftLite games, labelled with the game's result.

The imitation set the training reads (lab/learn/human.rbv3 and its summary lab/learn/human.json) is rebuilt
as the RiftLite games (lab/learn/human_riftlite.rbv3 / .json, made once from the current files) followed by
the Play games, one summary entry per game with its legends, so split_human_by_legend.py hands each
specialist the decisions of its legend's player. A game is used when it finished, replays to the end under
the current engine, and was not marked "training": false in the Play tab.

    python3 lab/tools/export_play_games.py [--games lab/play_games] [--binary build/riftbound_review]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LEARN = ROOT / "lab" / "learn"


def base_set() -> tuple[Path, Path]:
    """The RiftLite-only imitation set, split off the current files the first time."""
    data, summary = LEARN / "human_riftlite.rbv3", LEARN / "human_riftlite.json"
    if not data.exists():
        doc = json.loads((LEARN / "human.json").read_text())
        if any(str(g.get("file", "")).startswith("play:") for g in doc["games"]):
            raise SystemExit("lab/learn/human.json already holds Play games but human_riftlite.* is missing")
        shutil.copyfile(LEARN / "human.rbv3", data)
        shutil.copyfile(LEARN / "human.json", summary)
    return data, summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--games", default=str(ROOT / "lab" / "play_games"))
    ap.add_argument("--binary", default=os.environ.get("RIFTBOUND_REVIEW_BIN", str(ROOT / "build" / "riftbound_review")))
    ap.add_argument("--out", default=str(LEARN / "human.rbv3"))
    ap.add_argument("--summary", default=str(LEARN / "human.json"))
    a = ap.parse_args(argv)

    base_data, base_summary = base_set()
    summary = json.loads(base_summary.read_text())
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        work = tmp / "human.rbv3"
        shutil.copyfile(base_data, work)
        played = []
        for path in sorted(Path(a.games).glob("*.json")):
            doc = json.loads(path.read_text())
            gid = doc.get("id") or path.stem
            entry = {"file": f"play:{gid}", "game": 1, "source": "play", "rows": 0, "decisions": len(doc.get("decisions", [])),
                     "me": doc.get("me"), "opponent": doc.get("opponent"), "seat": doc.get("seat"), "level": doc.get("level")}
            if doc.get("in_progress") or not doc.get("finished"):
                entry["status"] = "not finished"
            elif doc.get("training") is False:
                entry["status"] = "not a training game"
            else:
                out = subprocess.run([a.binary, "--game", str(path), "--out", str(tmp / gid), "--sims", "0",
                                      "--export-features", str(work)], cwd=ROOT, capture_output=True, text=True,
                                     env={**os.environ, "RIFTBOUND_ROOT": str(ROOT)})
                m = re.search(r"exported (\d+) decisions", out.stdout)
                rev = json.loads((tmp / gid / "summary.json").read_text()) if (tmp / gid / "summary.json").exists() else {}
                entry.update({"status": "ok" if m else "skipped", "rows": int(m.group(1)) if m else 0,
                              "stop_reason": rev.get("stop_reason") or (out.stderr.strip()[-300:] or out.stdout.strip()[-300:]),
                              "remapped_by_text": rev.get("remapped_by_text", 0), "legends": rev.get("legends", {}),
                              "players": rev.get("players", []), "result": (doc.get("result") or {}).get("winner")})
            played.append(entry)
            print(f"{gid}: {entry['status']}, {entry['rows']} decisions" + (f" ({entry.get('stop_reason')})" if entry["status"] != "ok" else ""), flush=True)
        rows = sum(g["rows"] for g in played)
        summary["games"] = summary["games"] + [g for g in played if g["rows"]]
        summary["play"] = {"generated": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
                           "games": played, "rows": rows, "base": str(base_data.relative_to(ROOT))}
        summary["total"] = {**summary.get("total", {}), "rows": summary["total"]["rows"] + rows} if "total" in summary else {"rows": rows}
        shutil.copyfile(work, a.out)
    Path(a.summary).write_text(json.dumps(summary, indent=1))
    print(f"wrote {a.out}: {summary['total']['rows']} decisions ({rows} from {sum(1 for g in played if g['rows'])} Play game(s))")


if __name__ == "__main__":
    sys.exit(main())
