#!/usr/bin/env python3
"""Split the human imitation data (human.rbv3, built by build_human_dataset.py) by the deciding player's legend.

Every record of human.rbv3 is one human decision; records come game after game, in the order of the summary
beside the data file (human.json: per game its row count and both players' legends). The record's perspective
byte says which seat decided, so the seat's legend names the record's champion (the legend card's champion tag,
e.g. Heart of the Tempest -> Kennen). Writes human_<champion>.rbv3 (only that champion's player's decisions),
and human_others_<champion>.rbv3 (every other decision), same header, and prints the decisions and games per
champion.

    python3 lab/tools/split_human_by_legend.py [--data lab/learn/human.rbv3] [--summary data/learn/human.json]
                                               [--out-dir lab/learn/human] [--min 1]
"""
import argparse
import collections
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lab"))
import riftlab  # noqa: E402


def champion_of_legend(reg) -> dict:
    """Legend card name -> champion slug (its champion tag), e.g. 'Heart of the Tempest' -> 'kennen'."""
    # Champion names: the part before the comma of champion units ("Kennen, Storm of Shuriken" -> Kennen); a
    # legend's tags also name its region or race ("Yordle"), so its champion is the tag that is a champion name.
    champions = {c["name"].split(",")[0].strip().lower() for c in reg.cards
                 if (c.get("card_type") or c.get("type")) == "unit" and (c.get("super_type") or "").lower() == "champion"}
    out = {}
    for c in reg.cards:
        if (c.get("card_type") or c.get("type")) == "legend":
            tags = [t for t in c.get("tags", []) if t]
            pick = next((t for t in tags if t.lower() in champions), tags[0] if tags else None)
            if pick:
                out[c["name"]] = re.sub(r"[^a-z0-9]+", "_", pick.lower()).strip("_")
    return out


def records(b: bytes):
    """Yield (start, end, perspective) of every record of an RBV3/RBV4 file."""
    v4 = b[:4] == b"RBV4"
    pos = 12
    while pos < len(b):
        start = pos
        persp = b[pos + 1]
        nnz = struct.unpack_from("<H", b, pos + 17)[0]
        pos += 19 + 6 * nnz
        n_legal = struct.unpack_from("<H", b, pos + 4)[0]
        pos += 6 + 6 * n_legal + (4 if v4 else 0)
        yield start, pos, persp


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=str(ROOT / "lab/learn/human.rbv3"))
    ap.add_argument("--summary", default=str(ROOT / "lab/learn/human.json"))
    ap.add_argument("--out-dir", default=str(ROOT / "lab/learn/human"))
    ap.add_argument("--min", type=int, default=1, help="write a champion's file only with at least this many decisions")
    a = ap.parse_args(argv)
    b = Path(a.data).read_bytes()
    if b[:4] not in (b"RBV3", b"RBV4"):
        raise SystemExit(f"{a.data}: not RBV3/RBV4")
    games = [g for g in json.loads(Path(a.summary).read_text())["games"] if g.get("rows")]
    champ = champion_of_legend(riftlab.Registry.load())
    recs = list(records(b))
    if sum(g["rows"] for g in games) != len(recs):
        raise SystemExit(f"summary rows {sum(g['rows'] for g in games)} != {len(recs)} records: rebuild both together")
    chunks, seen = collections.defaultdict(list), collections.defaultdict(set)
    i = 0
    for k, g in enumerate(games):
        for s, e, persp in recs[i:i + g["rows"]]:
            name = champ.get((g.get("legends") or {}).get(f"P{persp}", ""), "unknown")
            chunks[name].append(b[s:e])
            seen[name].add(k)
        i += g["rows"]
    out = Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    for name, parts in sorted(chunks.items(), key=lambda kv: -len(kv[1])):
        report[name] = {"decisions": len(parts), "games": len(seen[name])}
        if len(parts) >= a.min and name != "unknown":
            (out / f"human_{name}.rbv3").write_bytes(b[:12] + b"".join(parts))
            # Its complement: every other legend's decisions, so a specialist can weigh its own legend's apart
            # without counting a decision twice (train_policy --human human_X.rbv3 human_others_X.rbv3 --human-weights W w).
            rest = [c for other, ps in chunks.items() if other != name for c in ps]
            (out / f"human_others_{name}.rbv3").write_bytes(b[:12] + b"".join(rest))
    (out / "split.json").write_text(json.dumps(report, indent=1))
    for name, r in report.items():
        print(f"{name:<14} {r['decisions']:>5} decisions  {r['games']:>3} games")


if __name__ == "__main__":
    main(sys.argv[1:])
