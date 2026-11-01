#!/usr/bin/env python3
"""import_list — turn a decklist pasted from a deck site into a lab deck file.

Reads the list on stdin (sections Legend: / Champion: / MainDeck: / Battlefields: / Rune Pool: or Runes: /
Sideboard:, lines "N Card Name"), fixes what deck sites write differently from the engine (a legend written
"Champion, Title" becomes the engine's "Title"; "Rune Pool" becomes "Runes"), checks every name against the
engine's card table (lab/dashboard/dist/data.json, else `riftbound --dump-registry`), and writes the file.

usage: import_list.py OUT.txt --title "Evelynn - ..." [--event EVENT] [--source TEXT] < list.txt
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SECTIONS = {"legend": "Legend", "champion": "Champion", "maindeck": "MainDeck", "main deck": "MainDeck", "main": "MainDeck",
            "battlefields": "Battlefields", "rune pool": "Runes", "runes": "Runes", "sideboard": "Sideboard"}


def card_names() -> set[str]:
    data = ROOT / "lab" / "dashboard" / "dist" / "data.json"
    cards = json.loads(data.read_text())["cards"]
    return {c["name"] for c in (cards.values() if isinstance(cards, dict) else cards) if c.get("name")}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--title", required=True)
    ap.add_argument("--event", default="")
    ap.add_argument("--source", default="pasted in the session (copied from riftdecks.com)")
    a = ap.parse_args()
    names = card_names()
    out: dict[str, list[tuple[int, str]]] = {}
    sec = None
    problems = []
    for raw in sys.stdin.read().splitlines():
        line = raw.strip()
        if not line:
            continue
        key = line.rstrip(":").strip().lower()
        if line.endswith(":") and key in SECTIONS:
            sec = SECTIONS[key]; out.setdefault(sec, []); continue
        m = re.match(r"^(\d+)\s*x?\s+(.+)$", line)
        if not m or not sec:
            problems.append(f"unread line: {line!r}"); continue
        n, name = int(m.group(1)), m.group(2).strip().replace("’", "'")
        if name not in names:
            alt = [name.split(", ", 1)[1]] if ", " in name else []
            alt += [x for x in names if x.lower() == name.lower()]
            hit = next((x for x in alt if x in names), None)
            if hit:
                name = hit
            elif sec != "Sideboard":
                problems.append(f"unknown card in {sec}: {name}")
        out[sec].append((n, name))
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    head = [f"# {a.title}"] + ([f"# Event: {a.event}"] if a.event else []) + [f"# Source: {a.source}"]
    body = []
    for s in ("Legend", "Champion", "MainDeck", "Battlefields", "Runes", "Sideboard"):
        if s in out:
            body += [f"{s}:"] + [f"{n} {x}" for n, x in out[s]] + [""]
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text("\n".join(head + body).rstrip() + "\n")
    total = sum(n for n, _ in out.get("MainDeck", []))
    print(f"wrote {a.out}: legend {out.get('Legend')}, champion {out.get('Champion')}, main {total} cards, "
          f"battlefields {len(out.get('Battlefields', []))}, runes {sum(n for n, _ in out.get('Runes', []))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
