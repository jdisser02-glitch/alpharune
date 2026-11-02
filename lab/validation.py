"""Independent confirmation and reproducible search identity (standard library only)."""
import hashlib
import json
import math
import re
from pathlib import Path

import riftlab


def as_tuple(value):
    return tuple(as_tuple(v) for v in value) if isinstance(value, list) else value


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def check_opponents(opponents):
    labels = [o["label"] for o in opponents]
    if not labels or len(labels) != len(set(labels)):
        raise ValueError("Opponent labels must be nonempty and unique")
    if any(not math.isfinite(float(o["weight"])) or float(o["weight"]) <= 0 for o in opponents):
        raise ValueError("Opponent weights must be finite and positive")


def fingerprint(spec, agent, opp_agent, opponents):
    """Hash actual executable and network contents, not just mutable filenames."""
    paths = {str(riftlab.ENGINE.resolve()), spec["base"]}
    paths.update(o["deck"] for o in opponents)
    import glob
    for pattern in spec.get("seeds", []) + spec.get("pool", {}).get("from_decks", []):
        paths.update(glob.glob(str(riftlab.ROOT / pattern)))
    for a in (agent, opp_agent):
        paths.update(re.findall(r"(?:eval=)(?:net|policy):([^,]+)", a))
    paths.update(str(Path(__file__).parent / name) for name in
                 ("ga.py", "riftlab.py", "validation.py"))
    files = {str(p): digest(riftlab.ROOT / p) for p in sorted(paths)}
    return {"spec": json.loads(json.dumps(spec)), "agent": agent,
            "opp_agent": opp_agent, "files": files}


def compare(rows, base, opponents, alpha=0.05):
    """Simultaneous Hoeffding bounds, stratified by opponent AND seat.

    Union bound across all lists/strata: valid without assuming independence
    between lists sharing random seeds. Within each stratum games must be
    independent. Conservative; no normal approximation or repeated peeking.
    """
    check_opponents(opponents)
    if not 0 < alpha < 1:
        raise ValueError("alpha must be between 0 and 1")
    strata = 2 * len(opponents)
    terms = len(rows) * strata
    weight_sum = sum(float(o["weight"]) for o in opponents)
    estimates = {}
    for key, results in rows.items():
        rate = radius = 0.
        for o in opponents:
            for seat in (1, 2):
                r = results[f"{o['label']}:{seat}"]
                n, wins = r["games"], r["wins"]
                if n <= 0 or not 0 <= wins <= n:
                    raise ValueError("Incomplete or invalid confirmation results")
                w = float(o["weight"]) / weight_sum / 2
                rate += w * wins / n
                radius += w * math.sqrt(math.log(2 * terms / alpha) / (2 * n))
        estimates[key] = (rate, radius)
    b, br = estimates[base]
    return {k: {"win_rate": rate, "delta": rate - b,
                "delta_interval": [max(-1., rate - b - rad - br),
                                   min(1., rate - b + rad + br)]}
            for k, (rate, rad) in estimates.items() if k != base}


def confirm(ga, population):
    from ga import key_of
    cfg = ga.spec.get("confirmation", {})
    games = int(cfg.get("games", 300))
    count = int(cfg.get("finalists", 3))
    if games < 4 or games % 2 or count < 1:
        raise ValueError("Confirmation requires an even games >= 4 and finalists >= 1")
    base = key_of(dict(ga.base["maindeck"]))
    finalists = sorted((k for k in population if k != base), key=ga.rate, reverse=True)[:count]
    keys = [base] + finalists
    # Above every search batch's entire seed range; engine increments seeds per game.
    max_search = (int(ga.spec.get("seed", 1)) +
                  1000003 * int(ga.spec.get("generations", 8)) +
                  10007 * len(ga.opponents) + 50000 +
                  max(2, int(ga.spec.get("games", 6))))
    seed = max_search + 100000
    if seed <= 0 or seed + 2 * len(ga.opponents) * games >= 0x7FFFFFFF:
        raise ValueError("Seed range would wrap in the engine; use a smaller search seed/budget")
    print(f"Independent confirmation: {len(keys)} lists x {len(ga.opponents)} opponents x {games} games", flush=True)
    rows = {}
    for key in keys:
        rows[key] = {}
        for i, o in enumerate(ga.opponents):
            for seat in (1, 2):
                deck = ga.archive[key]["path"]
                args = (deck, o["deck"], ga.agent, ga.opp_agent) if seat == 1 else (
                    o["deck"], deck, ga.opp_agent, ga.agent)
                result = riftlab.play_batch(*args, games // 2, 1,
                                           seed + (2 * i + seat - 1) * games)
                n = result["p1"] + result["p2"] + result["draws"]
                if n != games // 2:
                    raise ValueError("Confirmation batch returned an unexpected game count")
                rows[key][f"{o['label']}:{seat}"] = {
                    "games": n, "wins": result[f"p{seat}"], "draws": result["draws"]}
    if fingerprint(ga.spec, ga.agent, ga.opp_agent, ga.opponents) != ga.provenance:
        raise ValueError("Inputs changed during search/confirmation; results cannot be combined")
    comparisons = compare(rows, base, ga.opponents)
    passing = [k for k, r in comparisons.items() if r["delta_interval"][0] > 0]
    recommended = max(passing, key=lambda k: comparisons[k]["delta"]) if passing else None
    return {"status": "improvement_demonstrated" if recommended else "inconclusive",
            "recommended": recommended, "base": base, "seed": seed, "games_per_opponent": games,
            "method": "simultaneous_hoeffding_95", "comparisons": comparisons, "results": rows,
            "scope": "Simulation evidence only; engine fidelity and bot strength must be validated separately."}
