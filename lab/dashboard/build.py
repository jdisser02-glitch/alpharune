#!/usr/bin/env python3
"""Assemble the lab dashboard's data files into lab/dashboard/dist/.

dist/index.html     the app shell (copied from lab/dashboard/ with app.css, board.js, board.css, engine.worker.js)
dist/app/*.js       the view modules and shared library (lab/dashboard/app/, copied as is)
dist/home.json      the Home screen's editable copy, from lab/dashboard/home.yml (or home.json)
dist/data.json      tests (full results), decks with stats, variants, learning ladder
dist/replays/*.json game records referenced by data.json (the most recent ones)
dist/art/*.webp     card art sprite sheets (lab/dashboard/art/, built by art.py); their map art.json travels inside data.json as data.art

usage: build.py [--max-replays 40]
"""
import argparse
import hashlib
import datetime as dt
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "lab"
DIST = LAB / "dashboard" / "dist"
sys.path.insert(0, str(LAB))
import riftlab  # noqa: E402

PAGE_FILES = ["index.html", "app.css", "board.js", "board.css", "engine.worker.js"]   # shell, design system, board renderer, engine worker: copied as is
APP_DIR = LAB / "dashboard" / "app"                                                   # view modules + lib (ES modules), copied as a directory
ART_DIR = LAB / "dashboard" / "art"                                                   # sprite sheets + art.json (art.py), copied as a directory
# The WebAssembly engine for the Play tab (built with -DRIFTBOUND_WASM into build-wasm/) and the value net the
# in-browser bot evaluates with. Copied into dist/engine/ when present; the page hides the Play tab otherwise.
ENGINE_FILES = {"engine/riftbound.js": ROOT / "build-wasm" / "riftbound.js",
                "engine/riftbound.wasm": ROOT / "build-wasm" / "riftbound.wasm"}
DEFAULT_NET = LAB / "learn" / "nets" / "net_000.rbvn"
# Legends whose specialist net the Play bot uses (the active tracks and the Stuttgart field's top legends).
PLAY_SPECIALISTS = ["riven", "volibear", "orianna", "mordekaiser", "kennen", "kai_sa"]
SESSION = {"session_id": "session_01F68srW6k6CBYFTm9oVwNVu",
           "environment_id": "env_01TgKU1MBDD1jDjD7f3iW6Gm",
           "repo": "jdisser02-glitch/alpharune", "branch": "claude/zen-goodall-hb2ssu"}


def load_home():
    """Home screen copy: lab/dashboard/home.yml (needs PyYAML) or home.json; {} when neither exists."""
    y, j = LAB / "dashboard" / "home.yml", LAB / "dashboard" / "home.json"
    if y.exists():
        try:
            import yaml
            return yaml.safe_load(y.read_text()) or {}
        except ImportError:
            print("  PyYAML is not installed: home.yml ignored" + (", using home.json" if j.exists() else ""))
    return json.loads(j.read_text()) if j.exists() else {}


def net_of(agent: str) -> str:
    """Net file named in an agent spec ('...eval=policy:lab/learn/nets/net_128.rbpn,...' -> 'net_128'), or ''."""
    m = re.search(r"eval=(?:policy|net):([^,]+)", agent or "")
    return Path(m.group(1)).stem if m else ""


def merged_parts():
    """Result ids that a merged result was put together from (lab/tools/merge_results.py: one request per opponent,
    merged into one test). The merged result is the test; its parts are not tests of their own on any page."""
    parts = set()
    for f in (LAB / "results").glob("*.json"):
        try:
            parts.update(json.loads(f.read_text()).get("merged_from") or [])
        except (OSError, ValueError):
            pass
    return parts


STAGE_PART = re.compile(r"-(race\d+|cheaprace|confirm\d*|holdout)-[a-z0-9-]+$")


def result_files():
    """Result files, oldest first, without the parts of a merged result, and without the grouped parts of a search
    stage (`<search>-<stage>-gN`: one machine's opponents; `deckopt.py rank` merges them into the stage's test)."""
    parts = merged_parts()
    return [f for f in sorted((LAB / "results").glob("*.json"))
            if f.stem not in parts and not re.search(r"-(race\d+|confirm\d*)-g\d+$", f.stem)]


def not_a_rating(r):
    """Why a multi-list result does not rate its lists (None when it does). Daily critic, 14 Nov: race stages,
    cheap-pilot tests and frame tests were counted as "lists that beat the reference".
      selection  a race of a list search: its leaders were picked on these very games;
      part       one opponent's share of a stage, not merged yet;
      cheap      played by the cheap pilot (pmcts with 8 simulations or fewer): it cannot use reactive spells, so
                 its leads did not hold under the full pilot (Riven, 14 Nov);
      other      not a comparison of searched lists (frame tests, the rows of a matchup matrix)."""
    name = r.get("name", "")
    if re.search(r"-(race\d+|cheaprace)(-|$)", name):
        return "selection"
    if STAGE_PART.search(name) and not r.get("merged_from"):
        return "part"
    if re.search(r"-frames\d*$", name) or name.startswith("meta-"):
        return "other"
    m = re.search(r"sims=(\d+)", r.get("agent", ""))
    if m and int(m.group(1)) <= 8:
        return "cheap"
    return None


def engine_since(when):
    """Engine (src/) commits made after a measurement: a figure taken before them may no longer hold."""
    if not when:
        return None
    try:
        log = subprocess.run(["git", "log", f"--since={when}", "--format=%h", "--", "src/"], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout.split()
    except (OSError, subprocess.CalledProcessError):
        return None
    return len(log)


def measured_status(ladder: dict) -> dict:
    """What the Home steps are scored from, computed from the lab's files rather than typed in home.yml
    (2026-11-04 critical review: a hand-typed 80% for the best list and a 96% bot score rested on
    measurements of net_038 and a jury that misplays Kennen):
    - credibility: the newest simulated-vs-real matchup comparison (lab/meta/compare_meta-*.json):
      matchups inside the real players' 95% interval, the net it was measured with, stale if not current;
    - coverage: share of Riven's real non-mirror games played against the legends of that jury;
    - training: the general net and each specialist (running, adopted net, rejections since);
    - list: GA confirmation finalists whose difference against the reference excludes 0."""
    out = {}
    current = Path(ladder.get("current_net") or "").stem
    rl = LAB / "learn" / "specialists" / "riven" / "ladder.json"
    riven_now = Path(json.loads(rl.read_text()).get("current_net") or "").stem if rl.exists() else current

    cms = sorted((LAB / "meta").glob("compare_meta-*.json"))
    if cms:
        cm = json.loads(cms[-1].read_text())
        rows = cm.get("rows", [])
        res = ROOT / cm.get("result", "")
        agent = json.loads(res.read_text()).get("agent", "") if cm.get("result") and res.is_file() else ""
        net = cm.get("net") or net_of(agent)     # realism.py files name the Riven pilot's net directly
        # Strictly inside the real players' 95% interval (compare_matrix's "within" verdict allows 5 points of slack).
        def inside(r):
            lo, hi = (r.get("real_ci95") or [None, None])[:2]
            return lo is not None and r.get("sim") is not None and lo <= r["sim"] <= hi
        within = sum(1 for r in rows if inside(r))
        within_slack = sum(1 for r in rows if r.get("verdict") == "within")
        in_use = {current} | {Path(json.loads(f.read_text()).get("current_net") or "").stem
                              for f in (LAB / "learn" / "specialists").glob("*/ladder.json")}
        pilots = {Path(v).stem for v in (cm.get("pilots") or {}).values()}
        out["credibility"] = {"file": cms[-1].name, "net": net, "current": riven_now,
                              "stale": (bool(net) and net not in in_use) or bool(pilots - in_use),
                              "stale_pilots": sorted(pilots - in_use),
                              "engine_commits_since": engine_since(cm.get("measured_utc")),
                              "within": within, "within_slack": within_slack, "total": len(rows),
                              "rate": round(within / len(rows), 3) if rows else None,
                              "far": sorted({f"{r['legend']} vs {r['opponent_legend']}" for r in rows if not inside(r)})}
        mx = ROOT / cm.get("matrix", "")
        if mx.exists():
            real = json.loads(mx.read_text()).get("matchups", {}).get("Riven", {})
            total = sum(v["games"] for k, v in real.items() if k != "Riven")
            jury = sorted({r["opponent_legend"] for r in rows if r.get("legend") == "Riven" and r.get("opponent_legend") != "Riven"})
            got = sum(real[k]["games"] for k in jury if k in real)
            out["coverage"] = {"legends": jury, "games": got, "of": total, "share": round(got / total, 3) if total else None,
                               "file": cms[-1].name, "measured_utc": cm.get("measured_utc"), "matrix": Path(cm.get("matrix", "")).name,
                               "missing_top": [k for k, _ in sorted(real.items(), key=lambda kv: -kv[1]["games"]) if k != "Riven" and k not in jury][:4]}
    def track(L, pause, runners=20):
        its = L.get("iterations", [])
        since = 0
        for r in reversed(its):
            if r.get("accepted"):
                break
            since += 1
        last = its[-1].get("started_utc") if its else None
        recent = False
        if last:
            try:
                # "training" while the last iteration started less than three hours ago, or less than 2.5 times
                # the longest of the last four iterations (14 Nov: 2 to 5 hours each on a share of the runners)
                starts = [dt.datetime.fromisoformat(r["started_utc"]) for r in its[-5:] if r.get("started_utc")]
                gaps = [(b - a).total_seconds() for a, b in zip(starts, starts[1:]) if 0 < (b - a).total_seconds() < 12 * 3600]
                # (a track on a share of the 20 runners is slower in proportion: about 36 minutes on 20, 6 hours on 2)
                window = max(3 * 3600, 2.5 * max(gaps[-4:] or [0]), 2.5 * 0.6 * 3600 * 20 / max(1, runners))
                recent = (dt.datetime.now(dt.timezone.utc) - dt.datetime.fromisoformat(last)).total_seconds() < window
            except ValueError:
                pass
        paused = bool(pause and pause.exists())
        t = {"current": Path(L.get("current_net") or "").stem, "iterations": len(its), "last_started": last,
             "running": recent and not paused, "state": "paused" if paused else "training" if recent else "idle",
             "rejected_since_adoption": since, "adopted": sum(1 for r in its if r.get("accepted"))}
        # Continuous training (10 Nov): one candidate per iteration, adopted unless clearly worse. Counted apart from
        # the gated iterations before it; the head (newest adopted net) is committed only every few iterations, so the
        # net in use (tests, Play) can be older than the head.
        cont = [r for r in its if r.get("continuous")]
        if cont:
            # the legend's own frozen nets only: the general net loses nearly every mirror to a specialist (no signal)
            anchors = [(a.get("wins", 0), a.get("games", 0)) for r in cont[-8:]
                       for k, a in (r.get("vs_anchors") or {}).items() if "spec_" in k]
            g = sum(x[1] for x in anchors)
            t["continuous"] = {"since": cont[0].get("iteration"), "iterations": len(cont),
                               "adopted": sum(1 for r in cont if r.get("accepted")),
                               "head": Path(L.get("head_net") or L.get("current_net") or "").stem,
                               "anchor_rate_last8": round(sum(x[0] for x in anchors) / g, 4) if g else None, "anchor_games_last8": g}
            gated = [r for r in its if not r.get("continuous")]
            t["gated"] = {"iterations": len(gated), "adopted": sum(1 for r in gated if r.get("accepted"))}
        return t
    out["training"] = {"general": track(ladder, LAB / "learn" / "PAUSE"), "specialists": {}}
    for f in sorted((LAB / "learn" / "specialists").glob("*/ladder.json")):
        spec_file = LAB / "specs" / f"train_spec_{f.parent.name}.json"
        runners = int(json.loads(spec_file.read_text()).get("runners", 20)) if spec_file.exists() else 20
        out["training"]["specialists"][f.parent.name] = track(json.loads(f.read_text()), f.parent / "PAUSE", runners)
    confirmed = []
    for f in result_files():
        r = json.loads(f.read_text())
        if riftlab_legend((r.get("spec") or {}).get("base", "")) != "Riven":
            continue            # step 4 is the best Riven list: other legends' searches do not count here
        for c in (r.get("ga") or {}).get("confirmation", []) or []:
            for fin in c.get("finalists", []):
                ci = fin.get("diff_ci95") or [None, None]
                if ci[0] is not None and ci[0] > 0:
                    confirmed.append({"result": f.stem, "label": fin.get("label"), "diff": fin.get("diff_vs_reference"),
                                      "ci95": ci, "net": net_of(r.get("agent", ""))})
    # Head-to-head list comparisons: a test whose lists all met the same opponents (a confirmation such as
    # cetner-confirm). Each list's weighted rate, and each list minus the first (the reference) with a 95%
    # interval (normal approximation, matchups independent, weights normalised).
    comparisons = []
    items = []      # (name, result): multi-list tests as they are, single-list ones grouped by the deals they played
    groups = {}
    for f in result_files():
        r = json.loads(f.read_text())
        decks = r.get("decks") or []
        if r.get("ga") or not decks:
            continue
        if len(decks) >= 2:
            items.append((f.stem, r))
            continue
        sp = r.get("spec") or {}
        key = (r.get("seed"), r.get("games_per_matchup"), r.get("agent"),
               tuple(sorted((o.get("deck"), o.get("agent", "")) for o in sp.get("opponents", []))))
        if key[0] is not None and sp.get("opponents"):
            groups.setdefault(key, []).append((f.stem, r))
    for key, members in groups.items():
        if len(members) < 2:
            continue
        decks = [m[1]["decks"][0] for m in members]
        # the reference: the Sacramento list when the set has it, else the first list
        decks.sort(key=lambda d: 0 if d.get("path") == "decks/riven_sacramento.txt" else 1)
        merged = dict(members[-1][1], decks=decks)
        items.append((f"{members[0][0]} + {len(members) - 1} more (seed {key[0]})", merged))
    items.sort(key=lambda it: str(it[1].get("started_utc") or ""))
    # A Riven genetic search ends with a confirmation on fresh seeds (its finalists against the base list): it is a
    # list comparison too, the newest one when it is the latest Riven test.
    ga_comps = []
    for f in result_files():
        r = json.loads(f.read_text())
        sp = r.get("spec") or {}
        conf = ((r.get("ga") or {}).get("confirmation") or [None])[-1]
        if not conf or riftlab_legend(sp.get("base", "")) != "Riven":
            continue
        rows = [{"label": x.get("label"), "path": x.get("path"), "rate": x.get("confirmed_rate"), "diff": x.get("diff_vs_reference"),
                 "ci95": x.get("diff_ci95")} for x in conf.get("finalists", []) if x.get("diff_ci95")]
        legends = sorted({riftlab_legend(o.get("deck", "")) for o in sp.get("opponents", [])} - {None})
        ga_comps.append({"result": f.stem, "net": net_of(r.get("agent", "")), "started_utc": r.get("started_utc"),
                         "reference": Path(sp.get("base", "")).stem, "reference_rate": (conf.get("reference") or {}).get("confirmed_rate"),
                         "lists": rows, "jury_legends": legends, "ga": True})
    other_comps = []        # comparisons of another legend's lists: for their own test page, never for Home
    for stem, r in items:
        decks = r.get("decks") or []
        # Home's list figures are about Riven lists: a comparison of another legend's lists (Orianna against Riven,
        # 10 Nov) is not a Riven list check.
        legs = {deck_legend(d.get("path", ""), d.get("label", "")) for d in decks} - {None}
        not_riven = bool(legs - {"Riven"})
        opp = lambda d: {m.get("opponent_deck") or m.get("opponent"): m for m in d.get("matchups", [])}
        base = opp(decks[0])
        if not base or any(set(opp(d)) != set(base) for d in decks[1:]):
            continue
        W = sum(m.get("weight", 1) for m in base.values()) or 1
        def stat(d):
            ms = opp(d)
            rate = sum(m.get("weight", 1) * m["wins"] / max(1, m["games"]) for m in ms.values()) / W
            var = sum((m.get("weight", 1) / W) ** 2 * (m["wins"] / max(1, m["games"])) * (1 - m["wins"] / max(1, m["games"])) / max(1, m["games"]) for m in ms.values())
            return rate, var
        b_rate, b_var = stat(decks[0])
        rows = []
        for d in decks[1:]:
            rate, var = stat(d)
            diff, half = rate - b_rate, 1.96 * (var + b_var) ** 0.5
            rows.append({"label": d.get("label"), "path": d.get("path"), "rate": round(rate, 4), "diff": round(diff, 4),
                         "ci95": [round(diff - half, 4), round(diff + half, 4)]})
        legends = sorted({riftlab_legend(m.get("opponent_deck", "")) for m in base.values()} - {None})
        (other_comps if not_riven else comparisons).append(
            {"result": stem, "net": net_of(r.get("agent", "")), "started_utc": r.get("started_utc"), "reference": decks[0].get("label"),
             # A race of a card-level search (lab/deckopt.py) picks its leaders on these very games: it is a
             # selection stage, never a rating (its leads are inflated). Only confirmations rate a list.
             "selection": bool(not_a_rating(r)), "kind": not_a_rating(r),
             "reference_path": decks[0].get("path"), "reference_rate": round(b_rate, 4), "lists": rows, "jury_legends": legends,
             "legend": sorted(legs)[0] if len(legs) == 1 else None})
    all_riven = sorted(comparisons + ga_comps, key=lambda c: str(c.get("started_utc") or ""))
    # Home speaks of rated lists only: races, cheap-pilot tests, unmerged parts and frame tests stay on their own
    # test pages (by_result below), flagged, and never feed "newest check", "best" or "lists that beat the reference".
    comparisons = [c for c in all_riven if not c.get("selection")]
    for c in comparisons:
        if c.get("ga"):
            continue            # already counted from the search's own confirmation above
        for row in c["lists"]:
            if str(row.get("label") or "").lower().startswith("reference"):
                continue        # another frame's reference shown as a yardstick: not a searched list
            if row["ci95"][0] > 0:
                confirmed.append({"result": c["result"], "label": row["label"], "diff": row["diff"], "ci95": row["ci95"], "net": c["net"]})
    # Only a confirmation played by a net still in use counts (an older bot misjudged the matchups it was played on).
    in_use = {current} | {Path(json.loads(f.read_text()).get("current_net") or "").stem
                          for f in (LAB / "learn" / "specialists").glob("*/ladder.json")}
    stale = [c for c in confirmed if c.get("net") not in in_use]
    confirmed = [c for c in confirmed if c.get("net") in in_use]
    # The best measured list: the top-rated list of the newest comparison (lists tagged "[owned]" are the ones
    # Jeremy can build; prefer them), with its difference against the reference and whether it is proven.
    best = None
    plain = [c for c in comparisons if not c.get("ga")]   # a search's finalists are measured against its base list
    if plain:
        c = plain[-1]
        pool = [x for x in c["lists"] if "[owned]" in (x.get("label") or "")] or c["lists"]
        if pool:
            top = max(pool, key=lambda x: x["rate"])
            lo, hi = top["ci95"]
            best = {**top, "label": re.sub(r"\s*\([+\u2212-]\d[^)]*\)", "", top.get("label") or ""),
                    "engine_commits_since": engine_since(c.get("started_utc")), "result": c["result"], "reference": c["reference"], "reference_rate": c["reference_rate"],
                    "net": c["net"], "stale": c["net"] not in in_use,
                    "verdict": "beats" if lo > 0 else "trails" if hi < 0 else "ties"}
    # The right-favourite criterion (lab/tools/favorites.py): does the simulation favour the same side as real players.
    sys.path.insert(0, str(LAB / "tools"))
    import favorites
    fav = favorites.all_scores()
    for s_ in [fav.get("field")] + list(fav["specialists"].values()):
        if s_:
            s_["engine_commits_since"] = engine_since(s_.get("measured_utc"))
            # Counted half only when the measured legend's own net was replaced; opponent pilots that were adopted
            # since are listed (the specialists keep training, so a field measure is rarely fully current).
            s_["pilots_replaced"] = sorted({Path(v).stem for v in (s_.get("pilots") or {}).values()} - in_use)
            s_["stale"] = s_.get("net") not in in_use
    out["favorites"] = fav
    for c in comparisons[-3:]:
        c["stale"] = c.get("net") not in in_use
        c["engine_commits_since"] = engine_since(c.get("started_utc"))
    # A search finalist measured again later in a plain comparison (more games, fresh deals): the newest such row
    # per list file, so a page that quotes the search's own confirmation can say what the re-test found.
    # Every legend's lists count here (a Volibear finalist re-measured in a Volibear test), newest last.
    retests = {}
    every = sorted([c for c in all_riven if not c.get("ga")] + other_comps, key=lambda c: str(c.get("started_utc") or ""))
    for c in every:
        if c.get("selection"):
            continue
        for row in c["lists"]:
            if row.get("path"):
                retests[row["path"]] = {"result": c["result"], "started_utc": c.get("started_utc"), "diff": row["diff"], "ci95": row["ci95"],
                                        "reference": c["reference"], "reference_path": c.get("reference_path"), "net": c["net"]}
    # The same list under another file name (a search finalist copied to "<search>-best.txt" and re-measured under
    # that name) is the same list: a finalist's file also points at the re-test of its copy.
    def digest(path):
        f = ROOT / path
        if not path or not f.is_file():
            return None
        lines = sorted(ln.strip() for ln in f.read_text(errors="replace").splitlines() if ln.strip() and not ln.lstrip().startswith("#"))
        return hashlib.sha1("\n".join(lines).encode()).hexdigest()
    by_digest = {digest(path): row for path, row in retests.items()}
    by_digest.pop(None, None)
    for f in result_files():
        conf = ((json.loads(f.read_text()).get("ga") or {}).get("confirmation") or [None])[-1]
        for x in (conf or {}).get("finalists", []):
            pth = x.get("path")
            if pth and pth not in retests and digest(pth) in by_digest:
                retests[pth] = by_digest[digest(pth)]
    # A multi-list test's own comparison (each list against the test's first list, same deals), by result id: the
    # test page states that, not "best of N" against whichever list came last.
    by_result = {c["result"]: {"reference": c["reference"], "reference_rate": c["reference_rate"], "lists": c["lists"],
                               "net": c["net"], "legend": c.get("legend"), "selection": bool(c.get("selection")), "kind": c.get("kind"),
                               "engine_commits_since": engine_since(c.get("started_utc"))}
                 for c in every}
    # The Decks page's optimiser tab for every legend but Riven (Riven's follows `comparisons` above): the lists of
    # that legend's comparisons played by the net of its newest one against the same reference list. A list measured
    # in several of them (the same cards, whatever the file name) is pooled, each measure weighted by its precision,
    # so a list measured twice is not ranked on its luckier measure. Best first, ten at most.
    optimiser = {}
    by_legend = {}
    for c in sorted(other_comps, key=lambda c: str(c.get("started_utc") or "")):
        if c.get("legend") and not c.get("selection"):
            by_legend.setdefault(c["legend"], []).append(c)
    for leg, cs in by_legend.items():
        last = cs[-1]
        rows = {}
        # (14 Nov: every pilot's confirmations against the same reference count, so the list a legend already had
        # stays on the page next to a newcomer measured by a newer pilot; each row says which pilots measured it)
        for c in cs:
            if c.get("reference_path") != last.get("reference_path"):
                continue
            for row in c["lists"]:
                if str(row.get("label") or "").lower().startswith("reference"):
                    continue
                rows.setdefault(digest(row.get("path")) or row.get("path"), []).append({**row, "result": c["result"], "net": c["net"]})
        pooled_rows = []
        for ms in rows.values():
            ws = [1 / max(1e-9, ((m["ci95"][1] - m["ci95"][0]) / 3.92) ** 2) for m in ms]
            diff = sum(w * m["diff"] for w, m in zip(ws, ms)) / sum(ws)
            se = (1 / sum(ws)) ** 0.5
            newest = ms[-1]
            pooled_rows.append({"path": newest.get("path"), "label": newest.get("label"), "diff": round(diff, 4),
                                "ci95": [round(diff - 1.96 * se, 4), round(diff + 1.96 * se, 4)],
                                "rate": newest.get("rate"), "measures": len(ms), "results": [m["result"] for m in ms],
                                "nets": sorted({m["net"] for m in ms}), "same_deals_as": sorted({m["result"] for m in ms})})
        if not any(str(x.get("path") or "").startswith("lab/variants/") for x in pooled_rows):
            continue            # two community lists compared: no search behind it, so not on the optimiser tab
        # A search's stages call the reference list just "reference": show the name earlier tests gave that file.
        ref_name = next((c["reference"] for c in cs if c.get("reference_path") == last.get("reference_path")
                         and c["reference"] != "reference"), last["reference"])
        optimiser[leg] = {"reference": ref_name, "reference_path": last.get("reference_path"), "net": last["net"],
                          "stale": last["net"] not in in_use, "engine_commits_since": engine_since(last.get("started_utc")),
                          # ranked by the low end of the interval: the list most surely above the reference first,
                          # so one lucky measure does not outrank a list measured three times
                          "lists": sorted(pooled_rows, key=lambda x: -x["ci95"][0])[:10]}
    out["list"] = {"confirmed": confirmed, "stale_confirmations": stale, "comparisons": comparisons[-3:], "best": best,
                   "retests": retests, "by_result": by_result, "nets_in_use": sorted(n for n in in_use if n),
                   "optimiser": optimiser}
    # Coverage of the newest list test's jury (what the newest deck verdicts rest on), next to compare_meta's.
    if comparisons and "coverage" in out:
        mx = ROOT / json.loads(cms[-1].read_text()).get("matrix", "")
        real = json.loads(mx.read_text()).get("matchups", {}).get("Riven", {}) if mx.exists() else {}
        total = sum(v["games"] for k, v in real.items() if k != "Riven")
        jl = [l for l in comparisons[-1]["jury_legends"] if l != "Riven"]
        got = sum(real[k]["games"] for k in jl if k in real)
        out["coverage_latest_test"] = {"result": comparisons[-1]["result"], "legends": jl,
                                       "share": round(got / total, 3) if total else None,
                                       "missing_top": [k for k, _ in sorted(real.items(), key=lambda kv: -kv[1]["games"])
                                                       if k != "Riven" and k not in jl][:4]}
    # Engine check: the newest replay of the RiftLite games through the current engine (lab/learn/human_riftlite.json,
    # rebuilt by build_human_dataset.py), median desyncs per game (all desyncs: no engine-only split is recorded).
    hr = LAB / "learn" / "human_riftlite.json"
    if hr.exists():
        H = json.loads(hr.read_text())
        ds = sorted(g["desyncs"] for g in H.get("games", []) if g.get("desyncs") is not None)
        if ds:
            n = len(ds)
            med = ds[n // 2] if n % 2 else (ds[n // 2 - 1] + ds[n // 2]) / 2
            out["engine_replay"] = {"file": "lab/learn/human_riftlite.json", "generated": H.get("generated"), "games": n,
                                    "median": med, "mean": round(sum(ds) / n, 2),
                                    "engine_commits_since": engine_since(H.get("generated"))}
    return out


_LEGENDS = None


def learning_tracks(training: dict) -> dict:
    """The learning curves of each legend's specialist net, one compact row per iteration (the Bot tab draws them):
    Elo, the mirror against the run's own frozen earlier nets, the net in place against the panel of tournament
    lists, the value loss on held-out games and the share of human first moves found.

    The mirror counts the legend's own frozen nets only (the general net loses nearly every game and carries no
    signal), and the panel is the net in place (`base_rate`), as lab/learn/TRAINING_KNOWLEDGE.md reads them. A curve
    is only comparable while its opponents stay the same: `ms` and `ps` number the stretches of the run that share
    one set of frozen nets and one panel, so the page restarts its line where they change. `trend` is the last 8
    iterations against the 8 before (pooled games, standard error of the difference), within the newest stretch."""
    titles = {v: k for k, v in json.loads((LAB / "learn" / "legend_champions.json").read_text()).items()}

    def trend(rows, key, seg):
        last = rows[-1].get(seg) if rows else None
        xs = [r[key] for r in rows if r.get(key) and r.get(seg) == last]
        if last is None or len(xs) < 16:
            return None
        (w1, g1), (w0, g0) = [(sum(x[0] for x in part), sum(x[1] for x in part)) for part in (xs[-8:], xs[-16:-8])]
        p1, p0 = w1 / g1, w0 / g0
        return {"last": round(p1, 4), "prev": round(p0, 4), "diff": round(p1 - p0, 4), "games": g1,
                "se": round((p1 * (1 - p1) / g1 + p0 * (1 - p0) / g0) ** 0.5, 4)}

    tracks = []
    for f in sorted((LAB / "learn" / "specialists").glob("*/ladder.json")):
        L, slug = json.loads(f.read_text()), f.parent.name
        its = sorted(L.get("iterations", []), key=lambda r: r.get("iteration", 0))
        if not its:
            continue
        rows, mkey, pkey, ms, ps = [], None, None, 0, 0
        for r in its:
            row = {"i": r.get("iteration"), "t": r.get("started_utc"), "ok": bool(r.get("accepted"))}
            if r.get("continuous"):
                row["c"] = 1
            if r.get("committed"):
                row["k"] = 1
            if r.get("elo") is not None:
                row["elo"] = round(r["elo"], 1)
            own = {k: a for k, a in (r.get("vs_anchors") or {}).items() if "spec_" in k and a.get("games")}
            if own:
                if tuple(sorted(own)) != mkey:
                    mkey, ms = tuple(sorted(own)), ms + 1
                row["m"], row["ms"] = [sum(a.get("wins", 0) for a in own.values()), sum(a["games"] for a in own.values())], ms
            pn = r.get("panel") or {}
            if pn.get("games") and pn.get("base_rate") is not None:
                key = (tuple(sorted(pn.get("matchups") or {})), bool(pn.get("held_out")))
                if key != pkey:
                    pkey, ps = key, ps + 1
                row["p"], row["ps"] = [round(pn["base_rate"] * pn["games"]), pn["games"]], ps
                if pn.get("rate") is not None:
                    row["pc"] = round(pn["rate"], 4)
            if r.get("val_loss") is not None:
                row["vl"] = round(r["val_loss"], 4)
            hl = r.get("human_legend") or {}
            if hl.get("rows") and hl.get("top1") is not None:
                row["hu"] = [round(hl["top1"] * hl["rows"]), hl["rows"]]
            vc = r.get("vs_current") or {}
            if vc.get("games"):
                row["g"] = [vc.get("wins", 0), vc["games"]]
            if r.get("minutes") is not None:
                row["min"] = round(r["minutes"], 1)
            rows.append(row)
        st = training.get(slug, {})
        cont = [r["i"] for r in rows if r.get("c")]
        last_panel = (its[-1].get("panel") or {})
        tracks.append({
            "slug": slug, "legend": riftlab_legend(slug) or slug.replace("_", " ").title(),
            "title": titles.get(slug) or titles.get(slug.rsplit("_h", 1)[0]) or "",
            "human_games": slug.endswith("_h"),      # riven_h: the variant warm-started on human games
            "state": st.get("state") or "idle", "last_started": rows[-1]["t"], "first_started": rows[0]["t"],
            "current": Path(L.get("current_net") or "").stem, "head": Path(L.get("head_net") or L.get("current_net") or "").stem,
            "continuous_since": cont[0] if cont else None, "kept": sum(1 for r in rows if r["ok"]),
            "minutes": round(sum(r.get("min") or 0 for r in rows)),
            "panel_held_out": bool(last_panel.get("held_out")),
            "panel_lists": [riftlab_legend(m.get("deck", "")) or riftlab_legend(k) or k
                            for k, m in sorted((last_panel.get("matchups") or {}).items())],
            "mirror_nets": [Path(k).stem for k in (mkey or ())],
            "trend": {"mirror": trend(rows, "m", "ms"), "panel": trend(rows, "p", "ps")},
            "rows": rows})
    tracks.sort(key=lambda t: t["last_started"] or "", reverse=True)      # the run in progress first, then the newest
    tracks.sort(key=lambda t: t["state"] != "training")
    return {"tracks": tracks}


def _legend_names():
    """Legend names: the fixed list plus every legend of the newest real-results matrix (Rek'Sai, Irelia, Jayce...)."""
    global _LEGENDS
    if _LEGENDS is None:
        names = ["Riven", "Kennen", "Kai'Sa", "Rengar", "Vex", "Akali", "Evelynn", "LeBlanc", "Lillia", "Mordekaiser",
                 "Orianna", "Seraphine", "Master Yi", "Nasus", "Ekko", "Ziggs", "Draven", "Viktor", "Jinx", "Volibear",
                 "Rek'Sai", "Irelia", "Jayce"]
        ms = sorted((LAB / "meta").glob("riftdecks_winrate_*.json"))
        if ms:
            try:
                names += [k for k in json.loads(ms[-1].read_text()).get("matchups", {}) if k not in names]
            except (ValueError, OSError):
                pass
        _LEGENDS = names
    return _LEGENDS


def deck_legend(path: str, label: str = ""):
    """The legend of a deck file, read from its Legend section ("1 Orianna, Lady of Clockwork" -> "Orianna");
    from the file name or label when the file is gone. None when it cannot be told."""
    f = ROOT / path
    if path and f.is_file():
        lines = f.read_text(errors="replace").splitlines()
        for i, ln in enumerate(lines):
            if ln.strip().lower().startswith("legend"):
                for nxt in lines[i + 1:i + 4]:
                    m = re.match(r"\s*\d+\s+([^,]+)", nxt)
                    if m:
                        # a file written by the lab's tools names the legend card alone ("Iron Revenant"): the
                        # champion's name is that card's tag ("Mordekaiser")
                        return _legend_tag().get(m.group(1).strip().lower(), m.group(1).strip())
    return riftlab_legend(path) or riftlab_legend(label.replace(" ", "_"))


_LEGEND_TAG = {}


def _legend_tag():
    if not _LEGEND_TAG:
        for c in riftlab.Registry.load().cards:
            if c.get("card_type") == "legend" and c.get("tags"):
                _LEGEND_TAG[c["name"].lower()] = c["tags"][0]
    return _LEGEND_TAG


def riftlab_legend(path: str):
    """The champion legend a meta deck file names ('..._kai_sa.txt' -> "Kai'Sa"), from the matrix's legend names."""
    names = _legend_names()
    s = re.sub(r"[^a-z0-9]", "", Path(path).stem.lower())
    for n in sorted(names, key=len, reverse=True):
        if re.sub(r"[^a-z0-9]", "", n.lower()) in s:
            return n
    return None


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-replays", type=int, default=40)
    a = ap.parse_args(argv)
    reg = riftlab.Registry.load()
    riftlab.write_index(reg)
    index = json.loads((LAB / "index.json").read_text())

    if DIST.exists():
        shutil.rmtree(DIST)
    (DIST / "replays").mkdir(parents=True)

    tests = []
    indexed = {d["path"] for d in index["decks"]}
    parts = merged_parts()
    for t in index["tests"]:
        if Path(t["file"]).stem in parts:
            continue            # one opponent's share of a merged test: the merged test is the one shown
        tr = json.loads((ROOT / t["file"]).read_text())
        tr["engine_commits_since"] = engine_since(tr.get("started_utc"))
        # A test's deck rows carry the list's full stats; the page reads the card list from the deck index when the
        # list is indexed (tests.js deckSection) and never reads keywords or power symbols, so those stay out of
        # data.json (the artifact's per-file limit is 16 MB; genetic searches add hundreds of rows).
        for n, row in enumerate(tr.get("decks") or []):
            s = row.get("stats")
            if isinstance(s, dict):
                for k in ("keywords", "power_symbols") + (("list",) if row.get("path") in indexed else ()):
                    s.pop(k, None)
            # 14 Nov: the engine command lines of every list against every opponent were 2.1 MB of data.json. The
            # page shows them only as "exact commands" for a rerun: the first list's are kept, the others differ by
            # --deck1/--deck2 alone (tests.js says so).
            if n:
                for m in row.get("matchups") or []:
                    m.pop("commands", None)
        tests.append(tr)

    replays = []
    for f in sorted((LAB / "replays").rglob("game_*.json"), key=lambda p: p.stat().st_mtime, reverse=True):
        if len(replays) >= a.max_replays:
            break
        rel = f.relative_to(LAB / "replays")
        parts = rel.parts               # <test id>/<deck__vs__opp>/seatN/game_K.json
        g = json.loads(f.read_text())
        name = re.sub(r"[^A-Za-z0-9_.-]+", "_", "__".join(parts))
        shutil.copy(f, DIST / "replays" / name)
        replays.append({"file": f"replays/{name}", "test_id": parts[0] if len(parts) > 3 else "",
                        "matchup": parts[1] if len(parts) > 3 else "", "seat": parts[2] if len(parts) > 3 else "",
                        "game": g.get("game"), "deck1": g.get("deck1"), "deck2": g.get("deck2"),
                        "agent1": g.get("agent1"), "agent2": g.get("agent2"), "winner": g.get("winner"),
                        "scores": g.get("scores"), "turns": g.get("turns"), "decisions": g.get("decisions"),
                        "frames": len(g.get("frames", []))})

    # Human games replayed through the engine (lab/tools/human_review.py): same frame format plus the comparison
    # fields; copied as replays/human__<stem>_gN.json and summarised in data.human_review for the Replays/Tests tabs.
    human = {"generated": "", "agent": "", "games": []}
    hidx = LAB / "human_replays" / "index.json"
    if hidx.exists():
        hi = json.loads(hidx.read_text())
        human["generated"], human["agent"] = hi.get("generated", ""), hi.get("agent", "")
        for g in hi.get("games", []):
            src = ROOT / g.get("path", "")
            if not src.exists():
                continue
            stem = src.parent.name                                   # <file stem>_g<N>
            name = f"human__{stem}.json"
            shutil.copy(src, DIST / "replays" / name)
            rec = json.loads(src.read_text())
            summ = json.loads((ROOT / g["summary"]).read_text()) if g.get("summary") and (ROOT / g["summary"]).exists() else {}
            raw = LAB / "human_games" / g["file"]
            source_url = ""
            if raw.exists():
                try:
                    source_url = json.loads(raw.read_text()).get("source_url", "")
                except Exception:  # noqa: BLE001
                    source_url = ""
            decks = {}
            for seat in ("P1", "P2"):
                dp = src.parent / f"deck_{seat.lower()}.txt"
                inf = (summ.get("inferred_decks") or {}).get(seat, {})
                lst = riftlab.parse_deck(dp) if dp.exists() else {}
                decks[seat] = {"player": inf.get("player"), "legend": inf.get("legend"), "champion": inf.get("champion"),
                               "battlefields": inf.get("battlefields", []), "battlefield_brought": inf.get("battlefield_brought"),
                               "filler": (inf.get("filler") or {}).get("source", ""), "filler_cards": inf.get("filler_cards", 0),
                               "dropped": inf.get("dropped", []), "unknown_cards": sorted(set(inf.get("unknown_cards", []))),
                               "legal": inf.get("legal"), "list": {k: dict(lst.get(k, {})) for k in ("champion", "maindeck", "battlefields", "runes")}}
            entry = {k: g.get(k) for k in ("file", "game", "players", "capture_player", "legends", "bot", "decisions", "decisions_compared",
                                           "agreement_pct", "agreement_clean_pct", "compared_clean", "desyncs", "desyncs_engine", "log_artefacts", "top_desync_causes",
                                           "silent_passes", "hand_fixes", "stop_reason", "engine_scores", "log_scores", "seconds")}
            entry.update({"replay": f"replays/{name}", "stem": stem, "date": g["file"][:10], "source_url": source_url,
                          "capture_seat": rec.get("capture_seat"), "first_player": summ.get("first_player"),
                          "summary": {"agreement": summ.get("agreement", {}), "disagreements": summ.get("disagreements", []),
                                      "desync_list": summ.get("desync_list", [])[:12], "desync_causes": summ.get("desync_causes", {}),
                                      "result": summ.get("result", {}), "forced_decisions": summ.get("forced_decisions"),
                                      "decisions_human_known": summ.get("decisions_human_known"), "readiness_fixes": summ.get("readiness_fixes"),
                                      "hand_fixes": (summ.get("hand_fixes") or {}).get("created"), "decks": decks}})
            human["games"].append(entry)
            replays.append({"file": f"replays/{name}", "human": True, "test_id": "human", "matchup": f"{g['players'][0]}__vs__{g['players'][1]}",
                            "seat": f"seat{rec.get('capture_seat', '')}", "game": g.get("game"), "deck1": rec.get("deck1"), "deck2": rec.get("deck2"),
                            "agent1": rec.get("agent1"), "agent2": rec.get("agent2"), "winner": rec.get("winner"), "scores": rec.get("scores"),
                            "turns": rec.get("turns"), "decisions": rec.get("decisions"), "frames": len(rec.get("frames", [])),
                            "players": g.get("players"), "legends": g.get("legends"), "capture_seat": rec.get("capture_seat"), "date": g["file"][:10],
                            "agreement_pct": g.get("agreement_pct"), "decisions_compared": g.get("decisions_compared"), "stem": stem})
        print(f"  human games: {len(human['games'])} replayed games from {hidx.relative_to(ROOT)}")

    ladder_path = LAB / "learn" / "ladder.json"
    ladder = json.loads(ladder_path.read_text()) if ladder_path.exists() else {"iterations": []}

    # Deck texts: the Play tab hands them to the in-browser engine (rb_new_game takes the deck files' text).
    # A generated variant's header lines are already in its `comments`; repeating them in `text` for 2,400 variants
    # cost 1.4 MB of the 16 MB data.json may hold, so the text of a variant carries the list only.
    for d in index["decks"]:
        p = ROOT / d["path"]
        text = p.read_text() if p.exists() else ""
        if d["path"].startswith("lab/variants/") and d.get("list"):
            # 14 Nov: 2,400 variant texts were 1.2 MB of a data.json at 15.5 MB; the app rebuilds a variant's text
            # from its `list`, section by section in the same order (app/lib.js, deckText).
            text = ""
        d["text"] = text

    engine = {}
    net_rel = ladder.get("current_net")
    net_path = ROOT / net_rel if net_rel and (ROOT / net_rel).exists() else DEFAULT_NET
    # Two nets ride along: the value net (RBVN; the loop writes one next to every two-head .rbpn) and the two-head
    # policy net itself. The worker loads both; with the policy net the search bot plays as training does
    # (pmcts:eval=policy:<file>: value head as leaf eval, policy head as prior), else it falls back to the value net.
    pnet_path = net_path if net_path.suffix == ".rbpn" else net_path.with_suffix(".rbpn")
    if net_path.suffix == ".rbpn" and net_path.with_suffix(".rbvn").exists():
        net_path = net_path.with_suffix(".rbvn")
    if all(p.exists() for p in ENGINE_FILES.values()):
        (DIST / "engine").mkdir(exist_ok=True)
        for rel, src in ENGINE_FILES.items():
            shutil.copy(src, DIST / rel)
            print(f"  {rel:<24} {src.stat().st_size / 1e6:.2f} MB  (from {src.relative_to(ROOT)})")
        import base64, gzip
        net_rel_out = ""
        if net_path.exists():
            # The hosting serves only known file types, so the binary net travels base64-encoded in a JSON file.
            net_rel_out = "engine/value_net.json"
            (DIST / net_rel_out).write_text(json.dumps({"source": str(net_path.relative_to(ROOT)),
                                                        "base64": base64.b64encode(net_path.read_bytes()).decode()}))
            print(f"  {net_rel_out:<24} {(DIST / net_rel_out).stat().st_size / 1e6:.2f} MB  (from {net_path.relative_to(ROOT)})")
        pnet_rel_out = ""
        if pnet_path.exists():
            # ~7.7 MB raw: gzipped (~5.9 MB) then base64 (~7.9 MB), under the 16 MB a hosted file may hold; the
            # worker inflates it with DecompressionStream.
            pnet_rel_out = "engine/policy_net.json"
            (DIST / pnet_rel_out).write_text(json.dumps({"source": str(pnet_path.relative_to(ROOT)), "encoding": "gzip",
                                                         "base64": base64.b64encode(gzip.compress(pnet_path.read_bytes(), 9, mtime=0)).decode()}))
            size = (DIST / pnet_rel_out).stat().st_size
            print(f"  {pnet_rel_out:<24} {size / 1e6:.2f} MB  (from {pnet_path.relative_to(ROOT)})")
            if size > 16_000_000:
                print(f"  {pnet_rel_out} is over 16 MB: not shipped, the bot will use the value net")
                (DIST / pnet_rel_out).unlink()
                pnet_rel_out = ""
        # Each legend the bot may pilot gets its specialist's policy net (8 Nov: the Play bot played Riven with the
        # general net, which never trained on it). Gzipped + base64 like the general one (~7-11 MB each); the Play tab
        # loads the one for the bot deck's legend and falls back to the general net for the others.
        legend_slug = json.loads((LAB / "learn" / "legend_champions.json").read_text())
        spec_nets = {}
        for slug in PLAY_SPECIALISTS:
            lf = LAB / "learn" / "specialists" / slug / "ladder.json"
            cur = json.loads(lf.read_text()).get("current_net") if lf.exists() else None
            src = ROOT / cur if cur else None
            if not src or src.suffix != ".rbpn" or not src.exists():
                continue
            rel = f"engine/policy_{slug}.json"
            (DIST / rel).write_text(json.dumps({"source": cur, "encoding": "gzip",
                                                "base64": base64.b64encode(gzip.compress(src.read_bytes(), 9, mtime=0)).decode()}))
            size = (DIST / rel).stat().st_size
            if size > 16_000_000:
                (DIST / rel).unlink()
                print(f"  {rel} is over 16 MB: not shipped, the {slug} bot uses the general net")
                continue
            spec_nets[slug] = {"file": rel, "source": cur}
            print(f"  {rel:<24} {size / 1e6:.2f} MB  (from {cur})")
        engine = {"js": "engine/riftbound.js", "wasm": "engine/riftbound.wasm",
                  "specialist_nets": spec_nets, "legend_slug": legend_slug,
                  "net": net_rel_out, "net_source": str(net_path.relative_to(ROOT)) if net_path.exists() else "",
                  "policy_net": pnet_rel_out, "policy_net_source": str(pnet_path.relative_to(ROOT)) if pnet_rel_out else ""}
        # The browser engine is built by hand (cmake --build build-wasm): say when it is older than the engine
        # sources, so Play does not pass for the current rules (11 Nov: 22 card domains fixed, Play a day behind).
        wasm = ROOT / "build-wasm" / "riftbound.wasm"
        built = dt.datetime.fromtimestamp(wasm.stat().st_mtime, dt.timezone.utc).isoformat(timespec="seconds")
        engine["built_utc"] = built
        # What a first game downloads, measured on the shipped files (the Play page quotes it).
        mb = lambda q: round(q.stat().st_size / 1e6, 1) if q.is_file() else None
        pol = [m for m in (mb(q) for q in sorted((DIST / "engine").glob("policy_*.json"))) if m]
        engine["size_mb"] = {"engine": round(sum(q.stat().st_size for q in (DIST / "engine").glob("riftbound.*")) / 1e6, 1),
                             "value_net": mb(DIST / "engine" / "value_net.json"),
                             "policy_min": min(pol) if pol else None, "policy_max": max(pol) if pol else None}
        engine["src_commits_since_build"] = engine_since(built)
    else:
        print("  no WebAssembly engine in build-wasm/: the Play tab will be hidden")

    # Card art: the sprite sheets are copied as is; the map rides in data.json so the page learns it with its one fetch.
    art = {}
    if (ART_DIR / "art.json").exists():
        art = json.loads((ART_DIR / "art.json").read_text())
        shutil.copytree(ART_DIR, DIST / "art", ignore=shutil.ignore_patterns("art.json", ".*"))
        sheets = list((DIST / "art").glob("*.webp"))
        print(f"  art/: {len(sheets)} sprite sheets, {sum(p.stat().st_size for p in sheets) / 1e6:.1f} MB, {len(art.get('cards', {}))} cards mapped")
    else:
        print("  no sprite sheets in lab/dashboard/art/ (run art.py): cards fall back to the CDN, which the hosting may block")

    measured = measured_status(ladder)
    try:        # the last commit that touched the engine sources (the repository head is often a results commit)
        src_commit = subprocess.run(["git", "log", "-1", "--format=%h", "--", "src/"], cwd=ROOT, capture_output=True,
                                    text=True, check=True).stdout.strip() or None
    except (OSError, subprocess.CalledProcessError):
        src_commit = None
    data = {"generated_utc": index["generated_utc"], "engine_commit": index["engine_commit"], "engine_src_commit": src_commit,
            **SESSION, "tests": tests, "decks": index["decks"], "variants": index["variants"],
            "ladder": ladder, "replays": replays, "engine": engine,
            "human_review": {**human, "engine_commits_since": engine_since(human.get("generated"))}, "art": art,
            "measured": measured,
            "learning": learning_tracks(measured.get("training", {}).get("specialists", {})),
            "cards": [{"id": c.get("card_id"), "name": c["name"], "type": c["card_type"], "domains": c["domains"],
                       "energy": c.get("energy_cost") or 0, "power": c.get("power_cost") or 0,
                       "might": c.get("might") or 0, "super": c.get("super_type"), "tags": c.get("tags", []),
                       "set": c.get("set"), "image_url": c.get("image_url") or "",
                       # rules text for every card (~140 KB): the Cards drawer, the hover preview and the board's battlefield panel
                       **({"text": c["ability_text"]} if c.get("ability_text") else {})}
                      for c in reg.cards]}
    # 14 Nov: every matchup row repeated its opponent's label and deck path (1.7 MB). A test now lists its opponents
    # once (`opps`: [label, deck path]) and a matchup points at one (`o`); app/lib.js puts the two fields back on
    # every matchup when it loads the data, so the views read them as before.
    for tr in tests:
        opps, seen = [], {}
        for row in tr.get("decks") or []:
            for m in row.get("matchups") or []:
                key = (m.get("opponent"), m.get("opponent_deck"))
                if key not in seen:
                    seen[key] = len(opps); opps.append(list(key))
                m["o"] = seen[key]; m.pop("opponent", None); m.pop("opponent_deck", None)
        if opps:
            tr["opps"] = opps
    (DIST / "data.json").write_text(json.dumps(data, separators=(",", ":")))
    for name in PAGE_FILES:
        shutil.copy(LAB / "dashboard" / name, DIST / name)
    shutil.copytree(APP_DIR, DIST / "app", ignore=shutil.ignore_patterns("*.md", ".*"))
    (DIST / "home.json").write_text(json.dumps(load_home(), ensure_ascii=False, indent=1))
    size = sum(p.stat().st_size for p in DIST.rglob("*") if p.is_file())
    print(f"dist: {len(tests)} tests, {len(index['decks'])} decks, {len(replays)} replays, "
          f"{len(ladder.get('iterations', []))} learning iterations, {size / 1e6:.1f} MB")


if __name__ == "__main__":
    main(sys.argv[1:])
