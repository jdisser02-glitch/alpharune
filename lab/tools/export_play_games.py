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
the current engine with the recorded winner, and was not marked "training": false in the Play tab.

    python3 lab/tools/export_play_games.py [--games lab/play_games] [--binary build/riftbound_review]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LEARN = ROOT / "lab" / "learn"
CACHE = ROOT / "lab" / "play_games" / "rows"     # each game's exported rows, kept when a later engine can no longer replay it
sys.path.insert(0, str(ROOT / "lab" / "tools"))
from split_human_by_legend import records  # noqa: E402


def to_version(rows: bytes, src_v4: bool, dst_v4: bool) -> bytes:
    """Records of one file converted to the other file's version: RBV4 = RBV3 + a float32 search value per record
    (NaN for human decisions)."""
    if src_v4 == dst_v4:
        return rows
    out = bytearray()
    fake = (b"RBV4" if src_v4 else b"RBV3") + b"\0" * 8 + rows
    for s_, e_, _ in records(fake):
        rec = fake[s_:e_]
        out += (rec + struct.pack("<f", float("nan"))) if dst_v4 else rec[:-4]
    return bytes(out)


def body_of(path: Path) -> tuple[bytes, bool]:
    b = path.read_bytes()
    return b[12:], b[:4] == b"RBV4"


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
        work = tmp / "human.rbv3"   # keeps the base set's version (RBV3 or RBV4); Play rows are converted to it
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
                one = tmp / f"{gid}.rbv"
                out = subprocess.run([a.binary, "--game", str(path), "--out", str(tmp / gid), "--sims", "0",
                                      "--export-features", str(one)], cwd=ROOT, capture_output=True, text=True,
                                     env={**os.environ, "RIFTBOUND_ROOT": str(ROOT)})
                m = re.search(r"exported (\d+) decisions", out.stdout)
                rev = json.loads((tmp / gid / "summary.json").read_text()) if (tmp / gid / "summary.json").exists() else {}
                won = (rev.get("result") or {}).get("engine_winner")
                if m and won != (doc.get("result") or {}).get("winner"):
                    m = None   # replayed to the end but another player won: the engine has changed under the game
                    rev["stop_reason"] = f"replay winner P{won} differs from the recorded winner"
                cached = CACHE / f"{gid}.rbv"
                status, legends = ("ok" if m else "skipped"), rev.get("legends", {})
                if m and one.exists():
                    CACHE.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(one, cached)
                    (CACHE / f"{gid}.json").write_text(json.dumps({"legends": legends, "players": rev.get("players", [])}))
                elif cached.exists():
                    # The engine changed under this game (a card fix changes the options): keep the rows it gave before.
                    status, one = "cached", cached
                    meta = json.loads((CACHE / f"{gid}.json").read_text()) if (CACHE / f"{gid}.json").exists() else {}
                    legends = meta.get("legends", legends)
                nrows = 0
                if status in ("ok", "cached") and one.exists():
                    body, v4 = body_of(one)
                    wbody, wv4 = body_of(work)
                    conv = to_version(body, v4, wv4)
                    nrows = sum(1 for _ in records((b"RBV4" if wv4 else b"RBV3") + b"\0" * 8 + conv))
                    with open(work, "ab") as f:
                        f.write(conv)
                entry.update({"status": status if nrows else "skipped", "rows": nrows,
                              "stop_reason": rev.get("stop_reason") or (out.stderr.strip()[-300:] or out.stdout.strip()[-300:]),
                              "remapped_by_text": rev.get("remapped_by_text", 0), "legends": legends,
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
