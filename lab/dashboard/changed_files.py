#!/usr/bin/env python3
"""changed_files — the dist/ files that differ from what was last published, so a republish sends all of them.

8 Nov 2026: for four days only data.json and a few app files were republished; the browser engine and the bot's
nets stayed at their 4 Nov versions, and Jeremy kept playing a stale bot. Run after build.py:

    python3 lab/dashboard/changed_files.py            # prints a JSON map {published path: dist path} of changed files
    python3 lab/dashboard/changed_files.py --mark     # after a successful publish: record dist/ as published

The record lives in lab/dashboard/published.json (sha256 per published path). index.html is the page itself and is
always sent by the publish, so it is left out. Publishes are limited to 64 MB, so the map is split into batches."""
import argparse, hashlib, json, sys
from pathlib import Path

DIST = Path(__file__).resolve().parent / "dist"
REC = Path(__file__).resolve().parent / "published.json"
SKIP_DIRS = {"art"}      # sprite sheets change only when art.py runs; publish them on purpose


def digest(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--mark", action="store_true")
    ap.add_argument("--batch-mb", type=float, default=60)
    a = ap.parse_args(argv)
    files = {str(p.relative_to(DIST)): p for p in DIST.rglob("*")
             if p.is_file() and p.name != "index.html" and p.relative_to(DIST).parts[0] not in SKIP_DIRS}
    now = {k: digest(p) for k, p in files.items()}
    if a.mark:
        REC.write_text(json.dumps(now, indent=0, sort_keys=True))
        print(f"recorded {len(now)} files as published")
        return
    old = json.loads(REC.read_text()) if REC.exists() else {}
    changed = sorted(k for k, h in now.items() if old.get(k) != h)
    batches, cur, size = [], {}, 0
    for k in changed:
        s = files[k].stat().st_size
        if cur and size + s > a.batch_mb * 1e6:
            batches.append(cur); cur, size = {}, 0
        cur[k] = k; size += s
    if cur:
        batches.append(cur)
    print(json.dumps({"changed": len(changed), "batches": batches}, indent=1))


if __name__ == "__main__":
    sys.exit(main())
