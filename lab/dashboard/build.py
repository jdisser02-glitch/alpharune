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
    def track(L, pause=None):
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
                recent = (dt.datetime.now(dt.timezone.utc) - dt.datetime.fromisoformat(last)).total_seconds() < 3 * 3600
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
        out["training"]["specialists"][f.parent.name] = track(json.loads(f.read_text()), f.parent / "PAUSE")
    confirmed = []
    for f in sorted((LAB / "results").glob("*.json")):
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
    for f in sorted((LAB / "results").glob("*.json")):
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
    for f in sorted((LAB / "results").glob("*.json")):
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
    for stem, r in items:
        decks = r.get("decks") or []
        # Home's list figures are about Riven lists: a comparison of another legend's lists (Orianna against Riven,
        # 10 Nov) is not a Riven list check.
        if any(deck_legend(d.get("path", ""), d.get("label", "")) not in (None, "Riven") for d in decks):
            continue
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
        comparisons.append({"result": stem, "net": net_of(r.get("agent", "")), "started_utc": r.get("started_utc"), "reference": decks[0].get("label"),
                            "reference_rate": round(b_rate, 4), "lists": rows, "jury_legends": legends})
    comparisons = sorted(comparisons + ga_comps, key=lambda c: str(c.get("started_utc") or ""))
    for c in comparisons:
        if c.get("ga"):
            continue            # already counted from the search's own confirmation above
        for row in c["lists"]:
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
    retests = {}
    for c in plain:
        for row in c["lists"]:
            if row.get("path"):
                retests[row["path"]] = {"result": c["result"], "started_utc": c.get("started_utc"), "diff": row["diff"], "ci95": row["ci95"],
                                        "reference": c["reference"], "net": c["net"]}
    out["list"] = {"confirmed": confirmed, "stale_confirmations": stale, "comparisons": comparisons[-3:], "best": best,
                   "retests": retests, "nets_in_use": sorted(n for n in in_use if n)}
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
                        return m.group(1).strip()
    return riftlab_legend(path) or riftlab_legend(label.replace(" ", "_"))


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
    for t in index["tests"]:
        tr = json.loads((ROOT / t["file"]).read_text())
        # A test's deck rows carry the list's full stats; the page reads the card list from the deck index when the
        # list is indexed (tests.js deckSection) and never reads keywords or power symbols, so those stay out of
        # data.json (the artifact's per-file limit is 16 MB; genetic searches add hundreds of rows).
        for row in tr.get("decks") or []:
            s = row.get("stats")
            if isinstance(s, dict):
                for k in ("keywords", "power_symbols") + (("list",) if row.get("path") in indexed else ()):
                    s.pop(k, None)
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
    for d in index["decks"]:
        p = ROOT / d["path"]
        d["text"] = p.read_text() if p.exists() else ""

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

    data = {"generated_utc": index["generated_utc"], "engine_commit": index["engine_commit"],
            **SESSION, "tests": tests, "decks": index["decks"], "variants": index["variants"],
            "ladder": ladder, "replays": replays, "engine": engine,
            "human_review": {**human, "engine_commits_since": engine_since(human.get("generated"))}, "art": art,
            "measured": measured_status(ladder),
            "cards": [{"id": c.get("card_id"), "name": c["name"], "type": c["card_type"], "domains": c["domains"],
                       "energy": c.get("energy_cost") or 0, "power": c.get("power_cost") or 0,
                       "might": c.get("might") or 0, "super": c.get("super_type"), "tags": c.get("tags", []),
                       "set": c.get("set"), "image_url": c.get("image_url") or "",
                       # rules text for every card (~140 KB): the Cards drawer, the hover preview and the board's battlefield panel
                       **({"text": c["ability_text"]} if c.get("ability_text") else {})}
                      for c in reg.cards]}
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
