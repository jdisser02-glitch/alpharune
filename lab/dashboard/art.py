#!/usr/bin/env python3
"""Card art for the dashboard, bundled as sprite sheets so the page never loads images from the card CDN
(the claude.ai artifact hosting blocks cmsassets.rgpub.io; the sheets are same-origin files).

1. Every card in the engine's registry with an image_url is downloaded once, as a webp rendition
   (portraits 600 px wide, landscape battlefields 838 px wide), into lab/.cache/cardart/<card id>.webp.
   A card without image_url but with a public code ("RAD-149/167") is tried on a second public source,
   summonersbase (…/img/RAD/RAD-149.webp); the file must be a real image over 5 KB to count.
2. Two sprite sets are packed into lab/dashboard/art/ (committed; dist/ copies it):
     thumb   240x335 cells, 36 per sheet (6x6), webp q80   — chips, portraits, grids, the table
     large   600x838 cells,  9 per sheet (3x3), webp q78   — hover / tap preview, modal, battlefield panels
   Landscape battlefields get their own sheets with transposed cells (thumb_land 335x240, large_land 838x600).
   Cards are packed in card-id order, so a deck's cards (one or two sets) cluster on a few sheets.
3. art/art.json maps each card to its cells:
     {"cell":   {"thumb":[w,h], "large":[w,h], "thumb_land":[w,h], "large_land":[w,h]},
      "sheets": {"thumb":[[file, cols, rows], ...], ...},          # per sheet: file name and its grid
      "cards":  {"<id>": {"t":[sheet, col, row], "l":[sheet, col, row], "land":bool}, ...}}
   A renderer draws cell (col,row) of a sheet with cols x rows cells as
     background-size: cols*100% rows*100%;  background-position: col/(cols-1)*100% row/(rows-1)*100%
   on an element with the cell's aspect ratio (lib.js `spriteStyle`, board.js `sprite`).

Idempotent: a sheet that already exists is kept unless --force is given or the set of cards changed.

usage: art.py [--force] [--workers 3] [--thumb-quality 80] [--large-quality 82]
"""
import argparse
import concurrent.futures as cf
import hashlib
import io
import json
import math
import sys
import time
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "lab"
CACHE = LAB / ".cache" / "cardart"
OUT = LAB / "dashboard" / "art"
sys.path.insert(0, str(LAB))

PORTRAIT_W, LAND_W = 600, 838              # download widths (= the large cells)
ALT_SOURCE = "https://summonersbase.nyc3.cdn.digitaloceanspaces.com/production/img/{set}/{set}-{num:03d}.webp"
ALT_MIN_BYTES = 5_000
SETS = {                                   # name: (cell w, cell h, cols, per sheet, quality option, file prefix)
    "thumb":      (240, 335, 6, 36, "thumb_quality", "thumb"),
    "large":      (600, 838, 3, 9,  "large_quality", "large"),
    "thumb_land": (335, 240, 6, 36, "thumb_quality", "thumbland"),
    "large_land": (838, 600, 3, 9,  "large_quality", "largeland"),
}


def load_cards():
    """Card records from the engine registry: riftlab's cached dump, or the raw cache file when the engine is not built."""
    try:
        import riftlab
        return riftlab.Registry.load().cards
    except SystemExit:
        cache = LAB / ".cache" / "registry.json"
        if not cache.exists():
            sys.exit("no registry: build the engine or run lab/dashboard/build.py once")
        data = json.loads(cache.read_text())
        return data["cards"] if isinstance(data, dict) and "cards" in data else data


def is_landscape(c):
    import re
    return c.get("card_type") == "battlefield" or bool(re.search(r"10[34]\dx744", c.get("image_url") or ""))


def cdn_url(url, w):
    if "cmsassets.rgpub.io" not in url:
        return url
    return url + ("&" if "?" in url else "?") + f"w={w}&fm=webp&q=88"


def alt_url(card):
    """Second source for a card the registry has no image_url for, keyed by its public code; None without one."""
    import re
    m = re.match(r"^([A-Z]+)-(\d+)/\d+$", card.get("public_code") or "")
    return ALT_SOURCE.format(set=m.group(1), num=int(m.group(2))) if m else None


def fetch(card, retries=4):
    """Download one card's art into the cache (skipped when present). Returns (id, path | None, note)."""
    import requests
    cid = card["card_id"]
    dst = CACHE / f"{cid}.webp"
    if dst.exists() and dst.stat().st_size > 0:
        return cid, dst, "cached"
    alt = not card.get("image_url")
    url = alt_url(card) if alt else cdn_url(card["image_url"], LAND_W if is_landscape(card) else PORTRAIT_W)
    if not url:
        return cid, None, "no image_url and no public code"
    err = ""
    for attempt in range(retries):
        try:
            r = requests.get(url, timeout=40, headers={"User-Agent": "riven-lab-art/1.0"})
            if r.status_code == 200 and alt and len(r.content) < ALT_MIN_BYTES:
                return cid, None, f"second source returned {len(r.content)} bytes"
            if r.status_code == 200 and r.content:
                Image.open(io.BytesIO(r.content)).verify()          # a real image, not an error page
                tmp = dst.with_suffix(".part")
                tmp.write_bytes(r.content)
                tmp.replace(dst)
                return cid, dst, "filled from the second source" if alt else "downloaded"
            err = f"HTTP {r.status_code}"
            if r.status_code in (403, 404):
                break
        except Exception as e:  # noqa: BLE001 — network and decode errors alike: retry
            err = str(e)[:80]
        time.sleep(1.5 * (attempt + 1))
    return cid, None, err


def pack(name, items, quality, force):
    """Pack `items` ([(card id, path)], in order) into the sheets of one set. Returns (sheets, cells, bytes written)."""
    cw, ch, cols, per, _, prefix = SETS[name]
    sheets, cells, written = [], {}, 0
    for s in range(math.ceil(len(items) / per)):
        chunk = items[s * per:(s + 1) * per]
        rows = math.ceil(len(chunk) / cols)
        fname = f"{prefix}-{s:02d}.webp"
        sheets.append([fname, cols, rows])
        for i, (cid, _) in enumerate(chunk):
            cells[cid] = [s, i % cols, i // cols]
        out = OUT / fname
        if out.exists() and not force:
            continue
        sheet = Image.new("RGB", (cols * cw, rows * ch), (13, 16, 32))
        for i, (_, path) in enumerate(chunk):
            with Image.open(path) as im:
                im = ImageOps.fit(im.convert("RGB"), (cw, ch), Image.LANCZOS, centering=(0.5, 0.5))
                sheet.paste(im, ((i % cols) * cw, (i // cols) * ch))
        sheet.save(out, "WEBP", quality=quality, method=5)
        written += out.stat().st_size
        print(f"  {fname:<20} {len(chunk):>3} cards  {out.stat().st_size / 1e3:7.0f} KB")
    return sheets, cells, written


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="rebuild every sheet")
    ap.add_argument("--workers", type=int, default=3, help="parallel downloads (be polite to the CDN)")
    ap.add_argument("--thumb-quality", type=int, default=80)
    ap.add_argument("--large-quality", type=int, default=78)
    a = ap.parse_args(argv)
    CACHE.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)

    cards = sorted((c for c in load_cards() if (c.get("image_url") or alt_url(c)) and c.get("card_id") is not None), key=lambda c: int(c["card_id"]))
    no_url = [c for c in cards if not c.get("image_url")]
    print(f"{len(cards)} cards with art ({len(no_url)} without image_url, tried on the second source)")

    # 1. download
    done, failed, filled = {}, [], []
    t0 = time.time()
    with cf.ThreadPoolExecutor(max_workers=max(1, a.workers)) as ex:
        for n, (cid, path, note) in enumerate(ex.map(fetch, cards), 1):
            if path:
                done[cid] = path
                if note.startswith("filled") or (note == "cached" and not cards[n - 1].get("image_url")):
                    filled.append(cid)
            else:
                failed.append((cid, note))
            if n % 100 == 0 or n == len(cards):
                print(f"  {n}/{len(cards)} fetched ({len(failed)} failed, {time.time() - t0:.0f} s)")
    names = {c["card_id"]: c["name"] for c in cards}
    for cid in filled:
        print(f"  second source filled card {cid} ({names[cid]})")
    for cid, note in failed:
        print(f"  no art for card {cid} ({names[cid]}): {note}")

    # 2. pack (card-id order; landscape cards in their own sheets)
    by_id = {c["card_id"]: c for c in cards}
    portrait = [(cid, p) for cid, p in sorted(done.items()) if not is_landscape(by_id[cid])]
    land = [(cid, p) for cid, p in sorted(done.items()) if is_landscape(by_id[cid])]
    manifest = hashlib.sha1(json.dumps([cid for cid, _ in portrait + land]).encode()).hexdigest()[:12]
    prev = OUT / "art.json"
    force = a.force
    if prev.exists() and not force:
        try:
            if json.loads(prev.read_text()).get("manifest") != manifest:
                print("  card set changed since the last build: rebuilding every sheet")
                force = True
        except json.JSONDecodeError:
            force = True
    if force:
        for f in OUT.glob("*.webp"):
            f.unlink()

    art = {"cell": {k: [v[0], v[1]] for k, v in SETS.items()}, "sheets": {}, "cards": {}, "manifest": manifest}
    entries = {}
    for name, items in (("thumb", portrait), ("large", portrait), ("thumb_land", land), ("large_land", land)):
        quality = getattr(a, SETS[name][4])
        sheets, cells, _ = pack(name, items, quality, force)
        art["sheets"][name] = sheets
        key = "t" if name.startswith("thumb") else "l"
        for cid, cell in cells.items():
            entries.setdefault(cid, {"land": is_landscape(by_id[cid])})[key] = cell
    art["cards"] = {str(cid): e for cid, e in sorted(entries.items())}
    (OUT / "art.json").write_text(json.dumps(art, separators=(",", ":")))

    files = sorted(OUT.glob("*.webp"))
    total = sum(f.stat().st_size for f in files)
    for name in SETS:
        fs = [f for f in files if f.name.startswith(SETS[name][5] + "-")]
        if fs:
            print(f"  {name:<11} {len(fs):>3} sheets  {sum(f.stat().st_size for f in fs) / 1e6:6.1f} MB")
    print(f"art/: {len(files)} sheets + art.json ({(OUT / 'art.json').stat().st_size / 1e3:.0f} KB), {total / 1e6:.1f} MB, "
          f"{len(entries)} cards mapped, {len(failed)} without art")


if __name__ == "__main__":
    main(sys.argv[1:])
