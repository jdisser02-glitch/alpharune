#!/usr/bin/env python3
"""Merge one-opponent test results into one multi-opponent result.

A list test against a field is dispatched as one request per opponent so the machines run side by side
(`lab/specs/<name>_<opponent>.json`, each with the same lists, the same seed and one opponent). This puts the
results back together as one test in the format `lab/riftlab.py run` writes, so the dashboard reads it as one.

    python3 lab/tools/merge_results.py NAME [--write] [--weights opp=63,opp2=51]

NAME is the common result name: every `lab/results/*-NAME-<opponent>.json` is taken. Weights come from each
request's own opponent entry unless given. Prints, per list, the weighted win rate and its difference against
the first list (the reference) with a 95% interval (normal approximation, lists treated as independent although
they share deals, so the interval is on the wide side); with three lists or more the last column compares each
list with the one before it. `--write` writes lab/results/<stamp>-NAME.json (stamp of the last result to finish).
"""
import argparse
import copy
import glob
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def rate_var(deck):
    total = sum(m["weight"] for m in deck["matchups"])
    rate = sum(m["weight"] * m["wins"] / m["games"] for m in deck["matchups"]) / total
    var = sum((m["weight"] / total) ** 2 * (m["wins"] / m["games"]) * (1 - m["wins"] / m["games"]) / m["games"]
              for m in deck["matchups"])
    return rate, var


def interval(a, b):
    (ra, va), (rb, vb) = rate_var(a), rate_var(b)
    se = math.sqrt(va + vb)
    return ra - rb, ra - rb - 1.96 * se, ra - rb + 1.96 * se


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--weights", default="")
    a = ap.parse_args()

    files = sorted(f for f in glob.glob(str(ROOT / "lab" / "results" / f"*-{a.name}-*.json"))
                   if re.search(rf"\d{{8}}-\d{{6}}-{re.escape(a.name)}-[a-z0-9-]+\.json$", f))
    if not files:
        raise SystemExit(f"no lab/results/*-{a.name}-<opponent>.json")
    parts = [json.loads(Path(f).read_text()) for f in files]
    given = dict((k, float(v)) for k, v in (kv.split("=") for kv in a.weights.split(",") if kv))
    labels = [d["label"] for d in parts[0]["decks"]]
    for p in parts:
        if [d["label"] for d in p["decks"]] != labels or p["seed"] != parts[0]["seed"]:
            raise SystemExit(f"{p['id']}: not the same lists or seed as {parts[0]['id']}")
    parts.sort(key=lambda p: -p["decks"][0]["matchups"][0].get("weight", 1))

    decks = []
    for i in range(len(labels)):
        d = copy.deepcopy(parts[0]["decks"][i])
        d["matchups"] = []
        for p in parts:
            for m in copy.deepcopy(p["decks"][i]["matchups"]):
                slug = p["name"][len(a.name) + 1:]
                m["weight"] = given.get(slug, m.get("weight", 1))
                d["matchups"].append(m)
        d["weighted_win_rate"] = round(rate_var(d)[0], 4)
        decks.append(d)

    print("opponents:", ", ".join(f"{m['opponent']} ({m['weight']:g})" for m in decks[0]["matchups"]))
    for i, d in enumerate(decks):
        line = f"{d['label'][:52]:52s} {100 * d['weighted_win_rate']:5.1f}%"
        if i:
            diff, lo, hi = interval(d, decks[0])
            line += f"  vs reference {100 * diff:+5.1f} [{100 * lo:+5.1f}, {100 * hi:+5.1f}]"
        if i > 1:
            diff, lo, hi = interval(d, decks[i - 1])
            line += f"  vs previous {100 * diff:+5.1f} [{100 * lo:+5.1f}, {100 * hi:+5.1f}]"
        print(line + "  wins " + "/".join(str(m["wins"]) for m in d["matchups"]))

    if a.write:
        first = parts[0]
        stamp = re.sub(r"[-:T]", "", max(p["finished_utc"] for p in parts)[:19])
        rid = f"{stamp[:8]}-{stamp[8:]}-{a.name}"
        spec = dict(first["spec"], name=a.name,
                    opponents=[dict(o, weight=given.get(p["name"][len(a.name) + 1:], o.get("weight", 1)))
                               for p in parts for o in p["spec"]["opponents"]])
        out = {"id": rid, "name": a.name,
               "description": re.sub(r"\s*This request:.*$", "", first.get("description", "")) +
                              f" Merged from {len(parts)} requests, one per opponent.",
               "started_utc": min(p["started_utc"] for p in parts), "engine_commit": first["engine_commit"],
               "agent": first["agent"], "opp_agent": first["opp_agent"],
               "games_per_matchup": first["games_per_matchup"], "threads": first["threads"], "seed": first["seed"],
               "spec": spec, "merged_from": [p["id"] for p in parts], "decks": decks,
               "finished_utc": max(p["finished_utc"] for p in parts)}
        path = ROOT / "lab" / "results" / f"{rid}.json"
        path.write_text(json.dumps(out, indent=1))
        print("written", path.relative_to(ROOT))


if __name__ == "__main__":
    main()
