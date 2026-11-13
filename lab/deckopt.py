#!/usr/bin/env python3
"""deckopt — list search by card-level statistics, then a race, then a confirmation.

Why not the genetic search alone (lab/ga.py): a list's win rate on 60 to 360 games is known to 3-6 points while good
lists differ by 2-8, so ranking lists one against the other mostly ranks luck (lab/OPTIMISER_KNOWLEDGE.md: the
finalists of 12 Nov led by 7.8 points during the search and by -2.3 on fresh games). A card, on the other hand, is
in hundreds of the lists tried: its effect is known far better than any one list's rate. So:

  1. explore   (runners, in shards)  lists drawn around the centres (a centre plus a few random legal swaps) play a
               little each (2 games per opponent and seat), with the cards of every game recorded.
  2. model     (here)  ridge logistic regression of the win on the card counts, the opponent and the seat, over
               every explored game; plus, per card, the win rate when it is in the hand as dealt against when it is
               not (the shuffle decides that, so it compares like with like inside a list).
  3. propose   (here)  candidates read from the model: the best centres and a few steps of the swaps the model
               rates highest from them. The model's favourite is never taken on its word: it only buys a place in
               the race.
  4. wave      (here → runners)  the candidates and the reference play the same seeds, one request per opponent:
               race1 (everyone, few games), race2 (the best, more games), confirm (the last two or three and the
               reference, many games on new seeds). `rank` merges a stage and says who goes on.

usage:
  deckopt.py explore REQUEST.json            a runner's share: {"kind": "deckopt", "spec": "...json", "shard": 3}
  deckopt.py requests SPEC.json              write the explore request files, one per shard
  deckopt.py model SPEC.json                 fit and print the card table (lab/deckopt/<name>/model.json)
  deckopt.py propose SPEC.json               write the candidate lists (lab/variants/<name>-cand-*.txt)
  deckopt.py wave SPEC.json --stage race1    write the per-opponent test specs of a stage
  deckopt.py rank SPEC.json --stage race1    merge the stage's results and choose who goes on

spec (lab/specs/deckopt/<name>.json) = {
  "kind": "deckopt-spec", "name": "...", "description": "...",
  "base": "<reference list: its legend, champion, runes and battlefields frame every list>",
  "centres": ["<list>", ...],                 lists the exploration is drawn around (the base is always one)
  "pool": {"from_decks": [...], "extra": [...], "exclude": [...]}, "max_copies": 3,
  "pilot_lists": ["<list the pilot trained on>", ...],
  "agent": "...", "opp_agent": "...", "opponents": [{"deck", "label", "weight", "agent"}, ...], "seed": 1,
  "explore": {"shards": 12, "lists": 60, "games_per_cell": 2, "swaps_mean": 3, "max_swaps": 8},
  "race1": {"games": 72, "seed": 5101}, "race2": {"games": 288, "keep": 5, "seed": 5102},
  "confirm": {"games": 600, "keep": 2, "seed": 5103, "extra": [{"deck": "...", "label": "..."}]}
}
"games" in a stage are per opponent, half on each seat.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures as cf
import datetime as dt
import glob
import json
import math
import random
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ga as galib  # noqa: E402
import riftlab  # noqa: E402
from riftlab import LAB, ROOT  # noqa: E402

BUDGET_S = 300 * 60         # a runner stops drawing new lists after this long (the job limit is 350 minutes)


def out_dir(spec: dict) -> Path:
    d = LAB / "deckopt" / spec["name"]
    d.mkdir(parents=True, exist_ok=True)
    return d


def frame_of(spec: dict):
    """The GA object gives the pool, the legality check and the single swap; nothing of its search is used."""
    g = galib.GA({**spec, "threads": spec.get("threads", 4)}, riftlab.Registry.load(), True)
    return g


def centres_of(spec: dict, g) -> list[dict]:
    """[{path, main}] for the base and every centre that is legal in the base's frame and plays no excluded card."""
    banned = set(spec.get("pool", {}).get("exclude", []))
    out, seen = [], set()
    paths = [spec["base"]] + [str(Path(f).relative_to(ROOT)) for pat in spec.get("centres", []) for f in sorted(glob.glob(str(ROOT / pat)))]
    for p in paths:
        m = dict(riftlab.parse_deck(ROOT / p)["maindeck"])
        key = galib.key_of(m)
        if key in seen or set(m) & banned or sum(m.values()) != sum(g.base["maindeck"].values()) or not g.legal(m):
            continue
        seen.add(key)
        out.append({"path": p, "main": m})
    return out


def write_list(g, main: dict, path: Path, header: list[str]) -> None:
    d = dict(g.base)
    d["maindeck"] = collections.OrderedDict(sorted(((k, v) for k, v in main.items() if v > 0), key=lambda kv: kv[0]))
    path.parent.mkdir(parents=True, exist_ok=True)
    riftlab.write_deck(d, path, header)


# ── 1. explore ───────────────────────────────────────────────────────────────
def explore(req_path: str) -> int:
    req = json.loads(Path(req_path).read_text())
    spec = json.loads((ROOT / req["spec"]).read_text())
    shard = int(req["shard"])
    ex = spec.get("explore", {})
    g = frame_of(spec)
    centres = centres_of(spec, g)
    rng = random.Random(int(spec.get("seed", 1)) * 7919 + shard)
    g.rng = rng
    # A request may carry its own "lists" (a longer shard on a machine that has the time).
    n_lists, cell_games = int(req.get("lists", ex.get("lists", 60))), int(ex.get("games_per_cell", 2))
    mean_k, max_k = float(ex.get("swaps_mean", 3)), int(ex.get("max_swaps", 8))
    out_path = out_dir(spec) / f"explore-{shard:02d}.json"
    result = {"spec": req["spec"], "name": spec["name"], "shard": shard, "engine_commit": riftlab.git_commit(),
              "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"), "agent": g.agent,
              "opponents": [[o["label"], o["deck"], o["weight"], g.opp_agent_of(o)] for o in g.opponents],
              "centres": [c["path"] for c in centres], "games_per_cell": cell_games, "lists": []}
    t0 = time.time()
    tmp = Path(tempfile.mkdtemp(prefix="deckopt-"))

    def draw(j: int):
        """Every centre once per shard (they are the yardsticks), then a centre plus k random legal swaps."""
        if j < len(centres):
            return j, 0, dict(centres[j]["main"])
        ci = rng.randrange(len(centres))
        k = 1
        while k < max_k and rng.random() > 1.0 / mean_k:      # geometric, mean about swaps_mean
            k += 1
        m = dict(centres[ci]["main"])
        done = 0
        for _ in range(k):
            n = g.swap(m)
            if n:
                m, done = n, done + 1
        return ci, done, m

    with cf.ThreadPoolExecutor(max_workers=int(spec.get("threads", 4))) as pool:
        for j in range(n_lists):
            if time.time() - t0 > BUDGET_S:
                break
            ci, k, main = draw(j)
            path = tmp / f"list_{j:03d}.txt"
            write_list(g, main, path, [f"deckopt {spec['name']} shard {shard} list {j}"])
            seed0 = int(spec.get("seed", 1)) + 1_000_003 * (shard * 1000 + j)
            jobs = []
            for i, o in enumerate(g.opponents):
                oa = g.opp_agent_of(o)
                jobs.append((i, 1, (str(path), o["deck"], g.agent, oa, cell_games, 1, seed0 + 10007 * i, None, 0, 1)))
                jobs.append((i, 2, (o["deck"], str(path), oa, g.agent, cell_games, 1, seed0 + 10007 * i + 5003, None, 0, 2)))
            futs = {pool.submit(riftlab.play_batch, *args): (i, seat) for i, seat, args in jobs}
            cells, cards = [], None
            for f in cf.as_completed(futs):
                i, seat = futs[f]
                x = f.result()
                games = x["p1"] + x["p2"] + x["draws"]
                cells.append([i, seat, games, x["p1"] if seat == 1 else x["p2"]])
                cards = riftlab.merge_exposure(cards, x.get("cards"))
            result["lists"].append({"centre": ci, "swaps": k, "main": main, "cells": sorted(cells),
                                    "cards": (cards or {}).get("cards", {})})
            result["elapsed_s"] = round(time.time() - t0)
            out_path.write_text(json.dumps(result, separators=(",", ":")))
            won = sum(c[3] for c in cells); played = sum(c[2] for c in cells)
            print(f"shard {shard} list {j:3d} centre {ci:2d} swaps {k}  {won}/{played}  ({result['elapsed_s']} s)", flush=True)
    print(f"written {out_path.relative_to(ROOT)}: {len(result['lists'])} lists")
    return 0


def requests(spec_path: str) -> int:
    spec = json.loads(Path(spec_path).read_text())
    rel = str(Path(spec_path).resolve().relative_to(ROOT))
    for k in range(int(spec.get("explore", {}).get("shards", 12))):
        p = Path(spec_path).resolve().with_name(f"{spec['name']}.explore-{k:02d}.json")
        p.write_text(json.dumps({"kind": "deckopt", "spec": rel, "shard": k}, indent=1) + "\n")
        print(p.relative_to(ROOT))
    return 0


# ── 2. model ─────────────────────────────────────────────────────────────────
def load_explored(spec: dict) -> list[dict]:
    lists = []
    for f in sorted((LAB / "deckopt" / spec["name"]).glob("explore-*.json")):
        lists += json.loads(f.read_text())["lists"]
    return lists


def fit(spec: dict, verbose: bool = True) -> dict:
    import numpy as np
    lists = load_explored(spec)
    if not lists:
        raise SystemExit("nothing explored yet")
    g = frame_of(spec)
    opps = g.opponents
    seen = set()
    for p in spec.get("pilot_lists", []):
        for f in glob.glob(str(ROOT / p)):
            seen |= set(riftlab.parse_deck(Path(f))["maindeck"])
    cards = sorted({c for l in lists for c in l["main"]})
    ncell = 2 * len(opps)
    rows = [(l, c) for l in lists for c in l["cells"] if c[2]]
    X = np.zeros((len(rows), ncell + len(cards) + 1)); y = np.zeros(len(rows)); n = np.zeros(len(rows))
    idx = {c: ncell + i for i, c in enumerate(cards)}
    for r, (l, (i, seat, games, wins)) in enumerate(rows):
        X[r, 2 * i + seat - 1] = 1
        for c, k in l["main"].items():
            X[r, idx[c]] = k
        X[r, -1] = sum(k for c, k in l["main"].items() if seen and c not in seen)
        y[r], n[r] = wins, games
    mean = X[:, ncell:].mean(0); X[:, ncell:] -= mean      # card counts sum to the deck size: centre them

    def irls(lam_cards: float):
        lam = np.r_[np.zeros(ncell), np.full(len(cards), lam_cards), [lam_cards if seen else 1e6]]
        b = np.zeros(X.shape[1])
        for _ in range(80):
            p = 1 / (1 + np.exp(-(X @ b))); W = n * p * (1 - p)
            H = (X.T * W) @ X + np.diag(lam)
            step = np.linalg.solve(H, X.T @ (y - n * p) - lam * b)
            b += step
            if abs(step).max() < 1e-9:
                break
        return b, np.linalg.inv(H)

    # Empirical Bayes: how much the cards really differ (variance of the weakly-penalised effects minus their
    # sampling variance) sets the penalty, so a noisy data set pulls every card towards "no effect".
    b0, C0 = irls(1.0)
    eff, var = b0[ncell:-1], np.diag(C0)[ncell:-1]
    A = max(float(np.var(eff) - var.mean()), 1e-4)
    lam = min(400.0, max(2.0, 1.0 / A))
    b, C = irls(lam)
    p_all = float(y.sum() / n.sum())
    slope = p_all * (1 - p_all)                 # log-odds → win-rate points near the overall rate
    table = []
    for c in cards:
        j = idx[c]
        table.append({"card": c, "beta": float(b[j]), "se": float(math.sqrt(C[j, j])), "raw_beta": float(b0[j]),
                      "raw_se": float(math.sqrt(C0[j, j])), "points_per_copy": 100 * slope * float(b[j]),
                      "lists": sum(1 for l in lists if l["main"].get(c)), "pilot_knows": (c in seen) if seen else None})
    # Dealt against not dealt, inside the lists that play the card (Mantel-Haenszel over the lists).
    for row in table:
        c = row["card"]; num = den = v = 0.0; g1 = g0 = 0
        for l in lists:
            if not l["main"].get(c):
                continue
            N = sum(x[2] for x in l["cells"]); Wn = sum(x[3] for x in l["cells"])
            e = (l.get("cards") or {}).get(c)
            n1, w1 = (e[8], e[9]) if e and len(e) >= 10 else (0, 0)
            n0, w0 = N - n1, Wn - w1
            if not n1 or not n0:
                continue
            num += (w1 * n0 - w0 * n1) / N; den += n1 * n0 / N
            pb = Wn / N; v += (n1 * n0 / N) ** 2 * pb * (1 - pb) * (1 / n1 + 1 / n0)
            g1 += n1; g0 += n0
        row["dealt_games"] = g1
        row["dealt_diff"] = 100 * num / den if den else None
        row["dealt_se"] = 100 * math.sqrt(v) / den if den else None
    model = {"name": spec["name"], "lists": len(lists), "games": int(n.sum()), "rate": p_all, "prior_sd_logit": math.sqrt(A),
             "ridge": lam, "slope": slope, "unseen_beta": float(b[-1]) if seen else None,
             "unseen_se": float(math.sqrt(C[-1, -1])) if seen else None, "cards": table,
             "cov": {"cards": cards, "matrix": [[round(float(x), 6) for x in r] for r in C[ncell:-1, ncell:-1]]}}
    (out_dir(spec) / "model.json").write_text(json.dumps(model, separators=(",", ":")))
    if verbose:
        print(f"{model['lists']} lists, {model['games']} games, overall {100 * p_all:.1f}%; the cards differ by about "
              f"{100 * slope * math.sqrt(A):.1f} points per copy (prior), ridge {lam:.0f}")
        if seen:
            print(f"each copy of a card the pilot never trained on: {100 * slope * b[-1]:+.2f} points "
                  f"(± {100 * slope * math.sqrt(C[-1, -1]):.2f})")
        print(f"{'card':30s} {'pts/copy':>9s} {'±':>5s} {'lists':>6s}   {'dealt vs not':>13s} {'±':>5s} {'games dealt':>11s}")
        for r in sorted(table, key=lambda r: -r["beta"]):
            dd = "" if r["dealt_diff"] is None else f"{r['dealt_diff']:+13.1f} {r['dealt_se']:5.1f} {r['dealt_games']:11d}"
            print(f"{r['card'][:30]:30s} {r['points_per_copy']:+9.2f} {100 * slope * r['se']:5.2f} {r['lists']:6d}   {dd}")
    return model


# ── 3. propose ───────────────────────────────────────────────────────────────
def propose(spec: dict) -> int:
    import numpy as np
    model = json.loads((out_dir(spec) / "model.json").read_text())
    g = frame_of(spec)
    beta = {r["card"]: r["beta"] for r in model["cards"]}
    names = model["cov"]["cards"]; C = np.array(model["cov"]["matrix"]); pos = {c: i for i, c in enumerate(names)}
    slope = model["slope"]
    lists = load_explored(spec)
    centres = centres_of(spec, g)

    def score(m):           # log-odds relative to the average explored list
        return sum(beta.get(c, 0.0) * k for c, k in m.items())

    def se_diff(a, b):      # standard error of score(a) - score(b)
        d = np.zeros(len(names))
        for c, k in a.items():
            if c in pos: d[pos[c]] += k
        for c, k in b.items():
            if c in pos: d[pos[c]] -= k
        return float(math.sqrt(max(0.0, d @ C @ d)))

    base = centres[0]["main"]
    observed = {}
    for l in lists:
        if l["swaps"] == 0:
            o = observed.setdefault(l["centre"], [0, 0])
            o[0] += sum(x[2] for x in l["cells"]); o[1] += sum(x[3] for x in l["cells"])
    ranked = sorted(range(len(centres)), key=lambda i: -score(centres[i]["main"]))

    def climb(m, steps):
        """The model's best single swaps, one after the other (a card the model has not seen is never added)."""
        m = dict(m); trail = []
        for _ in range(steps):
            best = None
            for o in [k for k, v in m.items() if v > 0]:
                for i in g.pool:
                    if i == o or i not in beta or m.get(i, 0) >= g.cap(i):
                        continue
                    gain = beta[i] - beta.get(o, 0.0)
                    if best is None or gain > best[0]:
                        n = dict(m); n[o] -= 1; n[i] = n.get(i, 0) + 1
                        n = {k: v for k, v in n.items() if v > 0}
                        if g.legal(n):
                            best = (gain, o, i, n)
            if not best or best[0] <= 0:
                break
            m = best[3]; trail.append((best[1], best[2]))
        return m, trail

    cands = [("reference", dict(base), "the reference list")]
    for rank, ci in enumerate(ranked[:4]):
        if ci != 0:
            cands.append((f"centre{rank + 1}", dict(centres[ci]["main"]), f"starting list {centres[ci]['path']}"))
    for rank, ci in enumerate(ranked[:2]):
        for steps in ((2, 4, 6, 9) if rank == 0 else (3, 6)):
            m, trail = climb(centres[ci]["main"], steps)
            if trail:
                cands.append((f"c{rank + 1}+{len(trail)}", m, f"{centres[ci]['path']} then the model's {len(trail)} best swaps: "
                              + ", ".join(f"-1 {o} +1 {i}" for o, i in trail)))
    out, seen = [], set()
    for tag, m, why in cands:
        k = galib.key_of(m)
        if k in seen:
            continue
        seen.add(k)
        d, s = score(m) - score(base), se_diff(m, base)
        path = LAB / "variants" / f"{spec['name']}-cand-{tag.replace('+', 'p')}.txt"
        if tag != "reference":
            write_list(g, m, path, [f"Candidate of the card-level search {spec['name']} ({why}).",
                                    f"Predicted against the reference: {100 * slope * d:+.1f} points (± {100 * slope * s:.1f}); "
                                    f"a prediction buys a place in the race, nothing more.",
                                    f"Base: {spec['base']} — generated by lab/deckopt.py"])
        out.append({"tag": tag, "path": spec["base"] if tag == "reference" else str(path.relative_to(ROOT)), "why": why,
                    "predicted_points": 100 * slope * d, "predicted_se": 100 * slope * s, "main": m})
        print(f"{tag:10s} {100 * slope * d:+6.1f} ± {100 * slope * s:4.1f}  {why[:150]}")
    for i, c in enumerate(centres):
        o = observed.get(i)
        if o and o[0]:
            print(f"  centre {i:2d} observed {100 * o[1] / o[0]:5.1f}% on {o[0]} games, model {100 * slope * (score(c['main']) - score(base)):+5.1f}  {c['path']}")
    (out_dir(spec) / "candidates.json").write_text(json.dumps(out, indent=1))
    return 0


# ── 4. waves ─────────────────────────────────────────────────────────────────
def stage_lists(spec: dict, stage: str) -> list[dict]:
    d = out_dir(spec)
    if stage == "race1":
        return [{"deck": c["path"], "label": c["tag"]} for c in json.loads((d / "candidates.json").read_text())]
    prev = json.loads((d / f"{'race1' if stage == 'race2' else 'race2'}-rank.json").read_text())
    keep = int(spec.get(stage, {}).get("keep", 5 if stage == "race2" else 2))
    rows = [r for r in prev["ranking"] if r["label"] != "reference"][:keep]
    out = [{"deck": spec["base"], "label": "reference"}] + [{"deck": r["path"], "label": r["label"]} for r in rows]
    return out + list(spec.get(stage, {}).get("extra", []))


def wave(spec: dict, stage: str) -> int:
    st = spec.get(stage, {})
    decks = stage_lists(spec, stage)
    slug = lambda s: "".join(ch if ch.isalnum() else "-" for ch in s.lower()).strip("-")
    for o in spec["opponents"]:
        tag = slug(o["label"].split("(")[0])[:14].strip("-") + "-" + slug(Path(o["deck"]).stem)[:10].strip("-")
        t = {"name": f"{spec['name']}-{stage}-{tag}",
             "description": f"{spec.get('description', spec['name'])} Stage {stage}: {len(decks)} lists on the same seeds, "
                            f"{st.get('games')} games per opponent. This request: {o['label']}.",
             "agent": spec["agent"], "opp_agent": spec.get("opp_agent", spec["agent"]), "games": int(st.get("games", 72)),
             "review_games": 0, "seed": int(st.get("seed", 5100)), "decks": decks, "opponents": [o]}
        p = LAB / "specs" / "deckopt" / f"{spec['name']}.{stage}.{tag}.json"
        p.write_text(json.dumps(t, indent=1) + "\n")
        print(p.relative_to(ROOT))
    return 0


def rank(spec: dict, stage: str) -> int:
    """Merge the stage (every lab/results/*-<name>-<stage>-*.json), add the earlier race stage's games on the lists
    that were in both, shrink the differences towards zero by how much of their spread is noise, and rank."""
    def load(st):
        acc = {}
        for f in sorted(glob.glob(str(LAB / "results" / f"*-{spec['name']}-{st}-*.json"))):
            r = json.loads(Path(f).read_text())
            if r.get("merged_from"):
                continue
            for dk in r["decks"]:
                a = acc.setdefault(dk["label"], {"path": dk["path"], "cells": {}})
                for m in dk["matchups"]:
                    c = a["cells"].setdefault(m["opponent"], [m.get("weight", 1), 0, 0])
                    c[1] += m["games"]; c[2] += m["wins"]
        return acc
    acc = load(stage)
    if stage == "race2":
        for label, a in load("race1").items():
            if label in acc:
                for o, c in a["cells"].items():
                    t = acc[label]["cells"].setdefault(o, [c[0], 0, 0]); t[1] += c[1]; t[2] += c[2]
    if "reference" not in acc:
        raise SystemExit(f"no results for stage {stage} yet")

    def rate_var(a):
        W = sum(c[0] for c in a["cells"].values() if c[1])
        r = sum(c[0] * c[2] / c[1] for c in a["cells"].values() if c[1]) / W
        v = sum((c[0] / W) ** 2 * (c[2] / c[1]) * (1 - c[2] / c[1]) / c[1] for c in a["cells"].values() if c[1])
        return r, v
    r0, v0 = rate_var(acc["reference"])
    rows = []
    for label, a in acc.items():
        r, v = rate_var(a)
        rows.append({"label": label, "path": a["path"], "games": sum(c[1] for c in a["cells"].values()), "rate": r,
                     "diff": r - r0, "se": math.sqrt(v + v0) if label != "reference" else 0.0})
    others = [x for x in rows if x["label"] != "reference"]
    # Shrinkage (Efron 2011): spread of the observed differences minus their sampling variance is what is real.
    if len(others) > 1:
        m = sum(x["diff"] for x in others) / len(others)
        A = max(0.0, sum((x["diff"] - m) ** 2 for x in others) / (len(others) - 1) - sum(x["se"] ** 2 for x in others) / len(others))
    else:
        m, A = 0.0, 0.0
    for x in others:
        k = A / (A + x["se"] ** 2) if (A + x["se"] ** 2) else 0.0
        x["shrunk"] = m + k * (x["diff"] - m)
    for x in rows:
        x.setdefault("shrunk", 0.0)
    rows.sort(key=lambda x: -x["shrunk"])
    out = {"stage": stage, "reference_rate": r0, "real_spread_points": 100 * math.sqrt(A), "ranking": rows}
    (out_dir(spec) / f"{stage}-rank.json").write_text(json.dumps(out, indent=1))
    print(f"stage {stage}: reference {100 * r0:.1f}%; the lists really differ by about {100 * math.sqrt(A):.1f} points (spread minus noise)")
    for x in rows:
        if x["label"] == "reference":
            continue
        print(f"  {x['label']:12s} {100 * x['rate']:5.1f}%  {100 * x['diff']:+5.1f} [{100 * (x['diff'] - 1.96 * x['se']):+5.1f}, "
              f"{100 * (x['diff'] + 1.96 * x['se']):+5.1f}]  shrunk {100 * x['shrunk']:+5.1f}  {x['games']} games  {x['path']}")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("phase", choices=["explore", "requests", "model", "propose", "wave", "rank"])
    ap.add_argument("spec")
    ap.add_argument("--stage", choices=["race1", "race2", "confirm"])
    a = ap.parse_args(argv)
    if a.phase == "explore":
        return explore(a.spec)
    if a.phase == "requests":
        return requests(a.spec)
    spec = json.loads(Path(a.spec).read_text())
    if a.phase == "model":
        fit(spec); return 0
    if a.phase == "propose":
        return propose(spec)
    if not a.stage:
        raise SystemExit("--stage is needed")
    return wave(spec, a.stage) if a.phase == "wave" else rank(spec, a.stage)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
