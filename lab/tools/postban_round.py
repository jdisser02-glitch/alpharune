#!/usr/bin/env python3
"""Writes the next round's search spec of a post-ban list search from the previous round and a matchup matrix.

    python3 lab/tools/postban_round.py lab/specs/deckopt/mordekaiser-postban.json --round 2 --meta lab/meta/postban_meta_m1.json

The new spec (lab/specs/deckopt/<name>-r<round>.json, plus its explore request files) is the old one with:
  - every pilot replaced by its legend's net as committed now (cheap settings: sims=4, worlds=2);
  - the opponents and their weights taken from the matrix: every list of the matrix that is not of this legend,
    weighted by `next_field_weight` (half the prior share, half the equilibrium share); weights under 1 are dropped;
  - starting lists: the old reference, the old starting lists, and the lists the old round raced to the end
    (race2's ranking, the confirmation's lists);
  - `explore_from`: the old round (its games count in the card model with their own intercepts);
  - new seeds, the old stages' settings.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lab" / "learn"))
sys.path.insert(0, str(ROOT / "lab" / "tools"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec"); ap.add_argument("--round", type=int, required=True); ap.add_argument("--meta", required=True)
    a = ap.parse_args()
    import loop
    import meta_matrix
    old = json.loads((ROOT / a.spec).read_text())
    meta = json.loads((ROOT / a.meta).read_text())
    me = loop.specialist_of_deck(old["base"])
    rows = [d for d in meta["decks"] if loop.specialist_of_deck(d["deck"]) != me and d["next_field_weight"] >= 1]
    pil = meta_matrix.pilots([{"key": d["key"], "deck": d["deck"]} for d in rows] + [{"key": "__me", "deck": old["base"]}], False)
    base = old["name"].split("-r")[0] if old["name"].rsplit("-r", 1)[-1].isdigit() else old["name"]
    name = f"{base}-r{a.round}"
    centres = list(old.get("centres", []))
    d = ROOT / "lab/deckopt" / old["name"]
    for f in ("race2-rank.json", "confirm-rank.json"):
        if (d / f).exists():
            centres += [r["path"] for r in json.loads((d / f).read_text())["ranking"] if r["label"] != "reference"]
    centres = list(dict.fromkeys(c for c in centres if c != old["base"]))
    bump = 100 * (a.round - 1)
    new = dict(old, name=name, centres=centres, agent=pil["__me"][0], explore_from=[old["name"]] + list(old.get("explore_from", [])),
               seed=int(old["seed"]) + bump,
               opponents=[{"deck": r["deck"], "label": r["label"], "weight": r["next_field_weight"], "agent": pil[r["key"]][0]} for r in rows],
               pilot_lists=list(dict.fromkeys(list(old.get("pilot_lists", [])) + [old["base"]] + centres)),
               description=f"Round {a.round} of {base} (lab/tools/postban_round.py): pilots as committed on the day it was written "
                           f"({pil['__me'][1]} for this legend), opponents and weights from {a.meta} (half prior, half equilibrium), "
                           f"starting lists = round {a.round - 1}'s plus the lists it raced to the end. " + old.get("description", "")[:400])
    for st in ("race1", "race2", "confirm", "confirm2"):
        if st in new:
            new[st] = dict(new[st], seed=int(new[st]["seed"]) + bump)
            new[st].pop("extra", None)
    p = ROOT / "lab/specs/deckopt" / f"{name}.json"
    p.write_text(json.dumps(new, indent=1) + "\n")
    print(p.relative_to(ROOT), "| pilot", pil["__me"][1], "|", len(rows), "opponents:",
          ", ".join(f"{r['key']} {r['next_field_weight']:g}" for r in rows), "|", len(centres), "starting lists")
    for k in range(int(new["explore"].get("shards", 1))):
        q = p.with_name(f"{name}.explore-{k:02d}.json")
        q.write_text(json.dumps({"kind": "deckopt", "spec": str(p.relative_to(ROOT)), "shard": k}, indent=1) + "\n")
        print(q.relative_to(ROOT))


if __name__ == "__main__":
    main()
