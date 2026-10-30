#!/usr/bin/env python3
"""Replay recorded human games through the engine and compare with the bot.

Runs ./build/riftbound_guided on every game of the selected human-game files
(default: the Riven games, `--all` for every file), one output directory per
game under lab/human_replays/<file stem>_g<N>/ (game_1.json in the dashboard's
replay-frame format + summary.json), then writes lab/human_replays/index.json
from every summary found there.

    python3 lab/tools/human_review.py                      # the Riven games
    python3 lab/tools/human_review.py --all --agent ismcts:sims=20,eval=board
    python3 lab/tools/human_review.py --pattern kai_sa --both
"""
import argparse
import datetime as dt
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
GAMES = ROOT / "lab" / "human_games"
OUT = ROOT / "lab" / "human_replays"
DEFAULT_AGENT = "ismcts:sims=40,eval=net:lab/learn/nets/net_004.rbvn"


def select_games(pattern: str, all_files: bool, min_events: int):
    files = sorted(GAMES.glob("*.json"))
    if not all_files:
        files = [f for f in files if pattern in f.name]
    jobs = []
    for f in files:
        try:
            d = json.loads(f.read_text())
        except Exception as e:  # noqa: BLE001
            print(f"skip {f.name}: {e}", file=sys.stderr)
            continue
        for g in d.get("games", []):
            rng = g.get("events") or [0, 0]
            if rng[1] - rng[0] < min_events:
                continue
            jobs.append((f, int(g.get("game", 1)), d))
    return jobs


def run_one(binary: pathlib.Path, f: pathlib.Path, game: int, a) -> dict:
    out_dir = OUT / f"{f.stem}_g{game}"
    cmd = [str(binary), "--game", str(f), "--index", str(game), "--agent", a.agent,
           "--out", str(out_dir), "--root", str(ROOT), "--seed", str(a.seed),
           "--max-desyncs", str(a.max_desyncs), "--max-decisions", str(a.max_decisions)]
    if a.both:
        cmd.append("--both")
    if a.no_bot:
        cmd.append("--no-bot")
    t0 = time.time()
    try:
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=a.timeout)
        status = "ok" if p.returncode == 0 else f"exit {p.returncode}"
        tail = (p.stdout + p.stderr).strip().splitlines()[-3:]
    except subprocess.TimeoutExpired:
        status, tail = "timeout", []
    elapsed = time.time() - t0
    print(f"{f.stem} g{game}: {status} ({elapsed:.0f}s)")
    for line in tail:
        print("   " + line)
    return {"file": f.name, "game": game, "status": status, "elapsed": round(elapsed, 1), "out_dir": str(out_dir.relative_to(ROOT))}


def build_index(agent: str, runs: list) -> dict:
    entries = []
    for summary_path in sorted(OUT.glob("*/summary.json")):
        try:
            s = json.loads(summary_path.read_text())
        except Exception:  # noqa: BLE001
            continue
        rel = summary_path.parent.relative_to(ROOT)
        causes = sorted(s.get("desync_causes", {}).items(), key=lambda kv: -kv[1])[:3]
        agreement = s.get("agreement", {})
        entries.append({
            "file": pathlib.Path(s.get("source", "")).name,
            "game": s.get("game"),
            "players": s.get("players"),
            "capture_player": s.get("capture_player"),
            "legends": s.get("legends"),
            "bot": s.get("bot"),
            "decisions": s.get("decisions"),
            "decisions_compared": s.get("decisions_compared"),
            "agreement_pct": round(100 * agreement.get("rate", 0.0), 1),
            "agreement_clean_pct": round(100 * agreement.get("clean_boundary", {}).get("rate", 0.0), 1),
            "compared_clean": agreement.get("clean_boundary", {}).get("n"),
            "desyncs": s.get("desyncs"),
            # desyncs the log itself shows impossible (guided_replay's
            # classifier), and the rest: the engine-side count a fidelity
            # gate should use
            "log_artefacts": s.get("log_artefacts", 0),
            "desyncs_engine": s.get("desyncs_engine", s.get("desyncs")),
            "top_desync_causes": [{"cause": c, "n": n} for c, n in causes],
            "silent_passes": s.get("silent_passes"),
            "hand_fixes": s.get("hand_fixes", {}).get("pulled_from_deck", 0) + s.get("hand_fixes", {}).get("created", 0),
            "stop_reason": s.get("stop_reason"),
            "engine_scores": s.get("result", {}).get("engine_scores"),
            "log_scores": s.get("result", {}).get("log_scores"),
            "seconds": round(s.get("timing", {}).get("seconds", 0.0), 1),
            "path": str(rel / "game_1.json"),
            "summary": str(rel / "summary.json"),
        })
    return {"generated": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
            "agent": agent, "runs": runs, "games": entries}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--agent", default=DEFAULT_AGENT)
    ap.add_argument("--pattern", default="_riven_", help="substring of the file name (default: the Riven games)")
    ap.add_argument("--all", action="store_true", help="every human-game file")
    ap.add_argument("--both", action="store_true", help="compare the bot at both players' decisions")
    ap.add_argument("--no-bot", action="store_true", help="replay only")
    ap.add_argument("--limit", type=int, default=0, help="stop after this many games")
    ap.add_argument("--min-events", type=int, default=30, help="skip games with fewer log lines")
    ap.add_argument("--binary", default=str(ROOT / "build" / "riftbound_guided"))
    ap.add_argument("--timeout", type=int, default=3600, help="seconds per game")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--max-desyncs", type=int, default=25)
    ap.add_argument("--max-decisions", type=int, default=400)
    ap.add_argument("--index-only", action="store_true", help="only rebuild index.json")
    a = ap.parse_args(argv)

    OUT.mkdir(parents=True, exist_ok=True)
    runs = []
    if not a.index_only:
        binary = pathlib.Path(a.binary)
        if not binary.exists():
            sys.exit(f"{binary} not found: cmake --build build --target riftbound_guided")
        jobs = select_games(a.pattern, a.all, a.min_events)
        if a.limit:
            jobs = jobs[: a.limit]
        print(f"{len(jobs)} game(s), agent {a.agent}")
        for f, game, _ in jobs:
            runs.append(run_one(binary, f, game, a))
    index = build_index(a.agent, runs)
    (OUT / "index.json").write_text(json.dumps(index, indent=1) + "\n")

    print(f"\n{'game':58} {'cmp':>4} {'agree':>6} {'clean':>6} {'desync':>6} {'engine':>6} {'stop'}")
    for e in index["games"]:
        name = f"{pathlib.Path(e['file']).stem[:48]} g{e['game']}"
        print(f"{name:58} {e['decisions_compared'] or 0:4d} {e['agreement_pct']:5.1f}% {e['agreement_clean_pct']:5.1f}% "
              f"{e['desyncs'] or 0:6d} {e['desyncs_engine'] or 0:6d} {e['stop_reason']}")
    print(f"\nindex: {OUT / 'index.json'}")


if __name__ == "__main__":
    main()
