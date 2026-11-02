#!/usr/bin/env python3
"""audit_run — run the bot audit (lab/tools/audit_bot.js) on the user's new
games and on a few bot-vs-bot games of the jury matchups, and keep a summary.

  python3 lab/tools/audit_run.py --games DIR          # every game record in DIR not audited yet
  python3 lab/tools/audit_run.py --selfplay 4         # 4 bot-vs-bot games over the gate panel matchups

Reports go to lab/audits/games/<game id>.json and lab/audits/selfplay/<stamp>-<k>.json;
lab/audits/summary.json lists every report with its flag counts and the share
of the bot's decisions whose evaluation was saturated, plus totals per kind of
flag per 100 audited bot decisions (the trend to watch from net to net).
Needs the built dashboard (lab/dashboard/build.py) for the engine and nets.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AUD = ROOT / "lab" / "audits"
sys.path.insert(0, str(ROOT / "lab" / "learn"))
import loop  # noqa: E402  (the gate panel: the jury's matchups)

JS = ROOT / "lab" / "tools" / "audit_bot.js"


def run(args: list[str], out: Path) -> dict | None:
    out.parent.mkdir(parents=True, exist_ok=True)
    p = subprocess.run(["node", str(JS), *args, "--out", str(out)], cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        print(f"audit failed: {' '.join(args)}\n{p.stderr[-800:]}", file=sys.stderr)
        return None
    print(p.stdout.strip(), flush=True)
    return json.loads(out.read_text())


def slim(rep: dict, kind: str, path: Path) -> dict:
    bot = [p for p in rep.get("probes", []) if p.get("bot")]
    return {"kind": kind, "report": str(path.relative_to(ROOT)), "game": rep.get("game"), "me": rep.get("me"),
            "opponent": rep.get("opponent"), "net": rep.get("net"), "bot_decisions": len(bot),
            "flags": rep["summary"]["counts"], "saturated_share": rep["summary"]["saturated_share"],
            "lines": rep["summary"]["lines"][:6],
            "audited_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")}


def write_summary(new: list[dict]) -> dict:
    path = AUD / "summary.json"
    s = json.loads(path.read_text()) if path.exists() else {"reports": []}
    seen = {r["report"] for r in s["reports"]}
    s["reports"] += [r for r in new if r["report"] not in seen]
    by_net: dict[str, dict] = {}
    for r in s["reports"]:
        t = by_net.setdefault(r.get("net") or "?", {"bot_decisions": 0, "flags": {}, "reports": 0})
        t["reports"] += 1
        t["bot_decisions"] += r["bot_decisions"]
        for k, v in r["flags"].items():
            t["flags"][k] = t["flags"].get(k, 0) + v
    for t in by_net.values():
        t["per_100_decisions"] = {k: round(100 * v / max(1, t["bot_decisions"]), 1) for k, v in t["flags"].items()}
    s["by_net"] = by_net
    s["updated_utc"] = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(s, indent=1))
    return s


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--games", help="a folder of game records (the Play page's 'games' rows)")
    ap.add_argument("--selfplay", type=int, default=0, help="bot-vs-bot games over the panel matchups")
    ap.add_argument("--spec", default="search:sims=50,worlds=8,settle=60", help="bot for the bot-vs-bot games (training strength)")
    ap.add_argument("--seed", type=int, default=0, help="first seed of the bot-vs-bot games (default: from the date)")
    a = ap.parse_args(argv)
    new = []
    if a.games:
        for f in sorted(Path(a.games).glob("*.json")):
            rec = json.loads(f.read_text())
            rec = rec.get("data", rec)
            if not rec.get("finished") or rec.get("in_progress"):
                continue
            out = AUD / "games" / f"{rec.get('id', f.stem)}.json"
            if out.exists():
                continue
            rep = run(["--game", str(f)], out)
            if rep:
                new.append(slim(rep, "game", out))
    if a.selfplay:
        stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M")
        seed = a.seed or int(dt.datetime.now(dt.timezone.utc).strftime("%y%m%d%H")) % 100000 * 10
        for k in range(a.selfplay):
            name, d_a, d_b = loop.PANEL[k % len(loop.PANEL)]
            if k // len(loop.PANEL) % 2:
                d_a, d_b = d_b, d_a
            out = AUD / "selfplay" / f"{stamp}-{k}-{name}.json"
            rep = run(["--selfplay", d_a, d_b, "--seed", str(seed + k), "--spec", a.spec], out)
            if rep:
                new.append(slim(rep, "selfplay", out))
    s = write_summary(new)
    for net, t in s["by_net"].items():
        print(f"{net}: {t['reports']} audits, {t['bot_decisions']} bot decisions, per 100: {t['per_100_decisions']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
