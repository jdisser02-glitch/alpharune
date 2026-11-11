#!/usr/bin/env python3
"""Compare every card's `d.domains` with the domain colour printed on its image.

The official gallery metadata (cards/raw/gallery_raw.json) carries wrong domains
for a number of Radiance cards, and the card classes were authored from it. The
card image itself is the authority: the domain symbol in the bottom-right corner
(one per domain, two on signature cards) and the name banner both take the
domain's colour (Fury red, Body orange, Order yellow, Calm green, Mind blue,
Chaos purple).

For every unit, spell and gear card under src/cards/ this script downloads the
image named by `d.image_url` (falling back to the gallery record), samples the
corner symbol(s) and the banner, and classifies the hue.

  ok        the symbol colour(s) equal the declared domains
  mismatch  the symbol and the banner agree on a colour the card doesn't declare
  review    the reading is ambiguous (alt-art frame, BANNED stamp, ...): look at it
  no-image  no image URL in the card class or the gallery

Run from the repo root (needs Pillow and numpy):

  python3 scripts/check_card_domains.py [--cache DIR] [--csv OUT.csv]

Exit status is 1 when a mismatch is found. Runes, battlefields and legends are
not checked (runes and legends print their domains differently, battlefields
have none).
"""

import argparse
import csv
import json
import re
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent

# Hue windows in degrees, measured on the 2025-2026 card frames.
HUES = {
    "Fury": [(338, 360), (0, 10)],
    "Body": [(12, 36)],
    "Order": [(38, 66)],
    "Calm": [(80, 165)],
    "Mind": [(185, 228)],
    "Chaos": [(245, 300)],
}
# On the 744x1039 render: the domain symbol is a disc of radius ~12 centred at
# (692, 1000); a second domain's symbol sits 34 px to its left. The glyph inside
# the disc is not coloured, so about half of the disc carries the hue.
SYMBOL_Y, SYMBOL_X, SECOND_SYMBOL_X, SYMBOL_R = 1000, 692, 658, 11
SYMBOL_MIN = 0.40
BANNER = (598, 645, 70, 670)   # y0, y1, x0, x1


def parse_cards():
    cards = []
    for path in sorted((REPO / "src" / "cards").glob("*/*.cpp")):
        src = path.read_text()

        def field(pattern):
            m = re.search(pattern, src)
            return m.group(1) if m else None

        card_id = field(r"d\.id\s*=\s*(\d+)")
        card_type = field(r"d\.card_type\s*=\s*CardType::(\w+)")
        if card_id is None or card_type not in ("Unit", "Spell", "Gear"):
            continue
        decl = re.search(r"d\.domains\s*=\s*\{([^}]*)\}", src)
        cards.append({
            "id": int(card_id),
            "file": str(path.relative_to(REPO)),
            "def_id": field(r'd\.def_id\s*=\s*R"RB\((.*?)\)RB"'),
            "public_code": field(r'd\.public_code\s*=\s*R"RB\((.*?)\)RB"') or "",
            "name": field(r'd\.name\s*=\s*R"RB\((.*?)\)RB"') or "",
            "card_type": card_type,
            "domains": sorted(re.findall(r"Domain::(\w+)", decl.group(1))) if decl else [],
            "url": field(r'd\.image_url\s*=\s*R"RB\((.*?)\)RB"'),
        })
    return cards


def add_gallery_urls(cards):
    gallery_path = REPO / "cards" / "raw" / "gallery_raw.json"
    if not gallery_path.exists():
        return
    by_id = {g["id"]: g for g in json.loads(gallery_path.read_text())}
    for c in cards:
        g = by_id.get(c["def_id"])
        if not c["url"] and g:
            c["url"] = g["cardImage"]["url"]


def download(card, cache):
    target = cache / f"{card['id']}.png"
    if target.exists() and target.stat().st_size > 0:
        return
    with urllib.request.urlopen(card["url"].split("?")[0], timeout=60) as r:
        target.write_bytes(r.read())


def hue_shares(pixels):
    """Share of `pixels` (N x 3, 0..255) falling in each domain's hue window."""
    a = pixels.reshape(-1, 3).astype(float) / 255
    total = len(a)
    mx, mn = a.max(1), a.min(1)
    delta = mx - mn
    saturated = (mx > 0.35) & (delta > 0.45 * np.maximum(mx, 1e-9))
    a, mx, delta = a[saturated], mx[saturated], delta[saturated]
    r, g, b = a[:, 0], a[:, 1], a[:, 2]
    hue = np.where(mx == r, ((g - b) / delta) % 6,
                   np.where(mx == g, (b - r) / delta + 2, (r - g) / delta + 4)) * 60
    return {name: sum(((hue >= lo) & (hue < hi)).sum() for lo, hi in windows) / total
            for name, windows in HUES.items()}


_yy, _xx = np.mgrid[-SYMBOL_R:SYMBOL_R + 1, -SYMBOL_R:SYMBOL_R + 1]
_DISC = (_yy ** 2 + _xx ** 2) <= SYMBOL_R ** 2


def symbol_at(image, cx):
    """(domain, share) of the best single-hue disc within a few px of (cx, SYMBOL_Y)."""
    best = (None, 0.0)
    for dx in range(-4, 5, 2):
        for dy in range(-4, 5, 2):
            x, y = cx + dx, SYMBOL_Y + dy
            patch = image[y - SYMBOL_R:y + SYMBOL_R + 1, x - SYMBOL_R:x + SYMBOL_R + 1][_DISC]
            shares = hue_shares(patch)
            name = max(shares, key=shares.get)
            if shares[name] > best[1]:
                best = (name, shares[name])
    return best


def classify(card, cache):
    image = np.asarray(Image.open(cache / f"{card['id']}.png").convert("RGB"))
    right, right_share = symbol_at(image, SYMBOL_X)
    left, left_share = symbol_at(image, SECOND_SYMBOL_X)
    y0, y1, x0, x1 = BANNER
    banner_shares = hue_shares(image[y0:y1, x0:x1])
    banner = max(banner_shares, key=banner_shares.get)
    if banner_shares[banner] < 0.10:
        banner = None

    declared = card["domains"]
    seen = [right] if right_share >= SYMBOL_MIN else []
    if len(declared) == 2 and left_share >= SYMBOL_MIN:
        seen.append(left)
    seen = sorted(set(seen))
    card["symbols"], card["banner"] = seen, banner

    if seen == declared:
        return "ok"
    # A single clear symbol backed by the banner: the card prints another domain.
    if len(seen) == 1 and len(declared) <= 1 and banner == seen[0]:
        return "mismatch"
    return "review"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cache", default=str(REPO / "build" / "card_images"),
                    help="directory the card images are downloaded to (default: build/card_images)")
    ap.add_argument("--csv", help="write one row per card checked to this file")
    ap.add_argument("--jobs", type=int, default=16, help="parallel downloads")
    args = ap.parse_args()

    cache = Path(args.cache)
    cache.mkdir(parents=True, exist_ok=True)
    cards = parse_cards()
    add_gallery_urls(cards)

    with_image = [c for c in cards if c["url"]]
    with ThreadPoolExecutor(args.jobs) as pool:
        list(pool.map(lambda c: download(c, cache), with_image))

    for c in cards:
        c["symbols"], c["banner"] = [], None
        c["verdict"] = classify(c, cache) if c["url"] else "no-image"

    cards.sort(key=lambda c: (c["public_code"] or "~", c["id"]))
    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["id", "public_code", "name", "card_type", "declared", "symbols", "banner", "verdict"])
            for c in cards:
                w.writerow([c["id"], c["public_code"], c["name"], c["card_type"],
                            "/".join(c["domains"]) or "-", "/".join(c["symbols"]) or "-",
                            c["banner"] or "-", c["verdict"]])

    counts = {}
    for c in cards:
        counts[c["verdict"]] = counts.get(c["verdict"], 0) + 1
    print(f"{len(cards)} unit/spell/gear cards: " +
          ", ".join(f"{n} {v}" for v, n in sorted(counts.items())))
    for verdict in ("mismatch", "review", "no-image"):
        for c in cards:
            if c["verdict"] != verdict:
                continue
            print(f"  {verdict:8} {c['id']:5d} {c['public_code']:12} {c['name'][:34]:34} "
                  f"declared {'/'.join(c['domains']) or '-':12} "
                  f"symbols {'/'.join(c['symbols']) or '-':12} banner {c['banner'] or '-'}")
    return 1 if counts.get("mismatch") else 0


if __name__ == "__main__":
    sys.exit(main())
