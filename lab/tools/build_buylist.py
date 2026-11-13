#!/usr/bin/env python3
"""Rebuilds the page of the "Riftbound Buy List" artifact (https://claude.ai/artifact/18y7hmz2sjy9Cd1Ls2Tmzy).

    python3 lab/tools/build_buylist.py OUT.html

One section per list of DECKS below (legend, champion, main deck, battlefields, runes, sideboard), each card with
its printed code (base printing, from cards/raw/gallery_raw.json), a thumbnail cut from the dashboard's art sheets
and a "missing" counter; the page exports the missing cards as CSV. Cards of lab/meta/banned_2026-11-23.json are
left out of every list. Counts are stored in the browser per list, section and card code
(lab/tools/buylist_old_rows.json maps the rows of the page of 8 Nov so its counts carry over).
To publish: Artifact tool, `url` above, `file_path` OUT.html (read the artifact first).
"""
import base64
import html
import io
import json
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lab"))
import riftlab  # noqa: E402

# (slug, legend chip, title, list file, note, sideboard[, extra sections between the runes and the sideboard]).
# A slug that was on the earlier page keeps its counts.
DECKS = [
    ("volibear", "Volibear (Ritualo)", "Volibear Dragons: Ritualo's list and its improved version", "lab/decks/community/deck_profiles_2026-11-07/ritualo_volibear_dragons.txt",
     "Ritualo's list as registered (40 cards with the champion), then the improved version: 2 Rengar, Trophy Hunter "
     "replace 2 Show of Strength (measured +4.3 points on 3,602 simulated games). This is the list with the most "
     "turn-2 plays (3 Dragonsoul Sage, 3 Mobilize). Sideboard: Ritualo's without the banned Dazzling Aurora; its 3 "
     "free slots are filled with 2 Repulse (against spells that return your dragons to hand) and 1 Decree of Strength "
     "(against Mind decks such as Kai'Sa), both from soldog's sideboard. Sideboards are not simulated.",
     "2 Thermo Beam|2 Minotaur Reckoner|2 Rampage|1 Sabotage|2 Repulse|1 Decree of Strength",
     [("Improved list (in for 2 Show of Strength)", "2 Rengar, Trophy Hunter")]),
    ("volibear-best", "Volibear (other build)", "Volibear Dragons: the other build (soldog's deck, adapted)", "lab/variants/volibear-noaurora-best.txt",
     "A different 40 cards, only if you want to build it too. Measured 82.5%, 1.3 points above the improved Ritualo "
     "list, a gap the simulation cannot tell from zero; it has very few turn-2 plays (3 Mobilize), which the bot "
     "punishes less than a player would. Sideboard: soldog's own (not simulated).",
     "1 Assembly Rig|1 Deadbloom Predator|1 Decree of Strength|1 Ferrous Forerunner|1 Punch First|2 Repulse|2 Ruin Runner|1 Sabotage"),
    ("evelynn", "Evelynn", "Evelynn: your list of 13 Nov", "lab/decks/community/jeremy_2026-11-13/evelynn_in_control.txt",
     "No banned card. Not measured: the list as you sent it (champion Evelynn, Entrancing assumed), with its sideboard.",
     "2 Gust|1 Abandon|1 Switcheroo|2 Ravenbloom Prefect|1 Trinity Force|1 Vex, Apathetic|2 Graves, Blasting Through"),
    ("mordekaiser", "Mordekaiser", "Mordekaiser (Neal, Ka Zhi Yu)", "lab/decks/meta/ka_zhi_yu_tournoi_bo3_2026-10-24/04_neal_mordekaiser.txt",
     "No banned card. Not optimised: tournament list with its sideboard.",
     "2 Noxus Saboteur|1 Forge of the Future|1 Salvage|1 Against the Odds|1 Ashe, Focused|1 Vi, Peacekeeper|1 Brittle Steel|1 Decree of Rage|1 Decree of Unity"),    ("riven", "Riven", "Riven: the cards that stay legal", "decks/riven_best_field.txt",
     "Your list of 6 Nov without its banned cards: the champion Riven, Broken Blade, 3 Zhonya's Hourglass, 1 Stellacorn "
     "Herder, and the battlefields Emperor's Dais and Star Spring are gone. Still to choose: a champion (Riven, "
     "Shattered or Riven, Deserter), 4 main-deck cards and 2 battlefields; the lab is measuring them, and this "
     "section will then show the full list. Sideboard: the Sacramento list's (not simulated).",
     "2 Decree of Focus|1 Hush|2 An Offer You Can't Refuse|1 Not So Fast|1 Rebuke|1 Star-Crossed|1 Tomb-Raider Barbara|1 Baron Nashor"),
]
SUB = ("Volibear, Evelynn, Mordekaiser, then Riven, each with its sideboard; updated 13 Nov 2026 after the ban "
       "announcement. In the Missing column, enter how many copies of the card you do not own (0 and max set none or the "
       "list's count; + goes beyond it, up to 9, if you want spare copies), then export the CSV for the buying agent.")


def main():
    out = Path(sys.argv[1])
    reg = riftlab.Registry.load().cards
    ban = json.loads((ROOT / "lab/meta/banned_2026-11-23.json").read_text())
    banned = set(ban["cards"]) | set(ban["battlefields"])
    art = json.loads((ROOT / "lab/dashboard/art/art.json").read_text())
    cell, sheets = art["cell"], art["sheets"]
    byname = {}
    for c in sorted(reg, key=lambda c: c["card_id"]):
        byname.setdefault(c["name"], c)
    gal = json.loads((ROOT / "cards/raw/gallery_raw.json").read_text())
    gal = gal if isinstance(gal, list) else next(v for v in gal.values() if isinstance(v, list))
    base = {}
    for g in gal:
        m = re.fullmatch(r"([a-z]+)-(\d+)-(\d+)", g["id"])
        if not m or int(m[2]) > int(m[3]):
            continue      # alt arts, showcase and overnumbered prints
        cur = base.get(g["name"])
        if cur is None or int(m[2]) < cur[0]:
            base[g["name"]] = (int(m[2]), f"{m[1].upper()}-{m[2]}/{m[3]}", g["rarity"]["value"]["label"].lower())
    cache = {}

    def sheet(kind, i):
        if (kind, i) not in cache:
            cache[(kind, i)] = Image.open(ROOT / "lab/dashboard/art" / sheets[kind][i][0]).convert("RGB")
        return cache[(kind, i)]

    def thumb(card_id):
        a = art["cards"].get(str(card_id))
        if not a:
            return ""
        kind = "thumb_land" if a["land"] else "thumb"
        si, col, row = a["t"]
        w, h = cell[kind]
        im = sheet(kind, si).crop((col * w, row * h, col * w + w, row * h + h))
        im = im.resize((w * 96 // max(w, h), h * 96 // max(w, h)), Image.LANCZOS)
        b = io.BytesIO(); im.save(b, "WEBP", quality=72)
        return "data:image/webp;base64," + base64.b64encode(b.getvalue()).decode()

    data = []
    for slug, legend, title, path, note, side, *more in DECKS:
        sec, rows, dropped = None, [], []
        lines = (ROOT / path).read_text().splitlines()
        if "Sideboard:" in lines:               # the list file's own sideboard gives way to the one named above
            lines = lines[:lines.index("Sideboard:")]
        for name, cards in (more[0] if more else []):
            lines += [name + ":"] + cards.split("|")
        for line in lines + ["Sideboard:"] + side.split("|"):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.endswith(":"):
                sec = line[:-1]; continue
            m = re.match(r"(\d+)\s+(.+)", line)
            n, name = int(m[1]), m[2].strip()
            c = byname.get(name) or (byname.get(name.split(", ", 1)[1]) if sec == "Legend" and ", " in name else None)
            if not c:
                raise SystemExit(f"unknown card {name!r} in {path}")
            b = base.get(c["name"])
            if b:
                c = dict(c, collector_number=b[0], public_code=b[1], rarity=b[2])
            row = {"section": {"MainDeck": "Main deck"}.get(sec, sec), "qty": n, "name": c["name"], "code": c["public_code"],
                   "number": c["collector_number"], "set": c["set_name"], "rarity": c["rarity"], "img": thumb(c["card_id"]),
                   "id": c["card_id"]}
            if c["name"] in banned:          # a banned card is not on a buy list
                dropped.append(f"{n} {c['name']}")
                continue
            rows.append(row)
        copies = {}
        for r in rows:
            if r["section"] not in ("Legend", "Battlefields", "Runes"):
                copies[r["name"]] = copies.get(r["name"], 0) + r["qty"]
        over = {k: v for k, v in copies.items() if v > 3}
        if over or sum(r["qty"] for r in rows if r["section"] == "Sideboard") > 10:
            raise SystemExit(f"{slug}: more than three copies or more than ten sideboard cards: {over}")
        if dropped:
            print(f"{slug}: removed {', '.join(dropped)}")
        data.append({"slug": slug, "legend": legend, "title": title, "note": note, "source": path, "cards": rows})
    t = (ROOT / "lab/tools/buylist_template.html").read_text()
    for a, b in (("__DATA__", json.dumps(data).replace("</", "<\\/")),
                 ("__OLD__", json.dumps(json.loads((ROOT / "lab/tools/buylist_old_rows.json").read_text()))),
                 ("__SUB__", html.escape(SUB)), ("__BANURL__", html.escape(ban["source"])),
                 ("__BANLIST__", html.escape(", ".join(ban["cards"]) + "; battlefields " + " and ".join(ban["battlefields"])))):
        assert a in t, a
        t = t.replace(a, b)
    out.write_text(t)
    print(out, len(t) // 1024, "KB;", ", ".join(f"{d['slug']} {sum(c['qty'] for c in d['cards'])} cards" for d in data),
          "; banned rows:", sum(1 for d in data for c in d["cards"] if c.get("banned")))


if __name__ == "__main__":
    main()
