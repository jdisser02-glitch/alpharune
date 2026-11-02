#!/usr/bin/env python3
"""ga — evolve a better list with a genetic algorithm, the bot as the judge.

A population of main-deck lists (legend, champion, battlefields and runes stay
those of the base) plays the gauntlet every generation; the best lists survive
unchanged (elites), the rest are replaced by children: a crossover of two
parents picked by tournament, repaired to 40 cards and mutated by one or two
swaps drawn from the card pool. Every list of a generation plays the same seeds
against the same opponents (common random numbers), so the ranking reflects the
lists, not the deals. A list that survives keeps accumulating games across
generations, so the evidence grows where it matters.

The pool is the cards of the base plus the main decks named in "pool.from_decks"
(other lists of the same legend, by default) plus "pool.extra", minus
"pool.exclude"; only swaps that keep the deck legal are kept.

Choosing the best list. Rates measured during the generations are biased
toward lists that were lucky on few games (a list eliminated after one
generation can top the raw ranking). So when the last generation is done, the
"finalists" lists with the highest lower bound of their weighted win rate play
"confirm_games" new games per opponent on seeds no generation used, alongside
the base list (the reference) on the same seeds. A finalist is recommended
only if its confirmed rate beats the reference's with a 95% interval of the
difference above zero; otherwise the reference stays the recommendation. Only
that confirmed choice is written to lab/variants/<name>-best.txt; until then
the running leader goes to <name>-leader.txt.

Resuming. The state file records a fingerprint of what the scores depend on:
the engine sources (last commit touching src/, and whether src/ has local
changes), the agent specs, a hash of every network they name, the base and
every opponent deck and its weight. --resume refuses a state whose fingerprint
differs from the current one and lists the differences; --rescore keeps the
population but drops every score, so the lists are measured again from zero.

usage: ga.py SPEC.json [--dry-run] [--resume STATE.json [--rescore]]

spec = {
  "name": "riven-meta-ga-1",
  "base": "decks/riven_sacramento.txt",
  "agent": "pmcts:sims=30,eval=policy:lab/learn/nets/net_022.rbpn,worlds=6",
  "opp_agent": "...",                      # default: agent
  "opponents": [{"deck": "...", "label": "...", "weight": 2}, ...],
  "pool": {"from_decks": ["lab/decks/meta/*/*riven*.txt"], "extra": [], "exclude": []},
  "seeds": ["lab/decks/meta/*/*riven*.txt"],     # other lists that start in the population (optional)
  "population": 16, "generations": 8, "elite": 4,
  "games": 6,                              # per list per opponent per generation (half per seat)
  "swaps": [1, 2], "crossover": 0.6, "max_copies": 3,
  "threads": 4, "seed": 11,
  "finalists": 4, "confirm_games": 40      # final confirmation (per opponent, half per seat)
}
The result is a lab test file (lab/results/<id>.json, kind "ga") with every
list evaluated and a "ga" block with the generations; the best list is also
written to lab/variants/<name>-best.txt.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures as cf
import datetime as dt
import glob
import hashlib
import json
import os
import math
import random
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import riftlab  # noqa: E402
from riftlab import LAB, ROOT  # noqa: E402


def key_of(main: dict) -> str:
    return hashlib.sha1(json.dumps(sorted((k, v) for k, v in main.items() if v > 0)).encode()).hexdigest()[:10]


def build_pool(spec: dict, base: dict, reg: riftlab.Registry) -> list[str]:
    pool_spec = spec.get("pool", {})
    names = set(base["maindeck"])
    for pat in pool_spec.get("from_decks", []):
        for f in glob.glob(str(ROOT / pat)):
            try:
                names |= set(riftlab.parse_deck(Path(f))["maindeck"])
            except Exception as e:  # noqa: BLE001
                print(f"pool: skipped {f}: {e}", file=sys.stderr)
    names |= set(pool_spec.get("extra", []))
    names -= set(pool_spec.get("exclude", []))
    champion = next(iter(base["champion"]))
    out = []
    for n in sorted(names):
        if n == champion:
            continue
        c = reg.get(n)
        if not c or c.get("banned"):
            continue
        # one copy must be legal in the base deck (domain, signature rules)
        trial = dict(base)
        trial["maindeck"] = collections.OrderedDict(base["maindeck"])
        trial["maindeck"][n] = trial["maindeck"].get(n, 0) + 1
        first = next(iter(trial["maindeck"]))
        if first != n:
            trial["maindeck"][first] -= 1
            if trial["maindeck"][first] == 0:
                del trial["maindeck"][first]
        if not [e for e in riftlab.validate(trial, reg) if "outside" in e or "signature" in e or "banned" in e or "unknown" in e]:
            out.append(n)
    return out


def file_hash(path: Path) -> str:
    try:
        return hashlib.sha1(Path(path).read_bytes()).hexdigest()[:12]
    except OSError:
        return "missing"


def engine_source() -> str:
    """The last commit that touched src/, plus "-dirty" when src/ has local changes."""
    try:
        sha = subprocess.run(["git", "log", "-1", "--format=%h", "--", "src"], cwd=ROOT,
                             capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--", "src"], cwd=ROOT,
                               capture_output=True, text=True).stdout.strip()
        return (sha or "unknown") + ("-dirty" if dirty else "")
    except OSError:
        return "unknown"


def fingerprint_diff(old: dict | None, new: dict) -> list[str]:
    if not old:
        return ["the state has no fingerprint (written before fingerprints were recorded)"]
    out = []
    for k in sorted(set(old) | set(new)):
        if old.get(k) != new.get(k):
            out.append(f"{k}: {json.dumps(old.get(k))} -> {json.dumps(new.get(k))}")
    return out


def weighted(results: dict, opponents: list[dict]) -> tuple[float, float]:
    """Weighted win rate over the opponents and its standard error."""
    ws = sum(o["weight"] for o in opponents)
    rate = var = 0.0
    for o in opponents:
        r = results[o["label"]]
        n = max(1, r["games"])
        p = r["wins"] / n
        rate += o["weight"] * p / ws
        # a matchup with no games counts as fully uncertain
        var += (o["weight"] / ws) ** 2 * (max(p * (1 - p), 0.25 / n) / n if r["games"] else 0.25)
    return rate, math.sqrt(var)


class GA:
    def __init__(self, spec: dict, reg: riftlab.Registry, dry: bool):
        self.spec, self.reg, self.dry = spec, reg, dry
        self.base = riftlab.parse_deck(ROOT / spec["base"])
        self.pool = build_pool(spec, self.base, reg)
        self.rng = random.Random(int(spec.get("seed", 1)))
        self.max_copies = int(spec.get("max_copies", 3))
        self.agent = riftlab.resolve_agent(spec.get("agent", "random"))
        self.opp_agent = riftlab.resolve_agent(spec.get("opp_agent", self.agent))
        self.threads = int(spec.get("threads", os.cpu_count() or 1))
        self.opponents = []
        for o in spec["opponents"]:
            o = {"deck": o} if isinstance(o, str) else dict(o)
            o.setdefault("label", Path(o["deck"]).stem)
            o.setdefault("weight", 1.0)
            self.opponents.append(o)
        self.archive: dict[str, dict] = {}     # key -> {main, label, path, results{label:{games,wins}}, born}
        self.log: list[dict] = []
        self.confirmation_rounds: list[dict] = []
        self.rescored: dict | None = None
        self.started = dt.datetime.now(dt.timezone.utc)
        self.test_id = f"{self.started.strftime('%Y%m%d-%H%M%S')}-{spec['name']}"

    # ── lists ────────────────────────────────────────────────────────────────
    def legal(self, main: dict) -> bool:
        d = dict(self.base)
        d["maindeck"] = collections.OrderedDict((k, v) for k, v in main.items() if v > 0)
        return not riftlab.validate(d, self.reg)

    def swap(self, main: dict) -> dict | None:
        """One card out, one card in (a different name), within the copy limit."""
        m = dict(main)
        outs = [k for k, v in m.items() if v > 0]
        ins = [k for k in self.pool if m.get(k, 0) < self.max_copies]
        for _ in range(40):
            o, i = self.rng.choice(outs), self.rng.choice(ins)
            if o == i:
                continue
            n = dict(m); n[o] -= 1; n[i] = n.get(i, 0) + 1
            n = {k: v for k, v in n.items() if v > 0}
            if self.legal(n):
                return n
        return None

    def mutate(self, main: dict) -> dict:
        lo, hi = self.spec.get("swaps", [1, 2])
        n = dict(main)
        for _ in range(self.rng.randint(lo, hi)):
            s = self.swap(n)
            if s:
                n = s
        return n

    def crossover(self, a: dict, b: dict) -> dict:
        names = set(a) | set(b)
        child = {}
        for n in names:
            child[n] = self.rng.choice([a.get(n, 0), b.get(n, 0)])
        child = {k: v for k, v in child.items() if v > 0}
        target = sum(a.values())
        # repair to the deck size: trim the most-copied, fill from the parents' cards
        for _ in range(80):
            total = sum(child.values())
            if total == target:
                break
            if total > target:
                k = self.rng.choice([k for k, v in child.items() if v > 0])
                child[k] -= 1
                if child[k] == 0:
                    del child[k]
            else:
                k = self.rng.choice([k for k in names if child.get(k, 0) < self.max_copies])
                child[k] = child.get(k, 0) + 1
        return child if sum(child.values()) == target and self.legal(child) else dict(a)

    def describe(self, main: dict) -> str:
        base = self.base["maindeck"]
        out = [f"-{base[k] - main.get(k, 0)} {k}" for k in base if main.get(k, 0) < base[k]]
        inn = [f"+{main[k] - base.get(k, 0)} {k}" for k in main if main[k] > base.get(k, 0)]
        return ", ".join(out + inn) or "base (control)"

    def register(self, main: dict, gen: int) -> str:
        k = key_of(main)
        if k in self.archive:
            return k
        label = self.describe(main)
        changes = {"maindeck": {}}
        for n in set(main) | set(self.base["maindeck"]):
            d = main.get(n, 0) - self.base["maindeck"].get(n, 0)
            if d:
                changes["maindeck"][n] = d
        name = f"{self.spec['name']}--{k}"
        path = riftlab.build_variant({"name": name, "base": self.spec["base"], "changes": changes,
                                      "note": f"ga {self.spec['name']} generation {gen}: {label}"}, self.reg)
        self.archive[k] = {"main": main, "label": label, "path": str(path.relative_to(ROOT)), "changes": changes,
                           "results": {o["label"]: {"games": 0, "wins": 0} for o in self.opponents}, "born": gen,
                           "generations": []}
        return k

    # ── evaluation ───────────────────────────────────────────────────────────
    def rate(self, k: str) -> float:
        r = self.archive[k]["results"]
        ws = sum(o["weight"] for o in self.opponents)
        return sum(o["weight"] * (r[o["label"]]["wins"] / max(1, r[o["label"]]["games"])) for o in self.opponents) / ws

    def games_of(self, k: str) -> int:
        return sum(v["games"] for v in self.archive[k]["results"].values())

    def lower(self, k: str, field: str = "results") -> float:
        """Lower end of the 95% interval of the weighted win rate."""
        rate, se = weighted(self.archive[k][field], self.opponents)
        return rate - 1.96 * se

    def fingerprint(self) -> dict:
        nets = sorted(set(re.findall(r"eval=(?:policy|net):([^,]+)", f"{self.agent} {self.opp_agent}")))
        return {"engine_src": engine_source(), "agent": self.agent, "opp_agent": self.opp_agent,
                "nets": {n: file_hash(ROOT / n) for n in nets},
                "base": [self.spec["base"], riftlab.deck_hash(ROOT / self.spec["base"])],
                "opponents": [[o["label"], o["deck"], riftlab.deck_hash(ROOT / o["deck"]), o["weight"]] for o in self.opponents]}

    def evaluate(self, keys: list[str], gen: int, games: int, field: str = "results"):
        """Every list in `keys` plays `games` per opponent on the seeds of
        `gen` (the same for every list: common random numbers). Generation g
        uses seeds from seed + 1000003 g; the confirmation uses g = 1000 + round (no
        search reaches 1000 generations; 1000003 * 1000 stays within 31-bit seeds)."""
        seed = int(self.spec.get("seed", 1))
        jobs = []
        for k in keys:
            c = self.archive[k]
            c.setdefault(field, {o["label"]: {"games": 0, "wins": 0} for o in self.opponents})
            for i, o in enumerate(self.opponents):
                s1 = seed + 1000003 * gen + 10007 * i       # same seeds for every list of the generation
                half = max(1, games // 2)
                jobs.append((k, o, 1, (c["path"], o["deck"], self.agent, self.opp_agent, half, 1, s1)))
                jobs.append((k, o, 2, (o["deck"], c["path"], self.opp_agent, self.agent, max(1, games - half), 1, s1 + 5003)))
        with cf.ThreadPoolExecutor(max_workers=self.threads) as pool:
            futs = {pool.submit(riftlab.play_batch, *args): (k, o, seat) for k, o, seat, args in jobs}
            for f in cf.as_completed(futs):
                k, o, seat = futs[f]
                x = f.result()
                res = self.archive[k][field][o["label"]]
                res["games"] += x["p1"] + x["p2"] + x["draws"]
                res["wins"] += x["p1"] if seat == 1 else x["p2"]
        if field == "results":
            for k in keys:
                self.archive[k]["generations"].append(gen)

    def confirm(self) -> dict:
        """Final confirmation: the finalists (highest lower bounds) and the base
        play fresh seeds; the recommendation needs an interval above the base."""
        F = int(self.spec.get("finalists", 4)); games = int(self.spec.get("confirm_games", 40))
        base_key = key_of(dict(self.base["maindeck"]))
        if base_key not in self.archive:
            self.register(dict(self.base["maindeck"]), 0)
        measured = [k for k in self.archive if self.games_of(k) > 0 and k != base_key]
        finalists = sorted(measured, key=self.lower, reverse=True)[:F]
        print(f"confirmation: {len(finalists)} finalists and the reference, {games} new games per opponent", flush=True)
        self.evaluate(finalists + [base_key], 1000 + len(self.confirmation_rounds), games, field="confirm")
        b_rate, b_se = weighted(self.archive[base_key]["confirm"], self.opponents)
        rows = []
        for k in finalists:
            rate, se = weighted(self.archive[k]["confirm"], self.opponents)
            d = rate - b_rate
            dse = math.sqrt(se * se + b_se * b_se)
            rows.append({"key": k, "label": self.archive[k]["label"], "path": self.archive[k]["path"],
                         "search_rate": round(self.rate(k), 4), "search_games": self.games_of(k),
                         "confirmed_rate": round(rate, 4), "diff_vs_reference": round(d, 4),
                         "diff_ci95": [round(d - 1.96 * dse, 4), round(d + 1.96 * dse, 4)],
                         "verdict": "better" if d - 1.96 * dse > 0 else ("worse" if d + 1.96 * dse < 0 else "tie")})
            print(f"  {100 * rate:5.1f}% vs reference {100 * b_rate:5.1f}%  diff {100 * d:+5.1f} "
                  f"[{100 * (d - 1.96 * dse):+5.1f}, {100 * (d + 1.96 * dse):+5.1f}]  {self.archive[k]['label']}", flush=True)
        better = [r for r in rows if r["verdict"] == "better"]
        best = max(better, key=lambda r: r["confirmed_rate"]) if better else None
        out = {"games_per_opponent": games, "reference": {"key": base_key, "path": self.archive[base_key]["path"],
                                                           "confirmed_rate": round(b_rate, 4)},
               "finalists": rows, "best": best["key"] if best else base_key,
               "verdict": "a finalist beats the reference" if best else "no finalist beats the reference"}
        self.confirmation_rounds.append(out)
        return out

    # ── the loop ─────────────────────────────────────────────────────────────
    def run(self, state: dict | None = None, rescore: bool = False):
        P = int(self.spec.get("population", 16)); G = int(self.spec.get("generations", 8))
        E = int(self.spec.get("elite", max(2, P // 4))); games = int(self.spec.get("games", 6))
        px = float(self.spec.get("crossover", 0.6))
        if state:
            diff = fingerprint_diff(state.get("fingerprint"), self.fingerprint())
            if diff and not rescore:
                raise SystemExit("--resume refused: the scores in this state were measured under other conditions:\n  "
                                 + "\n  ".join(diff) + "\nrerun with --rescore to keep the population and measure it again")
            self.archive = state["archive"]; self.log = state["log"]; self.test_id = state["test_id"]
            self.confirmation_rounds = state.get("confirmation", [])
            self.rescored = state.get("rescored")
            self.started = dt.datetime.fromisoformat(state["started"]); population = state["population"]
            if diff:
                print("--rescore: scores dropped, the population is measured again:\n  " + "\n  ".join(diff), flush=True)
                for c in self.archive.values():
                    c["results"] = {o["label"]: {"games": 0, "wins": 0} for o in self.opponents}
                    c["generations"] = []
                    c.pop("confirm", None)
                self.confirmation_rounds = []
                # the generations start again from 1: earlier standings were measured under the old conditions
                self.rescored = {"after_generations": len(self.log), "differences": diff}
                self.log = []
            self.rng = random.Random(int(self.spec.get("seed", 1)) + 7919 * len(self.log))
            start = len(self.log)
            print(f"resuming {self.test_id} at generation {start + 1}", flush=True)
        else:
            population = [self.register(dict(self.base["maindeck"]), 0)]
            # "seeds": other lists whose main decks start in the population (the base's legend, champion,
            # battlefields and runes are kept); a seed that is not legal in that frame is skipped.
            for pat in self.spec.get("seeds", []):
                for f in sorted(glob.glob(str(ROOT / pat))):
                    try:
                        m = dict(riftlab.parse_deck(Path(f))["maindeck"])
                    except Exception as e:  # noqa: BLE001
                        print(f"seed skipped {f}: {e}", file=sys.stderr); continue
                    if sum(m.values()) != sum(self.base["maindeck"].values()) or not self.legal(m):
                        print(f"seed skipped {Path(f).name}: not legal in the base's frame", file=sys.stderr); continue
                    k = self.register(m, 0)
                    if k not in population and len(population) < P:
                        population.append(k); self.archive[k]["label"] = f"{Path(f).stem}: " + self.archive[k]["label"]
            while len(population) < P:
                m = self.mutate(self.base["maindeck"])
                k = self.register(m, 0)
                if k not in population:
                    population.append(k)
            start = 0
        budget = P * games * len(self.opponents) * (G - start)
        print(f"{self.spec['name']}: pool of {len(self.pool)} cards, population {P}, elite {E}, {G} generations, "
              f"{games} games per list per opponent, {len(self.opponents)} opponents: at most {budget} games", flush=True)
        for k in population:
            print(f"  {self.archive[k]['label']}")
        if self.dry:
            return
        for gen in range(start, G):
            t0 = dt.datetime.now()
            self.evaluate(population, gen + 1, games)
            ranked = sorted(population, key=self.rate, reverse=True)
            print(f"generation {gen + 1}  ({(dt.datetime.now() - t0).total_seconds():.0f}s)", flush=True)
            for k in ranked:
                print(f"  {100 * self.rate(k):5.1f}%  ({self.games_of(k):3d} games)  {self.archive[k]['label']}", flush=True)
            self.log.append({"generation": gen + 1, "games_per_opponent": games,
                             "standings": [{"key": k, "label": self.archive[k]["label"], "rate": round(self.rate(k), 4),
                                            "games": self.games_of(k)} for k in ranked]})
            if gen + 1 == G:
                if int(self.spec.get("finalists", 4)) > 0:
                    self.confirm()
                self.write(population, complete=True)
                break
            self.write(population, complete=False)
            # next generation: elites stay, children replace the rest; the base list stays as the control
            # whatever its rank, so its evidence keeps growing alongside the survivors'
            nxt = ranked[:E]
            base_key = key_of(dict(self.base["maindeck"]))
            if base_key in self.archive and base_key not in nxt:
                nxt = nxt[:-1] + [base_key] if len(nxt) >= E else nxt + [base_key]
            tries = 0
            while len(nxt) < P and tries < 200:
                tries += 1
                a = self.tournament(ranked); b = self.tournament(ranked)
                child = self.crossover(self.archive[a]["main"], self.archive[b]["main"]) if self.rng.random() < px else dict(self.archive[a]["main"])
                child = self.mutate(child)
                if not self.legal(child):
                    continue
                k = self.register(child, gen + 1)
                if k not in nxt:
                    nxt.append(k)
            population = nxt

    def tournament(self, ranked: list[str], size: int = 3) -> str:
        picks = self.rng.sample(ranked, min(size, len(ranked)))
        return max(picks, key=self.rate)

    # ── output ───────────────────────────────────────────────────────────────
    def write(self, population: list[str], complete: bool):
        result = {"id": self.test_id, "name": self.spec["name"], "kind": "ga",
                  "description": self.spec.get("description", f"Genetic search of lists around {self.spec['base']}: {self.spec.get('population', 16)} lists per generation, the bot as the judge."),
                  "started_utc": self.started.isoformat(timespec="seconds"),
                  "finished_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
                  "engine_commit": riftlab.git_commit(), "agent": self.agent, "opp_agent": self.opp_agent,
                  "games_per_matchup": int(self.spec.get("games", 6)), "threads": self.threads, "seed": int(self.spec.get("seed", 1)),
                  "spec": self.spec, "fingerprint": self.fingerprint(), "rescored": self.rescored,
                  "ga": {"generations": self.log, "complete": complete, "population": population,
                         "pool": self.pool, "confirmation": self.confirmation_rounds}, "decks": []}
        keys = sorted(self.archive, key=lambda k: (self.games_of(k) > 0, self.lower(k) if self.games_of(k) else 0), reverse=True)
        for k in keys:
            c = self.archive[k]
            if self.games_of(k) == 0:
                continue
            d = riftlab.parse_deck(ROOT / c["path"])
            drec = {"label": c["label"], "path": c["path"], "hash": riftlab.deck_hash(ROOT / c["path"]),
                    "stats": riftlab.stats(d, self.reg), "errors": [], "matchups": [], "changes": c["changes"],
                    "born": c["born"], "generations": c["generations"], "alive": k in population}
            for o in self.opponents:
                res = c["results"][o["label"]]
                lo, hi = riftlab.wilson(res["wins"], res["games"])
                drec["matchups"].append({"opponent": o["label"], "opponent_deck": o["deck"], "weight": o["weight"],
                                         "games": res["games"], "wins": res["wins"],
                                         "win_rate": round(res["wins"] / res["games"], 4) if res["games"] else None,
                                         "ci95": [round(lo, 4), round(hi, 4)]})
            drec["weighted_win_rate"] = round(self.rate(k), 4)
            drec["weighted_lower95"] = round(self.lower(k), 4)
            if "confirm" in c:
                drec["confirmed_win_rate"] = round(weighted(c["confirm"], self.opponents)[0], 4)
                drec["confirmed_games"] = sum(v["games"] for v in c["confirm"].values())
            result["decks"].append(drec)
        out = LAB / "results" / f"{self.test_id}.json"
        out.write_text(json.dumps(result, indent=1))
        state = {"test_id": self.test_id, "started": self.started.isoformat(), "archive": self.archive, "log": self.log,
                 "population": population, "rng": None, "fingerprint": self.fingerprint(),
                 "confirmation": self.confirmation_rounds, "rescored": self.rescored}
        (LAB / ".cache" / "ga").mkdir(parents=True, exist_ok=True)
        (LAB / ".cache" / "ga" / f"{self.test_id}.json").write_text(json.dumps(state))   # for --resume
        # the running leader (highest lower bound, not the raw rate) while the search runs;
        # the recommendation only from the final confirmation
        leader = max((k for k in keys if self.games_of(k) > 0), key=self.lower, default=None)
        if leader:
            d = riftlab.parse_deck(ROOT / self.archive[leader]["path"])
            riftlab.write_deck(d, LAB / "variants" / f"{self.spec['name']}-leader.txt",
                               [f"Leader of {self.spec['name']}, NOT confirmed ({100 * self.rate(leader):.1f}% over {self.games_of(leader)} games, "
                                f"lower bound {100 * self.lower(leader):.1f}%): {self.archive[leader]['label']}",
                                f"Base: {self.spec['base']} — generated by lab/ga.py"])
        if complete and self.confirmation_rounds:
            conf = self.confirmation_rounds[-1]
            best = conf["best"]
            row = next((r for r in conf["finalists"] if r["key"] == best), None)
            d = riftlab.parse_deck(ROOT / self.archive[best]["path"])
            why = (f"confirmed {100 * row['confirmed_rate']:.1f}% vs reference {100 * conf['reference']['confirmed_rate']:.1f}% "
                   f"on {conf['games_per_opponent']} new games per opponent, difference "
                   f"[{100 * row['diff_ci95'][0]:+.1f}, {100 * row['diff_ci95'][1]:+.1f}]") if row else \
                  "no finalist beat the reference on the confirmation games: the reference stays"
            riftlab.write_deck(d, LAB / "variants" / f"{self.spec['name']}-best.txt",
                               [f"Best list of {self.spec['name']}: {self.archive[best]['label']} ({why})",
                                f"Base: {self.spec['base']} — generated by lab/ga.py"])
        riftlab.write_index(self.reg)
        print(out.relative_to(ROOT), flush=True)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--resume", help="the state file lab/.cache/ga/<id>.json written by an earlier run of the same spec")
    ap.add_argument("--rescore", action="store_true", help="with --resume: when the fingerprint differs, keep the population and drop its scores")
    a = ap.parse_args(argv)
    spec = json.loads(Path(a.spec).read_text())
    reg = riftlab.Registry.load()
    ga = GA(spec, reg, a.dry_run)
    state = json.loads(Path(a.resume).read_text()) if a.resume else None
    ga.run(state, rescore=a.rescore)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
