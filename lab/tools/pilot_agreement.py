#!/usr/bin/env python3
"""Do two pilots rate the cards the same way? (calibration of a cheap pilot against the full one)

Both pilots played the same exploration lists (lab/deckopt.py explore, same seed, so the same lists shard by shard).
For each pilot and each half of the shards (even, odd) the card effects are estimated twice: by the regression of
the win on the card counts, and by "dealt against not dealt" inside the lists that play the card. Halves have
different seeds, so two halves agree only through what the cards really do:

  r(A even, A odd)   how repeatable pilot A's own card effects are (noise ceiling)
  r(A even, B odd)   agreement across pilots, on different games
  the ratio  r(A,B) / sqrt(r(A,A) r(B,B))  estimates how alike the TRUE effects are (1 = same ranking of cards).

usage: pilot_agreement.py SPEC_A.json SPEC_B.json [--min-lists 200]
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]


def load(name, parity):
    lists = []
    for f in sorted((ROOT / "lab" / "deckopt" / name).glob("explore-*.json")):
        k = int(f.stem.split("-")[1])
        if k < 12 and k % 2 == parity:
            lists += json.loads(f.read_text())["lists"]
    return lists


def effects(lists):
    cards = sorted({c for l in lists for c in l["main"]})
    idx = {c: 12 + i for i, c in enumerate(cards)}
    rows = [(l, c) for l in lists for c in l["cells"] if c[2]]
    X = np.zeros((len(rows), 12 + len(cards))); y = np.zeros(len(rows)); n = np.zeros(len(rows))
    for r, (l, (i, seat, g, w)) in enumerate(rows):
        X[r, 2 * i + seat - 1] = 1
        for c, k in l["main"].items():
            X[r, idx[c]] = k
        y[r], n[r] = w, g
    X[:, 12:] -= X[:, 12:].mean(0)
    lam = np.r_[np.zeros(12), np.full(len(cards), 4.0)]; b = np.zeros(X.shape[1])
    for _ in range(60):
        p = 1 / (1 + np.exp(-(X @ b))); W = n * p * (1 - p)
        H = (X.T * W) @ X + np.diag(lam); st = np.linalg.solve(H, X.T @ (y - n * p) - lam * b); b += st
        if abs(st).max() < 1e-9:
            break
    reg = {c: float(b[idx[c]]) for c in cards}
    dealt = {}
    for c in cards:
        num = den = 0.0
        for l in lists:
            if not l["main"].get(c):
                continue
            N = sum(x[2] for x in l["cells"]); Wn = sum(x[3] for x in l["cells"])
            e = (l.get("cards") or {}).get(c)
            n1, w1 = (e[8], e[9]) if e and len(e) >= 10 else (0, 0)
            n0, w0 = N - n1, Wn - w1
            if n1 and n0:
                num += (w1 * n0 - w0 * n1) / N; den += n1 * n0 / N
        if den:
            dealt[c] = num / den
    count = {c: sum(1 for l in lists if l["main"].get(c)) for c in cards}
    return reg, dealt, count


def corr(a, b, keys):
    x = np.array([a[k] for k in keys]); z = np.array([b[k] for k in keys])
    return float(np.corrcoef(x, z)[0, 1])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a"); ap.add_argument("b"); ap.add_argument("--min-lists", type=int, default=200)
    args = ap.parse_args()
    na, nb = json.loads(Path(args.a).read_text())["name"], json.loads(Path(args.b).read_text())["name"]
    E = {(n, p): effects(load(n, p)) for n in (na, nb) for p in (0, 1)}
    for kind, j in (("regression on card counts", 0), ("dealt against not dealt", 1)):
        keys = [c for c in E[(na, 0)][j] if all(c in E[k][j] and E[k][2].get(c, 0) >= args.min_lists / 2 for k in E)]
        raa = corr(E[(na, 0)][j], E[(na, 1)][j], keys); rbb = corr(E[(nb, 0)][j], E[(nb, 1)][j], keys)
        rab = (corr(E[(na, 0)][j], E[(nb, 1)][j], keys) + corr(E[(na, 1)][j], E[(nb, 0)][j], keys)) / 2
        same = (corr(E[(na, 0)][j], E[(nb, 0)][j], keys) + corr(E[(na, 1)][j], E[(nb, 1)][j], keys)) / 2
        ceil = math.sqrt(raa * rbb) if raa > 0 and rbb > 0 else float("nan")
        print(f"{kind}, {len(keys)} cards: {na} with itself {raa:+.2f}; {nb} with itself {rbb:+.2f}; across pilots on "
              f"different games {rab:+.2f} (same games {same:+.2f}); true-effect agreement about "
              f"{rab / ceil if ceil == ceil and ceil > 0 else float('nan'):.2f}")


if __name__ == "__main__":
    sys.exit(main())
