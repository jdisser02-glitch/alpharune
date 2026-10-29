#!/usr/bin/env python3
"""riftlab — deck variants, curve statistics and simulation runs for the
Riftbound engine.

Subcommands
  variant  SPEC.json            build a deck variant (base deck + changes) and validate it
  stats    DECK.txt [...]       energy curve, card types, domains, copies
  validate DECK.txt [...]       check deck-construction rules (Python mirror of DeckValidator)
  run      TEST.json            play a test (variants x opponents) and write lab/results/<id>.json

Everything is standard-library Python 3. Card data comes from the engine itself
(`riftbound --dump-registry`), cached in lab/.cache/registry.json and refreshed
whenever the engine binary is newer than the cache.
"""

from __future__ import annotations

import argparse
import collections
import datetime as _dt
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAB = ROOT / "lab"
CACHE = LAB / ".cache"
ENGINE = Path(os.environ.get("RIFTBOUND_BIN", ROOT / "build" / "riftbound"))

SECTIONS = ("legend", "champion", "maindeck", "battlefields", "runes", "sideboard")
SECTION_TITLES = {"legend": "Legend", "champion": "Champion", "maindeck": "MainDeck",
                  "battlefields": "Battlefields", "runes": "Runes", "sideboard": "Sideboard"}


# ─── Card data ───────────────────────────────────────────────────────────────

class Registry:
    """Card table from the engine, indexed by lower-cased name (+ legend aliases)."""

    def __init__(self, cards: list[dict]):
        self.cards = cards
        self.by_name: dict[str, dict] = {}
        for c in cards:
            self.by_name.setdefault(c["name"].lower(), c)
            if c["card_type"] == "legend":
                titles = [c["name"]]
                if c["name"].endswith(" - Starter"):          # Proving Grounds legends
                    titles.append(c["name"][: -len(" - Starter")])
                    self.by_name.setdefault(titles[1].lower(), c)
                for title in titles:
                    for tag in c.get("tags", []):
                        self.by_name.setdefault(f"{tag}, {title}".lower(), c)

    def get(self, name: str) -> dict | None:
        return self.by_name.get(name.lower())

    @staticmethod
    def load() -> "Registry":
        CACHE.mkdir(parents=True, exist_ok=True)
        cache = CACHE / "registry.json"
        if not ENGINE.exists():
            sys.exit(f"engine binary not found: {ENGINE} (build it first)")
        if not cache.exists() or cache.stat().st_mtime < ENGINE.stat().st_mtime:
            subprocess.run([str(ENGINE), "--dump-registry", str(cache)], cwd=ROOT,
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        data = json.loads(cache.read_text())
        cards = data["cards"] if isinstance(data, dict) and "cards" in data else data
        if isinstance(cards, dict):
            cards = list(cards.values())
        return Registry(cards)


# ─── Deck lists (Piltover Archive text format) ───────────────────────────────

def parse_deck(path: Path) -> dict:
    deck = {s: collections.OrderedDict() for s in SECTIONS}
    comments: list[str] = []
    section = None
    for raw in Path(path).read_text().splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            comments.append(line[1:].strip())
            continue
        if line.endswith(":"):
            section = line[:-1].strip().lower()
            if section not in deck:
                raise ValueError(f"{path}: unknown section '{section}'")
            continue
        m = re.match(r"^(\d+)\s+(.+)$", line)
        if not m or section is None:
            raise ValueError(f"{path}: bad line '{line}'")
        n, name = int(m.group(1)), m.group(2).strip()
        deck[section][name] = deck[section].get(name, 0) + n
    deck["comments"] = comments
    return deck


def write_deck(deck: dict, path: Path, header: list[str]) -> None:
    out = [f"# {h}" if h else "#" for h in header]
    for s in SECTIONS:
        if not deck[s]:
            continue
        out.append(f"{SECTION_TITLES[s]}:")
        for name, n in deck[s].items():
            if n > 0:
                out.append(f"{n} {name}")
        out.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(out).rstrip() + "\n")


def apply_changes(deck: dict, changes: dict) -> dict:
    """changes = {"maindeck": {"Astral Heron": -3, "Riven, Deserter": 2},
                  "runes": {...}, "battlefields": {...}, "sideboard": {...},
                  "legend": "name", "champion": "name"}"""
    new = {s: collections.OrderedDict(deck[s]) for s in SECTIONS}
    for s in ("legend", "champion"):
        if s in changes:
            new[s] = collections.OrderedDict({changes[s]: 1})
    for s in ("maindeck", "battlefields", "runes", "sideboard"):
        for name, delta in changes.get(s, {}).items():
            cur = new[s].get(name, 0) + int(delta)
            if cur < 0:
                raise ValueError(f"cannot remove {-int(delta)} '{name}' from {s}: only {new[s].get(name, 0)}")
            if cur == 0:
                new[s].pop(name, None)
            else:
                new[s][name] = cur
    return new


# ─── Rules (mirror of src/rules/deck_validator.cpp) ─────────────────────────

def validate(deck: dict, reg: Registry) -> list[str]:
    errors: list[str] = []

    def card(name: str, where: str) -> dict | None:
        c = reg.get(name)
        if c is None:
            errors.append(f"{where}: unknown card '{name}' (not implemented in the engine)")
        return c

    legend = None
    if sum(deck["legend"].values()) != 1:
        errors.append("exactly one legend required")
    for n in deck["legend"]:
        legend = card(n, "legend")
        if legend and legend["card_type"] != "legend":
            errors.append(f"legend slot holds a {legend['card_type']}: {n}")
    champ = None
    if sum(deck["champion"].values()) != 1:
        errors.append("exactly one chosen champion required")
    for n in deck["champion"]:
        champ = card(n, "champion")
        if champ and (champ["card_type"] != "unit" or champ.get("super_type") != "champion"):
            errors.append(f"champion slot holds a non-champion: {n}")
    if legend and champ and not set(legend.get("tags", [])) & set(champ.get("tags", [])):
        errors.append(f"champion '{champ['name']}' shares no tag with legend '{legend['name']}'")

    main_total = sum(deck["maindeck"].values()) + sum(deck["champion"].values())
    if main_total != 40:
        errors.append(f"main deck must be 40 cards including the champion, got {main_total}")
    if sum(deck["runes"].values()) != 12:
        errors.append(f"rune deck must be 12 runes, got {sum(deck['runes'].values())}")
    if sum(deck["battlefields"].values()) != 3 or any(v > 1 for v in deck["battlefields"].values()):
        errors.append("exactly 3 different battlefields required")
    if sum(deck["sideboard"].values()) > 10:
        errors.append("sideboard holds at most 10 cards")

    domains = set(legend["domains"]) if legend else set()
    copies: collections.Counter = collections.Counter()
    sig = 0
    for s in ("champion", "maindeck", "sideboard"):
        for n, k in deck[s].items():
            c = card(n, s)
            if not c:
                continue
            copies[c["name"]] += k
            if c.get("banned"):
                errors.append(f"{s}: '{n}' is banned")
            if legend and c["domains"] and not set(c["domains"]) <= domains:
                errors.append(f"{s}: '{n}' is outside the legend's domains {sorted(domains)}")
            if c.get("super_type") == "signature":
                sig += k
                if legend and not set(c.get("tags", [])) & set(legend.get("tags", [])):
                    errors.append(f"signature card '{n}' does not match the legend")
    for n, k in copies.items():
        if k > 3:
            errors.append(f"too many copies of '{n}': {k} (max 3)")
    if sig > 3:
        errors.append(f"too many signature cards: {sig} (max 3)")
    for n, k in deck["battlefields"].items():
        c = card(n, "battlefields")
        if c and c.get("banned"):
            errors.append(f"battlefields: '{n}' is banned")
        if c and c["card_type"] != "battlefield":
            errors.append(f"battlefield slot holds a {c['card_type']}: {n}")
    for n, k in deck["runes"].items():
        c = card(n, "runes")
        if c and c["card_type"] != "rune":
            errors.append(f"rune slot holds a {c['card_type']}: {n}")
        elif c and legend and not set(c["domains"]) <= domains:
            errors.append(f"rune '{n}' is outside the legend's domains")
    return errors


# ─── Statistics ──────────────────────────────────────────────────────────────

def stats(deck: dict, reg: Registry) -> dict:
    curve: collections.Counter = collections.Counter()
    types: collections.Counter = collections.Counter()
    power: collections.Counter = collections.Counter()
    keywords: collections.Counter = collections.Counter()
    total = cost_sum = 0
    cards = []
    for s in ("champion", "maindeck"):
        for n, k in deck[s].items():
            c = reg.get(n)
            if not c:
                continue
            e = int(c.get("energy_cost") or 0)
            curve[e] += k
            types[c["card_type"]] += k
            total += k
            cost_sum += e * k
            if c.get("power_cost"):
                for d in c["domains"] or ["any"]:
                    power[d] += k * int(c["power_cost"])
            for kw in ("Hidden", "Accelerate", "Reaction", "Action", "Ganking", "Tank", "Deflect"):
                if f"[{kw}" in (c.get("ability_text") or "") or f"[{kw}" in (c.get("effect_text") or ""):
                    keywords[kw] += k
            cards.append({"name": c["name"], "count": k, "type": c["card_type"], "energy": e,
                          "power": int(c.get("power_cost") or 0), "domains": c["domains"],
                          "might": c.get("might") or 0})
    runes: collections.Counter = collections.Counter()
    for n, k in deck["runes"].items():
        c = reg.get(n)
        for d in (c["domains"] if c else []):
            runes[d] += k
    return {
        "cards": total,
        "avg_energy": round(cost_sum / total, 2) if total else 0,
        "curve": {str(e): curve[e] for e in sorted(curve)},
        "types": dict(types),
        "power_symbols": dict(power),
        "rune_split": dict(runes),
        "keywords": dict(keywords),
        "two_drops": sum(v for e, v in curve.items() if e <= 2),
        "list": sorted(cards, key=lambda x: (x["energy"], x["name"])),
    }


def _slug(s: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")[:60]


def deck_hash(path: Path) -> str:
    return hashlib.sha1(Path(path).read_bytes()).hexdigest()[:10]


# ─── Simulation runs ─────────────────────────────────────────────────────────

RESULT_RE = {k: re.compile(rf"{k}:\s+(\d+)") for k in ("P1 wins", "P2 wins", "Draws")}
AVG_RE = {"turns": re.compile(r"Avg turns:\s+(\d+)"), "decisions": re.compile(r"Avg decisions:\s+(\d+)")}


def wilson(wins: float, n: int, z: float = 1.96) -> tuple[float, float]:
    if n == 0:
        return (0.0, 1.0)
    p = wins / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (max(0.0, c - h), min(1.0, c + h))


def play_batch(deck1: str, deck2: str, agent1: str, agent2: str, games: int,
               threads: int, seed: int, review_dir: str | None = None,
               review_games: int = 0) -> dict:
    cmd = [str(ENGINE), "--deck1", deck1, "--deck2", deck2, "--agent1", agent1,
           "--agent2", agent2, "--games", str(max(games, 2)), "--threads", str(threads),
           "--seed", str(seed), "--render-html", "off", "--web", "off"]
    if review_dir:
        cmd += ["--review-dir", review_dir, "--review-games", str(review_games)]
    t0 = time.time()
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    wall = time.time() - t0
    if p.returncode != 0:
        raise RuntimeError(f"engine failed ({p.returncode}): {' '.join(cmd)}\n{p.stderr[-2000:]}")
    out = p.stdout
    r = {k: int(m.group(1)) if (m := rx.search(out)) else 0 for k, rx in RESULT_RE.items()}
    a = {k: int(m.group(1)) if (m := rx.search(out)) else None for k, rx in AVG_RE.items()}
    return {"p1": r["P1 wins"], "p2": r["P2 wins"], "draws": r["Draws"],
            "avg_turns": a["turns"], "avg_decisions": a["decisions"], "wall_s": round(wall, 2),
            "cmd": " ".join(cmd[1:])}


def git_commit() -> str:
    try:
        sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                             capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--", "src"], cwd=ROOT,
                               capture_output=True, text=True).stdout.strip()
        return sha + ("-dirty" if dirty else "")
    except OSError:
        return "unknown"


def resolve_deck(entry, reg: Registry) -> tuple[str, str]:
    """A test entry is a deck path, or {"variant": spec-path}, or an inline
    {"name", "base", "changes"}. Returns (label, path)."""
    if isinstance(entry, str):
        return (Path(entry).stem, entry)
    if "variant" in entry:
        spec = json.loads((ROOT / entry["variant"]).read_text())
    elif "base" in entry:
        spec = entry
    else:
        return (entry.get("label") or Path(entry["deck"]).stem, entry["deck"])
    path = build_variant(spec, reg)
    return (spec["name"], str(path.relative_to(ROOT)))


def build_variant(spec: dict, reg: Registry) -> Path:
    base = parse_deck(ROOT / spec["base"])
    deck = apply_changes(base, spec.get("changes", {}))
    errors = validate(deck, reg)
    if errors:
        raise ValueError(f"variant '{spec['name']}' is not legal:\n  - " + "\n  - ".join(errors))
    path = LAB / "variants" / f"{spec['name']}.txt"
    header = [f"Variant {spec['name']} — generated by lab/riftlab.py, do not edit by hand.",
              f"Base: {spec['base']}"]
    for s, ch in spec.get("changes", {}).items():
        if isinstance(ch, dict):
            header.append(f"{s}: " + ", ".join(f"{'+' if v > 0 else ''}{v} {k}" for k, v in ch.items()))
        else:
            header.append(f"{s}: {ch}")
    if spec.get("note"):
        header.append(spec["note"])
    write_deck(deck, path, header)
    return path


def run_test(spec: dict, reg: Registry, out_dir: Path) -> Path:
    """spec = {"name", "agent": "ismcts:sims=50", "opp_agent": (default agent),
               "games": N per matchup (split across seats), "threads", "seed",
               "decks": [entry...], "opponents": [{"deck": path, "weight": w, "label"}...]}"""
    agent = spec.get("agent", "random")
    opp_agent = spec.get("opp_agent", agent)
    games = int(spec.get("games", 100))
    review_games = int(spec.get("review_games", 1))
    threads = int(spec.get("threads", os.cpu_count() or 1))
    seed = int(spec.get("seed", 1))
    started = _dt.datetime.now(_dt.timezone.utc)
    result = {
        "id": f"{started.strftime('%Y%m%d-%H%M%S')}-{spec['name']}",
        "name": spec["name"], "description": spec.get("description", ""),
        "started_utc": started.isoformat(timespec="seconds"),
        "engine_commit": git_commit(), "agent": agent, "opp_agent": opp_agent,
        "games_per_matchup": games, "threads": threads, "seed": seed,
        "spec": spec, "decks": [],
    }
    opponents = []
    for o in spec["opponents"]:
        o = {"deck": o} if isinstance(o, str) else dict(o)
        o.setdefault("label", Path(o["deck"]).stem)
        o.setdefault("weight", 1.0)
        opponents.append(o)
    for entry in spec["decks"]:
        label, path = resolve_deck(entry, reg)
        d = parse_deck(ROOT / path)
        errors = validate(d, reg)
        drec = {"label": label, "path": path, "hash": deck_hash(ROOT / path),
                "stats": stats(d, reg), "errors": errors, "matchups": []}
        if errors:
            result["decks"].append(drec)
            continue
        wsum = wwin = 0.0
        for i, o in enumerate(opponents):
            half = games // 2
            s1 = seed + 100000 * i
            rdir = LAB / "replays" / result["id"] / f"{_slug(label)}__vs__{_slug(o['label'])}"
            a = play_batch(path, o["deck"], agent, opp_agent, half, threads, s1,
                           str((rdir / "seat1").relative_to(ROOT)), review_games)
            b = play_batch(o["deck"], path, opp_agent, agent, games - half, threads, s1 + 50000,
                           str((rdir / "seat2").relative_to(ROOT)), review_games)
            wins = a["p1"] + b["p2"]
            n = a["p1"] + a["p2"] + a["draws"] + b["p1"] + b["p2"] + b["draws"]
            lo, hi = wilson(wins, n)
            m = {"opponent": o["label"], "opponent_deck": o["deck"], "weight": o["weight"],
                 "games": n, "wins": wins, "win_rate": round(wins / n, 4) if n else None,
                 "ci95": [round(lo, 4), round(hi, 4)],
                 "as_p1": {"games": a["p1"] + a["p2"] + a["draws"], "wins": a["p1"]},
                 "as_p2": {"games": b["p1"] + b["p2"] + b["draws"], "wins": b["p2"]},
                 "avg_turns": a["avg_turns"], "wall_s": round(a["wall_s"] + b["wall_s"], 1),
                 "replays": str(rdir.relative_to(ROOT)),
                 "commands": [a["cmd"], b["cmd"]]}
            drec["matchups"].append(m)
            wsum += o["weight"]
            wwin += o["weight"] * (wins / n if n else 0)
            print(f"  {label:>28} vs {o['label']:<24} {wins:>4}/{n:<4} "
                  f"{100 * wins / max(n, 1):5.1f}%  [{100 * lo:4.1f}-{100 * hi:4.1f}]  {m['wall_s']}s",
                  flush=True)
        drec["weighted_win_rate"] = round(wwin / wsum, 4) if wsum else None
        result["decks"].append(drec)
    result["finished_utc"] = _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds")
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{result['id']}.json"
    path.write_text(json.dumps(result, indent=1))
    return path


# ─── Index for the dashboard ─────────────────────────────────────────────────

def write_index(reg: Registry) -> Path:
    """lab/index.json: every test result (summary), every deck the lab knows
    (with its stats), every variant spec. lab/cards.json: the card table."""
    tests = []
    for f in sorted((LAB / "results").glob("*.json"), reverse=True):
        try:
            r = json.loads(f.read_text())
        except json.JSONDecodeError:
            continue
        tests.append({
            "id": r["id"], "file": str(f.relative_to(ROOT)), "name": r["name"],
            "description": r.get("description", ""), "started_utc": r.get("started_utc"),
            "finished_utc": r.get("finished_utc"), "engine_commit": r.get("engine_commit"),
            "agent": r.get("agent"), "opp_agent": r.get("opp_agent"),
            "games_per_matchup": r.get("games_per_matchup"),
            "decks": [{"label": d["label"], "path": d["path"],
                       "weighted_win_rate": d.get("weighted_win_rate"),
                       "games": sum(m["games"] for m in d.get("matchups", [])),
                       "errors": d.get("errors", [])} for d in r["decks"]],
            "opponents": sorted({m["opponent"] for d in r["decks"] for m in d.get("matchups", [])}),
        })
    decks = []
    paths = sorted((ROOT / "decks").glob("*.txt")) + sorted((LAB / "decks").rglob("*.txt")) \
        + sorted((LAB / "variants").glob("*.txt"))
    for p in paths:
        try:
            d = parse_deck(p)
        except ValueError as e:
            decks.append({"path": str(p.relative_to(ROOT)), "errors": [str(e)]})
            continue
        legend = next(iter(d["legend"]), "")
        st = stats(d, reg)
        decks.append({"path": str(p.relative_to(ROOT)), "legend": legend,
                      "champion": next(iter(d["champion"]), ""),
                      "comments": d.get("comments", [])[:6],
                      "errors": validate(d, reg),
                      "stats": {k: st[k] for k in ("cards", "avg_energy", "curve", "types",
                                                    "rune_split", "two_drops")},
                      "list": {s: d[s] for s in SECTIONS if d[s]}})
    variants = []
    for f in sorted((LAB / "specs").rglob("*.json")):
        try:
            spec = json.loads(f.read_text())
        except json.JSONDecodeError:
            continue
        if "base" in spec:
            variants.append({"spec": str(f.relative_to(ROOT)), **spec})
    index = {"generated_utc": _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds"),
             "engine_commit": git_commit(), "tests": tests, "decks": decks, "variants": variants}
    out = LAB / "index.json"
    out.write_text(json.dumps(index, indent=0))
    cards = [{"name": c["name"], "type": c["card_type"], "super": c.get("super_type"),
              "domains": c["domains"], "energy": c.get("energy_cost") or 0,
              "power": c.get("power_cost") or 0, "might": c.get("might") or 0,
              "tags": c.get("tags", []), "set": c.get("set"), "code": c.get("public_code"),
              "text": c.get("ability_text", "")} for c in reg.cards]
    (LAB / "cards.json").write_text(json.dumps(cards, separators=(",", ":")))
    return out


# ─── CLI ─────────────────────────────────────────────────────────────────────

def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("variant"); v.add_argument("spec")
    s = sub.add_parser("stats"); s.add_argument("decks", nargs="+")
    c = sub.add_parser("validate"); c.add_argument("decks", nargs="+")
    sub.add_parser("index")
    r = sub.add_parser("run"); r.add_argument("spec")
    r.add_argument("--out", default=str(LAB / "results"))
    r.add_argument("--games", type=int); r.add_argument("--threads", type=int)
    r.add_argument("--agent")
    args = ap.parse_args(argv)
    reg = Registry.load()

    if args.cmd == "variant":
        path = build_variant(json.loads(Path(args.spec).read_text()), reg)
        print(path.relative_to(ROOT))
        print(json.dumps(stats(parse_deck(path), reg), indent=1))
    elif args.cmd == "stats":
        for d in args.decks:
            print(json.dumps({"deck": d, **stats(parse_deck(Path(d)), reg)}, indent=1))
    elif args.cmd == "validate":
        bad = 0
        for d in args.decks:
            errs = validate(parse_deck(Path(d)), reg)
            print(f"{d}: {'OK' if not errs else 'INVALID'}")
            for e in errs:
                print(f"  - {e}")
            bad += bool(errs)
        return 1 if bad else 0
    elif args.cmd == "run":
        spec = json.loads(Path(args.spec).read_text())
        for k in ("games", "threads", "agent"):
            if getattr(args, k) is not None:
                spec[k] = getattr(args, k)
        print(run_test(spec, reg, Path(args.out)).relative_to(ROOT))
        write_index(reg)
    elif args.cmd == "index":
        print(write_index(reg).relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
