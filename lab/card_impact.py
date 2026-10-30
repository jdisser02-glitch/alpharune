#!/usr/bin/env python3
"""card_impact — how much each card of a list moves its win rate, from simulated games.

Plays the list against a jury (both seats, every game recorded) and, for every
card of the list, compares the games where it reached its pilot's hand (opening
hand or drawn, after the mulligan) with the games where it never did:

  GIH   win rate in Games In Hand (the card was in hand at some point)
  GNS   win rate in Games Not Seen
  IWD   GIH - GNS, the "improvement when drawn", with a 95% interval
  played  share of the games in hand where it was also played

Read IWD as a ranking signal, not a verdict: a card drawn in long games looks
better than it is, a card whose job is to stabilise a lost board worse. The
point is to rank the cards worth questioning and the pool cards worth trying
in a list search (lab/ga.py), on the same engine and bot as the search itself.

usage: card_impact.py SPEC.json [--games-dir DIR]

spec = {
  "name": "riven-card-impact",
  "deck": "decks/riven_sacramento.txt",
  "agent": "pmcts:sims=30,eval=policy:lab/learn/nets/net_027.rbpn,worlds=6",
  "opponents": [{"deck": "...", "label": "...", "weight": 1}, ...],
  "games": 40,               # per opponent, half per seat
  "threads": 4, "seed": 7
}
Writes lab/results/<id>.json (kind "card_impact") and prints the table.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures as cf
import datetime as dt
import json
import math
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import riftlab  # noqa: E402
from riftlab import LAB, ROOT  # noqa: E402

LINE = re.compile(r"^\[D:[\d.]+\]\s*")


def wilson_diff(w1, n1, w2, n2, z=1.96):
    """Difference of two proportions with a normal-approximation 95% interval."""
    if not n1 or not n2:
        return None, None, None
    p1, p2 = w1 / n1, w2 / n2
    se = math.sqrt(p1 * (1 - p1) / n1 + p2 * (1 - p2) / n2)
    d = p1 - p2
    return d, d - z * se, d + z * se


def game_facts(path: Path, seat: int) -> dict | None:
    """Cards that reached `seat`'s hand and cards it played, and whether it won."""
    try:
        g = json.loads(path.read_text())
    except Exception:  # noqa: BLE001
        return None
    drawer = None
    hand: set[str] = set()
    set_aside: collections.Counter = collections.Counter()
    played: set[str] = set()
    for fr in g.get("frames", []):
        for raw in fr.get("log", []):
            t = LINE.sub("", str(raw)).strip()
            m = re.match(r"(?:EFFECT|DRAW_HAND|DRAW PHASE): P([12]) draws", t)
            if m:
                drawer = int(m.group(1)); continue
            m = re.match(r"DREW: (.+?) \(id=\d+\)", t)
            if m and drawer == seat:
                hand.add(m.group(1)); continue
            m = re.match(r"MULLIGAN: P([12]) set aside (.+?) \(id=\d+\)", t)
            if m and int(m.group(1)) == seat:
                set_aside[m.group(2)] += 1; continue
            m = re.match(r"CHOSE: \[P([12])\] (?:PlayCard|PlayActionCard|PlayReaction) (.+?)(?: to | targeting |$)", t)
            if m and int(m.group(1)) == seat:
                played.add(m.group(2).strip())
    # a card set aside at the mulligan counts as seen only if it came back later (drawn again)
    return {"won": g.get("winner") == seat, "hand": hand | played, "played": played}


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec")
    ap.add_argument("--games-dir", default="", help="keep the recorded games here (default: a temporary folder, deleted)")
    a = ap.parse_args(argv)
    spec = json.loads(Path(a.spec).read_text())
    reg = riftlab.Registry.load()
    deck = spec["deck"]
    agent = spec.get("agent", "random")
    games = int(spec.get("games", 40))
    threads = int(spec.get("threads", 4))
    seed = int(spec.get("seed", 7))
    started = dt.datetime.now(dt.timezone.utc)
    test_id = f"{started.strftime('%Y%m%d-%H%M%S')}-{spec['name']}"
    out_dir = Path(a.games_dir) if a.games_dir else LAB / ".cache" / "card_impact" / test_id
    out_dir.mkdir(parents=True, exist_ok=True)
    opponents = [({"deck": o} if isinstance(o, str) else dict(o)) for o in spec["opponents"]]
    jobs = []
    for i, o in enumerate(opponents):
        o.setdefault("label", Path(o["deck"]).stem)
        half = max(1, games // 2)
        for seat in (1, 2):
            d = out_dir / f"{i:02d}_seat{seat}"
            d1, d2 = (deck, o["deck"]) if seat == 1 else (o["deck"], deck)
            jobs.append((o, seat, d, (d1, d2, agent, agent, half, 1, seed + 1009 * i + 50000 * (seat - 1), str(d), half)))
    print(f"{spec['name']}: {len(opponents)} opponents x {games} games, agent {agent}", flush=True)
    with cf.ThreadPoolExecutor(max_workers=threads) as pool:
        list(pool.map(lambda j: riftlab.play_batch(*j[3]), jobs))

    names = list(riftlab.parse_deck(ROOT / deck)["maindeck"])
    stats = {n: {"gih": [0, 0], "gns": [0, 0], "played": 0} for n in names}
    total = [0, 0]
    per_opp = collections.defaultdict(lambda: [0, 0])
    for o, seat, d, _ in jobs:
        for f in sorted(d.glob("game_*.json")):
            facts = game_facts(f, seat)
            if not facts:
                continue
            total[0] += facts["won"]; total[1] += 1
            per_opp[o["label"]][0] += facts["won"]; per_opp[o["label"]][1] += 1
            for n in names:
                k = "gih" if n in facts["hand"] else "gns"
                stats[n][k][0] += facts["won"]; stats[n][k][1] += 1
                if n in facts["played"]:
                    stats[n]["played"] += 1
    rows = []
    for n, s in stats.items():
        d, lo, hi = wilson_diff(s["gih"][0], s["gih"][1], s["gns"][0], s["gns"][1])
        rows.append({"card": n, "copies": riftlab.parse_deck(ROOT / deck)["maindeck"][n],
                     "games_in_hand": s["gih"][1], "gih": round(s["gih"][0] / s["gih"][1], 4) if s["gih"][1] else None,
                     "games_not_seen": s["gns"][1], "gns": round(s["gns"][0] / s["gns"][1], 4) if s["gns"][1] else None,
                     "iwd": round(d, 4) if d is not None else None,
                     "iwd_ci95": [round(lo, 4), round(hi, 4)] if d is not None else None,
                     "played_share": round(s["played"] / s["gih"][1], 3) if s["gih"][1] else None})
    rows.sort(key=lambda r: -(r["iwd"] if r["iwd"] is not None else -9))
    result = {"id": test_id, "name": spec["name"], "kind": "card_impact", "deck": deck, "agent": agent,
              "started_utc": started.isoformat(timespec="seconds"),
              "finished_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
              "engine_commit": riftlab.git_commit(), "spec": spec,
              "games": total[1], "win_rate": round(total[0] / total[1], 4) if total[1] else None,
              "by_opponent": {k: {"games": v[1], "wins": v[0]} for k, v in per_opp.items()}, "cards": rows}
    out = LAB / "results" / f"{test_id}.json"
    out.write_text(json.dumps(result, indent=1))
    print(f"{total[1]} games, list win rate {100 * total[0] / max(1, total[1]):.1f}%")
    print(f"{'card':<28} {'n':>2} {'in hand':>8} {'GIH':>6} {'GNS':>6} {'IWD':>7}  95% interval     played")
    for r in rows:
        if r["iwd"] is None:
            continue
        print(f"{r['card'][:28]:<28} {r['copies']:>2} {r['games_in_hand']:>8} {100 * r['gih']:6.1f} {100 * r['gns']:6.1f} {100 * r['iwd']:+7.1f}  "
              f"[{100 * r['iwd_ci95'][0]:+5.1f}, {100 * r['iwd_ci95'][1]:+5.1f}]  {100 * r['played_share']:5.0f}%")
    print(out.relative_to(ROOT))
    if not a.games_dir:
        shutil.rmtree(out_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
