#!/usr/bin/env python3
"""Extract Piltover-format decklists from a saved tournament page (Runes and
Rift style: "<place> – <player> – <legend>" followed by "Legend:" ...
"Sideboard:" blocks) into lab/decks/meta/<event>/<place>_<player>.txt.

usage: import_decklists.py PAGE.html EVENT_SLUG SOURCE_URL
"""
import html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def page_text(path: str) -> str:
    t = Path(path).read_text(errors="ignore")
    t = re.sub(r"<script.*?</script>|<style.*?</style>", "", t, flags=re.S)
    t = re.sub(r"<br\s*/?>|</p>|</li>|</h\d>|</tr>|</div>", "\n", t)
    t = html.unescape(re.sub(r"<[^>]+>", " ", t))
    t = t.replace("а", "a").replace("е", "e").replace("о", "o") \
         .replace("с", "c").replace("р", "p").replace("і", "i")
    t = re.sub(r"[ \t]+", " ", t)
    return re.sub(r"\n\s*\n+", "\n", t)


def slug(s: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")


def main(page: str, event: str, url: str) -> None:
    lines = [l.strip() for l in page_text(page).splitlines()]
    out_dir = ROOT / "lab" / "decks" / "meta" / event
    out_dir.mkdir(parents=True, exist_ok=True)
    i = 0
    n = 0
    while i < len(lines):
        m = re.match(r"^(\d+)(?:st|nd|rd|th)\s+[–-]\s+(.+?)\s+[–-]\s+(.+)$", lines[i])
        if not m:
            i += 1
            continue
        place, player, archetype = m.group(1), m.group(2), m.group(3)
        j = i + 1
        while j < len(lines) and lines[j] != "Legend:":
            j += 1
        body = []
        while j < len(lines) and not lines[j].startswith("Copy Decklist"):
            l = lines[j]
            if l == "Rune Pool:":
                l = "Runes:"
            if re.match(r"^(\d+ .+|[A-Za-z ]+:)$", l):
                body.append(l)
            j += 1
        if body:
            text = "\n".join(
                [f"# {place} - {player} - {archetype}", f"# Event: {event}", f"# Source: {url}"]
                + [("\n" + l if l.endswith(":") and k > 0 else l) for k, l in enumerate(body)]
            ) + "\n"
            name = f"{int(place):02d}_{slug(player)}_{slug(archetype)}.txt"
            (out_dir / name).write_text(text)
            n += 1
            print(out_dir.relative_to(ROOT) / name)
        i = j
    print(f"{n} decklists", file=sys.stderr)


if __name__ == "__main__":
    main(*sys.argv[1:4])
