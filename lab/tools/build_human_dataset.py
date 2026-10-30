#!/usr/bin/env python3
"""Build the human imitation dataset from the recorded human games.

Replays every game of lab/human_games/*.json through the engine with
`riftbound_guided --no-bot --export-features` (the same log-following driver
behind lab/human_replays, without the bot), which appends one RBV3 record per
decision whose human move is known and that had more than one option: the
deciding player's masked state features, the legal options' vocab slots and
the human's move as a one-hot target, `source = human` (ml::kPolicyHuman), the
clean-boundary flag, and the log's final result as the label. The replay
frames in lab/human_replays carry no raw features, which is why the engine
re-extracts them here.

    python3 lab/tools/build_human_dataset.py                       # all games -> data/learn/human.rbv3
    python3 lab/tools/build_human_dataset.py --pattern _riven_ --out data/learn/human_riven.rbv3

The output is read by lab/learn/train_policy.py (--human), which weights the
rows and holds part of the games out for the top-1 / top-3 agreement metric.
A JSON summary (rows per game, decision kinds, clean-boundary share) is written
beside the data file.
"""
import argparse
import collections
import datetime as dt
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lab" / "tools"))
sys.path.insert(0, str(ROOT / "lab" / "learn"))
from human_review import select_games  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "data" / "learn" / "human.rbv3"))
    ap.add_argument("--pattern", default="", help="substring of the file name (default: every file)")
    ap.add_argument("--limit", type=int, default=0, help="stop after this many games")
    ap.add_argument("--min-events", type=int, default=30, help="skip games with fewer log lines")
    ap.add_argument("--binary", default=str(ROOT / "build" / "riftbound_guided"))
    ap.add_argument("--timeout", type=int, default=1800, help="seconds per game")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--max-desyncs", type=int, default=25)
    ap.add_argument("--max-decisions", type=int, default=400)
    ap.add_argument("--keep-replays", default="", help="directory to keep each game's game_1.json / summary.json")
    a = ap.parse_args(argv)

    binary = pathlib.Path(a.binary)
    if not binary.exists():
        sys.exit(f"{binary} not found: cmake --build <build dir> --target riftbound_guided")
    out = pathlib.Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    jobs = select_games(a.pattern, not a.pattern, a.min_events)
    if a.limit:
        jobs = jobs[: a.limit]
    print(f"{len(jobs)} game(s) -> {out}")
    work = pathlib.Path(a.keep_replays) if a.keep_replays else pathlib.Path(tempfile.mkdtemp(prefix="human_dataset_"))
    work.mkdir(parents=True, exist_ok=True)
    games, t_all = [], time.time()
    for f, game, _ in jobs:
        out_dir = work / f"{f.stem}_g{game}"
        cmd = [str(binary), "--game", str(f), "--index", str(game), "--no-bot", "--out", str(out_dir),
               "--root", str(ROOT), "--seed", str(a.seed), "--max-desyncs", str(a.max_desyncs),
               "--max-decisions", str(a.max_decisions), "--export-features", str(out)]
        t0 = time.time()
        try:
            p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=a.timeout)
            status = "ok" if p.returncode == 0 else f"exit {p.returncode}"
            tail = (p.stdout + p.stderr).strip().splitlines()[-1:] if p.returncode else []
        except subprocess.TimeoutExpired:
            status, tail = "timeout", []
        entry = {"file": f.name, "game": game, "status": status, "seconds": round(time.time() - t0, 1)}
        summary_path = out_dir / "summary.json"
        if summary_path.exists():
            s = json.loads(summary_path.read_text())
            entry.update({"rows": s.get("exported_records", 0), "decisions": s.get("decisions"),
                          "human_known": s.get("decisions_human_known"), "desyncs": s.get("desyncs"),
                          "stop_reason": s.get("stop_reason"), "players": s.get("players"),
                          "legends": s.get("legends")})
            frames_path = out_dir / "game_1.json"
            if frames_path.exists():
                frames = json.loads(frames_path.read_text()).get("frames", [])
                kinds = collections.Counter(fr.get("kind", "?") for fr in frames
                                            if fr.get("human_known") and len(fr.get("options", [])) > 1)
                entry["kinds"] = dict(kinds)
                entry["clean_rows"] = sum(1 for fr in frames if fr.get("human_known") and fr.get("clean_boundary")
                                          and len(fr.get("options", [])) > 1)
        games.append(entry)
        print(f"  {f.stem[:60]} g{game}: {status}, {entry.get('rows', 0)} rows ({entry['seconds']}s)" +
              ("".join("\n     " + t for t in tail)))
    if not a.keep_replays:
        shutil.rmtree(work, ignore_errors=True)

    total = {"rows": 0, "clean_rows": 0, "games": 0, "kinds": collections.Counter()}
    for g in games:
        if g.get("rows"):
            total["games"] += 1
            total["rows"] += g["rows"]
            total["clean_rows"] += g.get("clean_rows", 0)
            total["kinds"].update(g.get("kinds", {}))
    total["kinds"] = dict(total["kinds"])
    if out.exists():
        from train_value import read_records  # noqa: E402
        d = read_records(out)
        total["records_in_file"] = int(len(d["y"]))
        total["human_source_records"] = int((d["source"] == 2).sum())
        total["vocab"] = d["vocab"]
        total["dim"] = d["dim"]
    report = {"generated": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"), "out": str(out),
              "binary": str(binary), "seed": a.seed, "seconds": round(time.time() - t_all, 1),
              "total": total, "games": games}
    out.with_suffix(".json").write_text(json.dumps(report, indent=1) + "\n")
    print(f"\n{total['rows']} rows from {total['games']} games ({total['clean_rows']} at clean boundaries); "
          f"kinds {total['kinds']}\n-> {out}, {out.with_suffix('.json')}")


if __name__ == "__main__":
    main()
