#!/usr/bin/env python3
"""Assemble the lab dashboard's data files into lab/dashboard/dist/.

dist/index.html     the page (copied from lab/dashboard/index.html)
dist/data.json      tests (full results), decks with stats, variants, learning ladder
dist/replays/*.json game records referenced by data.json (the most recent ones)

usage: build.py [--max-replays 40]
"""
import argparse
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "lab"
DIST = LAB / "dashboard" / "dist"
sys.path.insert(0, str(LAB))
import riftlab  # noqa: E402

SESSION = {"session_id": "session_01F68srW6k6CBYFTm9oVwNVu",
           "environment_id": "env_01TgKU1MBDD1jDjD7f3iW6Gm",
           "repo": "jdisser02-glitch/alpharune", "branch": "claude/zen-goodall-hb2ssu"}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-replays", type=int, default=40)
    a = ap.parse_args(argv)
    reg = riftlab.Registry.load()
    riftlab.write_index(reg)
    index = json.loads((LAB / "index.json").read_text())

    if DIST.exists():
        shutil.rmtree(DIST)
    (DIST / "replays").mkdir(parents=True)

    tests = []
    for t in index["tests"]:
        tests.append(json.loads((ROOT / t["file"]).read_text()))

    replays = []
    for f in sorted((LAB / "replays").rglob("game_*.json"), key=lambda p: p.stat().st_mtime, reverse=True):
        if len(replays) >= a.max_replays:
            break
        rel = f.relative_to(LAB / "replays")
        parts = rel.parts               # <test id>/<deck__vs__opp>/seatN/game_K.json
        g = json.loads(f.read_text())
        name = re.sub(r"[^A-Za-z0-9_.-]+", "_", "__".join(parts))
        shutil.copy(f, DIST / "replays" / name)
        replays.append({"file": f"replays/{name}", "test_id": parts[0] if len(parts) > 3 else "",
                        "matchup": parts[1] if len(parts) > 3 else "", "seat": parts[2] if len(parts) > 3 else "",
                        "game": g.get("game"), "deck1": g.get("deck1"), "deck2": g.get("deck2"),
                        "agent1": g.get("agent1"), "agent2": g.get("agent2"), "winner": g.get("winner"),
                        "scores": g.get("scores"), "turns": g.get("turns"), "decisions": g.get("decisions"),
                        "frames": len(g.get("frames", []))})

    ladder_path = LAB / "learn" / "ladder.json"
    ladder = json.loads(ladder_path.read_text()) if ladder_path.exists() else {"iterations": []}

    data = {"generated_utc": index["generated_utc"], "engine_commit": index["engine_commit"],
            **SESSION, "tests": tests, "decks": index["decks"], "variants": index["variants"],
            "ladder": ladder, "replays": replays,
            "cards": [{"name": c["name"], "type": c["card_type"], "domains": c["domains"],
                       "energy": c.get("energy_cost") or 0, "power": c.get("power_cost") or 0,
                       "might": c.get("might") or 0, "super": c.get("super_type"), "tags": c.get("tags", []),
                       "set": c.get("set")} for c in reg.cards]}
    (DIST / "data.json").write_text(json.dumps(data, separators=(",", ":")))
    shutil.copy(LAB / "dashboard" / "index.html", DIST / "index.html")
    size = sum(p.stat().st_size for p in DIST.rglob("*") if p.is_file())
    print(f"dist: {len(tests)} tests, {len(index['decks'])} decks, {len(replays)} replays, "
          f"{len(ladder.get('iterations', []))} learning iterations, {size / 1e6:.1f} MB")


if __name__ == "__main__":
    main(sys.argv[1:])
