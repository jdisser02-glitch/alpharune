#!/usr/bin/env python3
"""favorites — does the simulator pick the same favourite as real players, matchup by matchup?

The credibility measure the deck search rests on (Jeremy, 7 Nov: "passe au critère du bon favori"). Being inside
real players' 95% interval is too strict for a bot (real rates mix every skill level and rest on 20-250 games), and
what a list search needs is the direction of each matchup. A matchup counts as right when:
  - real players clearly favour one side (their 95% interval excludes 50%) and the simulation favours the same side;
  - or the real matchup is open (interval contains 50%) and the simulation is inside that interval or within
    5 points of 50%.
Next to it, the rank correlation (Spearman) between simulated and real rates over the matchups: does the bot order
the matchups the way players do. Target: right favourite in >= 80% of matchups.

Sources of simulated rates:
  - each specialist's current net on its own jury (lab/learn/specialists/<name>/ladder.json, the panel of the
    iteration that adopted it: the candidate's rate per jury deck);
  - the newest lab/meta/compare_meta-*.json (Riven's field measure, 10+ legends).
Real rates: the newest lab/meta/riftdecks_winrate_*.json or riftlite_matrix_*.json that has the legend's row, the
one with the most games.

    python3 lab/tools/favorites.py            # prints every legend's score
"""
from __future__ import annotations

import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
META = ROOT / "lab" / "meta"
LEGENDS = ["Riven", "Kennen", "Kai'Sa", "Rengar", "Vex", "Akali", "Evelynn", "LeBlanc", "Lillia", "Mordekaiser",
           "Orianna", "Seraphine", "Master Yi", "Nasus", "Ekko", "Ziggs", "Draven", "Viktor", "Jinx", "Jarvan IV",
           "Volibear", "Kha'Zix", "Irelia", "Yasuo", "Ahri", "Leona", "Lux", "Teemo", "Sett", "Garen", "Miss Fortune"]
SPEC_LEGEND = {"riven": "Riven", "orianna": "Orianna", "mordekaiser": "Mordekaiser", "kennen": "Kennen",
               "kai_sa": "Kai'Sa", "akali": "Akali", "evelynn": "Evelynn", "ziggs": "Ziggs",
               "volibear": "Volibear"}
TARGET = 0.8


def legend_of(path: str):
    s = re.sub(r"[^a-z0-9]", "", Path(path).stem.lower())
    for n in sorted(LEGENDS, key=len, reverse=True):
        if re.sub(r"[^a-z0-9]", "", n.lower()) in s:
            return n
    return None


def wilson(p: float, n: int, z: float = 1.96):
    if not n:
        return [0.0, 1.0]
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return [round(max(0.0, c - h), 3), round(min(1.0, c + h), 3)]


def right_favorite(sim: float, ci) -> bool:
    lo, hi = ci
    if lo > 0.5:
        return sim > 0.5
    if hi < 0.5:
        return sim < 0.5
    return lo <= sim <= hi or abs(sim - 0.5) <= 0.05


def spearman(xs, ys):
    if len(xs) < 3:
        return None
    def ranks(v):
        order = sorted(range(len(v)), key=lambda i: v[i])
        r = [0.0] * len(v)
        i = 0
        while i < len(v):
            j = i
            while j + 1 < len(v) and v[order[j + 1]] == v[order[i]]:
                j += 1
            for k in range(i, j + 1):
                r[order[k]] = (i + j) / 2
            i = j + 1
        return r
    rx, ry = ranks(xs), ranks(ys)
    mx, my = sum(rx) / len(rx), sum(ry) / len(ry)
    cov = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    vx, vy = sum((a - mx) ** 2 for a in rx), sum((b - my) ** 2 for b in ry)
    return round(cov / math.sqrt(vx * vy), 3) if vx and vy else None


def real_row(legend: str):
    """(matrix file, {opponent: {win_rate, games}}) with the most games for this legend."""
    best = None
    for f in sorted(META.glob("riftdecks_winrate_*.json")) + sorted(META.glob("riftlite_matrix_*.json")):
        row = json.loads(f.read_text()).get("matchups", {}).get(legend)
        if row:
            n = sum(v.get("games", 0) for v in row.values())
            if best is None or n >= best[2]:
                best = (f, row, n)
    return (best[0], best[1]) if best else (None, {})


def score(legend: str, sims: dict, real: dict) -> dict:
    """sims: {opponent legend: (rate, games)}; real: the matrix row."""
    rows = []
    for opp, (sim, games) in sorted(sims.items()):
        r = real.get(opp)
        if opp == legend or not r or not r.get("games"):
            continue
        ci = wilson(r["win_rate"], r["games"])
        rows.append({"opponent_legend": opp, "sim": round(sim, 3), "sim_games": games, "real": r["win_rate"],
                     "real_games": r["games"], "real_ci95": ci, "right": right_favorite(sim, ci)})
    right = sum(r["right"] for r in rows)
    return {"rows": rows, "right": right, "total": len(rows), "rate": round(right / len(rows), 3) if rows else None,
            "spearman": spearman([r["sim"] for r in rows], [r["real"] for r in rows]), "target": TARGET,
            "wrong": [f"{legend} vs {r['opponent_legend']} (sim {round(100 * r['sim'])}%, real {round(100 * r['real'])}%)"
                      for r in rows if not r["right"]]}


def from_specialist(name: str):
    f = ROOT / "lab" / "learn" / "specialists" / name / "ladder.json"
    if not f.exists():
        return None
    L = json.loads(f.read_text())
    cur = Path(L.get("current_net") or "").stem
    adopted = [i for i in L.get("iterations", []) if i.get("accepted") and Path(str(i.get("net", ""))).stem == cur]
    if not adopted or not (adopted[-1].get("panel") or {}).get("matchups"):
        return None
    it = adopted[-1]
    legend = SPEC_LEGEND.get(name, name.title())
    # Continuous training (10 Nov): a panel is small (about 32 games per opponent) and the net moves a little every
    # iteration, so the simulated rates are pooled over the last 12 panels (the held-out opponents).
    panels = [it]
    if it.get("continuous"):
        panels = [r for r in L.get("iterations", []) if r.get("continuous") and (r.get("panel") or {}).get("matchups")][-12:]
    agg = {}
    for r in panels:
        for m in r["panel"]["matchups"].values():
            opp = legend_of(m.get("deck", ""))
            if opp:
                w, g = agg.get(opp, (0.0, 0))
                agg[opp] = (w + m["rate"] * m.get("games", 0), g + m.get("games", 0))
    sims = {k: (w / g, g) for k, (w, g) in agg.items() if g}
    mx, real = real_row(legend)
    out = score(legend, sims, real)
    src = f"{name} specialist's jury, iteration {it.get('iteration')}"
    if len(panels) > 1:     # pooled: the rates are not the newest net's alone
        src = (f"{name} specialist's held-out panel, iterations {panels[0].get('iteration')} to {panels[-1].get('iteration')} "
               f"pooled ({len(panels)} successive nets)")
    out.update({"legend": legend, "source": src, "pooled_nets": len(panels),
                "net": cur, "measured_utc": it.get("started_utc"), "matrix": str(mx.relative_to(ROOT)) if mx else None})
    return out


def from_compare_meta():
    cms = sorted(META.glob("compare_meta-*.json"))
    if not cms:
        return None
    cm = json.loads(cms[-1].read_text())
    rows = cm.get("rows", [])
    legend = rows[0]["legend"] if rows else "Riven"
    sims = {r["opponent_legend"]: (r["sim"], r.get("sim_games", 0)) for r in rows if r.get("sim") is not None}
    real = {r["opponent_legend"]: {"win_rate": r["real"], "games": r["real_games"]} for r in rows if r.get("real") is not None}
    out = score(legend, sims, real)
    out.update({"legend": legend, "source": f"field measure {cms[-1].name}", "net": cm.get("net"),
                "pilots": cm.get("pilots", {}), "measured_utc": cm.get("measured_utc"), "matrix": cm.get("matrix")})
    return out


def all_scores() -> dict:
    out = {"field": from_compare_meta(), "specialists": {}}
    for name in SPEC_LEGEND:
        s = from_specialist(name)
        if s:
            out["specialists"][name] = s
    return out


def main():
    S = all_scores()
    for label, s in [("field", S["field"])] + list(S["specialists"].items()):
        if not s or not s["total"]:
            continue
        print(f"{label:12} {s['legend']:12} {s['right']}/{s['total']} right favourite ({round(100 * s['rate'])}%), "
              f"Spearman {s['spearman']}, net {s['net']} — {s['source']}")
        for w in s["wrong"]:
            print(f"{'':14}wrong: {w}")


if __name__ == "__main__":
    sys.exit(main())
