#!/usr/bin/env python3
"""The post-ban metagame as the lab can see it: every list against every other, then what a field of players who
all pick the best deck against each other would settle on.

    python3 lab/tools/meta_matrix.py specs TAG --decks lab/meta/postban_decks_TAG.json [--games 600] [--full]
    python3 lab/tools/meta_matrix.py solve TAG --decks lab/meta/postban_decks_TAG.json

`--decks`: [{"key": "kai_sa", "label": "Kai'Sa (Jason Tian, post-ban)", "deck": "<list>", "prior": 51}, ...]; `prior`
is the share one would guess before any game (Stuttgart's registrations of the legends still standing).

specs  writes one request per list (lab/specs/meta/TAG.row-<key>.json, test name meta-TAG-row-<key>): that list
       against all the others, `--games` per pair half on each seat, each list piloted by its legend's specialist
       net as committed now (the general net otherwise), cheap pilot (sims=4, worlds=2) unless --full. Dispatch each
       with riftlab.yml.
solve  reads lab/results/*-meta-TAG-row-*.json and writes lab/meta/postban_meta_TAG.json and .md:
       - the matrix (row beats column), both directions of a pair pooled;
       - each list's rate against the prior field;
       - the equilibrium of the game "pick a list, win if it beats the opponent's" (Nash of the antisymmetric
         matrix p - 0.5, by multiplicative weights), with its spread over 200 redraws of the matrix from the games
         played (a pair known to 2 points moves an equilibrium a lot: read the range, not the point);
       - the field of the next search round: half prior, half equilibrium.
What it cannot see: lists nobody has built yet, sideboards, and above all pilots of unequal strength. A legend
whose pilot trained less looks weaker than it is (lab/learn/TRAINING_KNOWLEDGE.md); the page says how many
iterations each pilot has.
"""
import argparse
import glob
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lab" / "learn"))
sys.path.insert(0, str(ROOT / "lab"))


def pilots(decks, full):
    import loop
    nets = loop.specialist_pilots()
    general = json.loads((ROOT / "lab/learn/ladder.json").read_text())["current_net"]
    sims, worlds = (50, 8) if full else (4, 2)
    out = {}
    for d in decks:
        net = nets.get(loop.specialist_of_deck(d["deck"])) or general
        out[d["key"]] = (f"pmcts:sims={sims},eval=policy:{net},worlds={worlds},settle=60", Path(net).stem)
    return out


def specs(tag, decks, games, full):
    pil = pilots(decks, full)
    for d in decks:
        t = {"name": f"meta-{tag}-row-{d['key']}",
             "description": f"Post-ban matchup matrix {tag} (lab/tools/meta_matrix.py): {d['label']} against the other "
                            f"{len(decks) - 1} lists, {games} games per pair, {'full' if full else 'cheap'} pilots "
                            f"(this list: {pil[d['key']][1]}).",
             "agent": pil[d["key"]][0], "opp_agent": pil[d["key"]][0], "games": games, "review_games": 0, "seed": 7001,
             "decks": [{"deck": d["deck"], "label": d["label"]}],
             "opponents": [{"deck": o["deck"], "label": o["label"], "weight": o.get("prior", 1), "agent": pil[o["key"]][0]}
                           for o in decks if o["key"] != d["key"]]}
        p = ROOT / "lab/specs/meta" / f"{tag}.row-{d['key']}.json"
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(t, indent=1) + "\n")
        print(p.relative_to(ROOT), "pilot", pil[d["key"]][1])


def nash(A, steps=6000):
    """Equilibrium of the symmetric zero-sum game with antisymmetric payoff A (multiplicative weights, averaged)."""
    import numpy as np
    n = len(A); x = np.full(n, 1.0 / n); avg = np.zeros(n); eta = 0.5
    for t in range(steps):
        x = x * np.exp(eta * (A @ x) / (1 + t) ** 0.5 * 4); x /= x.sum(); avg += x
    avg /= steps
    return avg, float((A @ avg).max())


def solve(tag, decks):
    import numpy as np
    keys = [d["key"] for d in decks]; n = len(keys); by_label = {d["label"]: i for i, d in enumerate(decks)}
    W = np.zeros((n, n)); G = np.zeros((n, n)); nets = {}
    for f in sorted(glob.glob(str(ROOT / "lab/results" / f"*-meta-{tag}-row-*.json"))):
        r = json.loads(Path(f).read_text())
        for dk in r["decks"]:
            i = by_label[dk["label"]]
            nets[keys[i]] = Path(re.search(r"policy:([^,]+)", r["agent"]).group(1)).stem
            for m in dk["matchups"]:
                j = by_label[m["opponent"]]
                W[i, j] += m["wins"]; G[i, j] += m["games"]
                W[j, i] += m["games"] - m["wins"]; G[j, i] += m["games"]     # the other direction of the same pair
    missing = [(keys[i], keys[j]) for i in range(n) for j in range(i + 1, n) if not G[i, j]]
    if missing:
        raise SystemExit(f"pairs without a game yet: {missing[:6]}{' …' if len(missing) > 6 else ''}")
    P = np.where(G > 0, W / np.maximum(G, 1), 0.5); np.fill_diagonal(P, 0.5)
    prior = np.array([float(d.get("prior", 1)) for d in decks]); prior /= prior.sum()
    eq, gap = nash(P - 0.5)
    rng = np.random.default_rng(1); draws = []
    for _ in range(200):
        Q = np.full((n, n), 0.5)
        for i in range(n):
            for j in range(i + 1, n):
                q = rng.beta(W[i, j] + 1, G[i, j] - W[i, j] + 1); Q[i, j], Q[j, i] = q, 1 - q
        draws.append(nash(Q - 0.5, 2500)[0])
    draws = np.array(draws); lo, hi = np.percentile(draws, 10, 0), np.percentile(draws, 90, 0)
    vs_prior = P @ prior
    nxt = 0.5 * prior + 0.5 * draws.mean(0)
    out = {"tag": tag, "games_per_pair": int(G[0, 1]), "decks": [
        {"key": keys[i], "label": decks[i]["label"], "deck": decks[i]["deck"], "pilot": nets.get(keys[i]),
         "prior": round(float(prior[i]), 4), "vs_prior_field": round(float(vs_prior[i]), 4),
         "equilibrium": round(float(eq[i]), 4), "equilibrium_mean": round(float(draws.mean(0)[i]), 4),
         "equilibrium_p10": round(float(lo[i]), 4), "equilibrium_p90": round(float(hi[i]), 4),
         "next_field_weight": round(100 * float(nxt[i]), 1)} for i in range(n)],
        "matrix": [[round(float(P[i, j]), 4) for j in range(n)] for i in range(n)], "equilibrium_gap": round(gap, 4)}
    (ROOT / "lab/meta" / f"postban_meta_{tag}.json").write_text(json.dumps(out, indent=1) + "\n")
    order = sorted(range(n), key=lambda i: -vs_prior[i])
    md = [f"# Post-ban matchup matrix {tag}", "",
          f"{int(G[0, 1])} games per pair (both directions pooled), from `lab/results/*-meta-{tag}-row-*.json`; made by "
          "`lab/tools/meta_matrix.py solve`. Rates are what the lab's pilots do, not what players do: a legend whose pilot "
          "trained less looks weaker than it is.", "",
          "| list | pilot | against the prior field | share at equilibrium (10%-90% of 200 redraws) | prior share |", "|---|---|---|---|---|"]
    for i in order:
        md.append(f"| {decks[i]['label']} | {nets.get(keys[i], '?')} | {100 * vs_prior[i]:.1f}% | {100 * draws.mean(0)[i]:.0f}% "
                  f"({100 * lo[i]:.0f}-{100 * hi[i]:.0f}) | {100 * prior[i]:.0f}% |")
    md += ["", "Row beats column (%):", "", "| | " + " | ".join(keys[j] for j in order) + " |", "|---|" + "---|" * n]
    for i in order:
        md.append(f"| {keys[i]} | " + " | ".join("-" if i == j else f"{100 * P[i, j]:.0f}" for j in order) + " |")
    (ROOT / "lab/meta" / f"postban_meta_{tag}.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["specs", "solve"]); ap.add_argument("tag")
    ap.add_argument("--decks", required=True); ap.add_argument("--games", type=int, default=600)
    ap.add_argument("--full", action="store_true")
    a = ap.parse_args()
    decks = json.loads((ROOT / a.decks).read_text())
    specs(a.tag, decks, a.games, a.full) if a.cmd == "specs" else solve(a.tag, decks)


if __name__ == "__main__":
    main()
