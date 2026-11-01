#!/usr/bin/env python3
"""import_markdown — import every decklist of a "tournament winners" markdown export (one "## Dnn - Legend" block
per list, with **Date :**, **Pilote :**, **Classement publié :** and fenced card blocks under #### Légende /
Champion élu / Main deck / Runes / Champs de bataille / Réserve) into lab/decks/meta/<event>_<date>/NN_<pilot>_<legend>.txt
through lab/tools/import_list.py. Lists already in lab/decks/meta with the same cards are skipped.

usage: import_markdown.py FILE.md [--dry-run]
"""
from __future__ import annotations

import re
import subprocess
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lab"))
import riftlab  # noqa: E402

SECTION = {"légende": "Legend", "champion élu": "Champion", "main deck": "MainDeck", "runes": "Runes",
           "champs de bataille": "Battlefields", "réserve": "Sideboard"}


def slug(s: str) -> str:
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_") or "x"


def main_key(path: Path):
    try:
        d = riftlab.parse_deck(path)
    except Exception:  # noqa: BLE001
        return None
    g = d if isinstance(d, dict) else vars(d)
    return tuple(tuple(sorted((g.get(k) or {}).items())) for k in ("legend", "champion", "maindeck", "battlefields", "runes"))


def main() -> int:
    md = Path(sys.argv[1]).read_text()
    dry = "--dry-run" in sys.argv
    existing = {main_key(p): p for p in (ROOT / "lab" / "decks" / "meta").rglob("*.txt")}
    existing |= {main_key(ROOT / "decks" / "riven_sacramento.txt"): ROOT / "decks" / "riven_sacramento.txt"}
    blocks = re.split(r"\n(?=## D\d+ - )", md)
    for b in blocks:
        m = re.match(r"## (D\d+) - (.+)\n### (.+)", b)
        if not m:
            continue
        did, legend, event = m.group(1), m.group(2).strip(), m.group(3).strip()
        date = re.search(r"\*\*Date :\*\*\s*(\d\d)/(\d\d)/(\d{4})", b)
        pilot = (re.search(r"\*\*Pilote :\*\*\s*(.+?)\s*$", b, re.M) or [None, "?"])[1]
        rank = re.search(r"\*\*Classement publié :\*\*\s*(\d+)\D+?(\d+) participants", b)
        source = (re.search(r"\*\*Source de la composition :\*\*\s*(\S+)", b) or [None, ""])[1]
        iso = f"{date.group(3)}-{date.group(2)}-{date.group(1)}" if date else "unknown"
        place, players = (int(rank.group(1)), int(rank.group(2))) if rank else (1, 0)
        text = []
        for sec, body in re.findall(r"#### ([^\n(]+?)\s*(?:\([^)]*\))?\n```text\n(.*?)```", b, re.S):
            key = next((v for k, v in SECTION.items() if sec.lower().startswith(k)), None)
            if key:
                text += [f"{key}:", body.strip(), ""]
        champ = legend.split(",")[0].strip()
        folder = ROOT / "lab" / "decks" / "meta" / f"{slug(event)[:48]}_{iso}"
        out = folder / f"{place:02d}_{slug(pilot)[:24]}_{slug(champ)}.txt"
        title = f"{place} - {pilot} - {champ}"
        cmd = [sys.executable, str(ROOT / "lab" / "tools" / "import_list.py"), str(out), "--title", title,
               "--event", f"{slug(event)[:48]}_{iso}",
               "--source", f"{event}, {players} players, {iso}, place {place} ({source}; winners export {Path(sys.argv[1]).name})"]
        if dry:
            print(did, out.relative_to(ROOT)); continue
        r = subprocess.run(cmd, input="\n".join(text), capture_output=True, text=True)
        if r.returncode:
            print(f"{did} {champ} ({event}): NOT IMPORTED\n  " + r.stderr.strip().replace("\n", "\n  "))
            continue
        k = main_key(out)
        if k in existing and existing[k] != out:
            print(f"{did} {champ} ({event}): same cards as {existing[k].relative_to(ROOT)}, kept as a separate event entry")
        else:
            print(f"{did} {champ} ({event}): imported -> {out.relative_to(ROOT)}")
        existing.setdefault(k, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
