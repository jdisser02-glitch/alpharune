#!/usr/bin/env python3
"""Assemble the lab dashboard's data files into lab/dashboard/dist/.

dist/index.html     the app shell (copied from lab/dashboard/ with app.css, board.js, board.css, engine.worker.js)
dist/app/*.js       the view modules and shared library (lab/dashboard/app/, copied as is)
dist/home.json      the Home screen's editable copy, from lab/dashboard/home.yml (or home.json)
dist/data.json      tests (full results), decks with stats, variants, learning ladder
dist/replays/*.json game records referenced by data.json (the most recent ones)

usage: build.py [--max-replays 40]
"""
import argparse
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "lab"
DIST = LAB / "dashboard" / "dist"
sys.path.insert(0, str(LAB))
import riftlab  # noqa: E402

PAGE_FILES = ["index.html", "app.css", "board.js", "board.css", "engine.worker.js"]   # shell, design system, board renderer, engine worker: copied as is
APP_DIR = LAB / "dashboard" / "app"                                                   # view modules + lib (ES modules), copied as a directory
# The WebAssembly engine for the Play tab (built with -DRIFTBOUND_WASM into build-wasm/) and the value net the
# in-browser bot evaluates with. Copied into dist/engine/ when present; the page hides the Play tab otherwise.
ENGINE_FILES = {"engine/riftbound.js": ROOT / "build-wasm" / "riftbound.js",
                "engine/riftbound.wasm": ROOT / "build-wasm" / "riftbound.wasm"}
DEFAULT_NET = LAB / "learn" / "nets" / "net_000.rbvn"
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
    for t in index["tests"]:
        tests.append(json.loads((ROOT / t["file"]).read_text()))

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
                                           "agreement_pct", "agreement_clean_pct", "compared_clean", "desyncs", "top_desync_causes",
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
    if all(p.exists() for p in ENGINE_FILES.values()):
        (DIST / "engine").mkdir(exist_ok=True)
        for rel, src in ENGINE_FILES.items():
            shutil.copy(src, DIST / rel)
            print(f"  {rel:<24} {src.stat().st_size / 1e6:.2f} MB  (from {src.relative_to(ROOT)})")
        net_rel_out = ""
        if net_path.exists():
            # The hosting serves only known file types, so the binary net travels base64-encoded in a JSON file.
            import base64
            net_rel_out = "engine/value_net.json"
            (DIST / net_rel_out).write_text(json.dumps({"source": str(net_path.relative_to(ROOT)),
                                                        "base64": base64.b64encode(net_path.read_bytes()).decode()}))
            print(f"  {net_rel_out:<24} {(DIST / net_rel_out).stat().st_size / 1e6:.2f} MB  (from {net_path.relative_to(ROOT)})")
        engine = {"js": "engine/riftbound.js", "wasm": "engine/riftbound.wasm",
                  "net": net_rel_out, "net_source": str(net_path.relative_to(ROOT)) if net_path.exists() else ""}
    else:
        print("  no WebAssembly engine in build-wasm/: the Play tab will be hidden")

    data = {"generated_utc": index["generated_utc"], "engine_commit": index["engine_commit"],
            **SESSION, "tests": tests, "decks": index["decks"], "variants": index["variants"],
            "ladder": ladder, "replays": replays, "engine": engine, "human_review": human,
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
