#!/usr/bin/env python3
"""Import public human Riftbound games from RiftLite replays into
lab/human_games/<date>_<legendA>_vs_<legendB>_<id>.json.

Only reads what a visitor sees on public pages (the public replay list, each
replay's page, its "Match log" and "Match decks" panels), one page at a time,
through the Playwright helper lab/tools/riftlite_scrape.js.

  python3 lab/tools/import_riftlite.py --list              # print the public replays
  python3 lab/tools/import_riftlite.py --id rl2_...        # import one (repeatable)
  python3 lab/tools/import_riftlite.py --all               # import every complete replay
  python3 lab/tools/import_riftlite.py --reparse           # re-run the parser on cached raw scrapes

Options: --step S (slider sweep step in seconds, default 30), --delay S (pause
between pages, default 3), --refresh (re-scrape even when a raw capture is
cached), --include-partial, --limit N.
"""
import argparse
import json
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(__file__).resolve().parent
OUT_DIR = ROOT / "lab" / "human_games"
RAW_DIR = OUT_DIR / "raw"
SCRAPER = TOOLS / "riftlite_scrape.js"
BASE = "https://www.riftlite.com"

# --------------------------------------------------------------------------
# Match-log parser: data-driven. Each rule is (kind, regex). The regex is
# matched against the line with the actor prefix already removed. Named groups
# become fields of the parsed event; `raw` always keeps the full text.
# --------------------------------------------------------------------------
CARD = r"(?P<card>.+?)"
PLACE = r"(?P<to>.+?)"
RULES = [
    # plays
    ("played", rf"^played {CARD} token to {PLACE}\.$"),
    ("played", rf"^played (?P<amount>\d+) {CARD} tokens? to {PLACE}\.$"),
    ("played", rf"^played {CARD} from (?P<from>hand|trash|deck|banishment) to {PLACE}\.$"),
    ("played", rf"^played {CARD} from (?P<from>hand|trash|deck|banishment)\.$"),
    ("played", rf"^played {CARD} from (?P<from>.+?)\.$"),
    ("played", rf"^Played {CARD} from (?P<from>hand|trash|deck|banishment)\.$"),
    ("played", rf"^Played {CARD} from (?P<from>.+?)\.$"),
    # movement / board
    ("discarded", rf"^moved {CARD} from (?P<from>hand) to (?P<to>trash)\.$"),
    ("killed", rf"^moved {CARD} from (?P<from>base|.+?) to (?P<to>trash)\.$"),
    ("moved", rf"^moved {CARD} from (?P<from>.+?) to {PLACE}\.$"),
    ("moved", rf"^moved {CARD} to {PLACE}\.$"),
    ("moved", rf"^moved {CARD} into deck \((?P<to>.+?)\)\.$"),
    ("duplicated", rf"^duplicated {CARD} in (?P<battlefield>.+?)\.$"),
    ("hidden_card_placed", rf"^put a hidden card to {PLACE}\.$"),
    ("exhausted", r"^exhausted (?P<amount>\d+) (?P<rune>\w+) runes?\.$"),
    ("exhausted", r"^exhausted (?P<amount>\d+) (?P<rune>\w+) runes? and readied (?P<readied>\d+) .*\.$"),
    ("exhausted", rf"^exhausted {CARD}\.$"),
    ("readied", r"^readied (?P<amount>\d+) (?P<rune>\w+) runes?\.$"),
    ("readied", rf"^readied {CARD}\.$"),
    ("recycled", r"^recycled (?P<amount>\d+) (?P<rune>\w+) runes?\.$"),
    ("recycled", r"^recycled a looked-at card to bottom of deck\.$"),
    ("recycled", rf"^recycled {CARD} from looked-at deck cards to bottom of deck\.$"),
    ("recycled", r"^recycled (?P<amount>\d+) looked-at cards? to bottom of deck(?: in random order)?(?:, including (?P<card>.+?))?\.$"),
    ("token_removed", rf"^removed {CARD} token from play\.$"),
    ("recycled", rf"^Recycled {CARD} from (?P<from>.+?) to bottom of deck\.$"),
    ("recycled", r"^recycled (?P<amount>\d+) selected cards? from deck peek into deck in random order\.$"),
    ("banished", rf"^Banished {CARD} from (?P<from>.+?)\.$"),
    ("banished", rf"^banished {CARD}(?: from (?P<from>.+?))?\.$"),
    ("channeled", r"^channeled (?P<amount>\d+) runes?\.$"),
    ("equipped", rf"^equipped {CARD} to (?P<to>.+?)\.$"),
    ("made_unit", rf"^made {CARD} a unit with (?P<with>.+?)\.$"),
    ("buff_applied", rf"^applied buff to {CARD}\.$"),
    ("buff_consumed", rf"^consumed buff from {CARD}\.$"),
    ("token_used", rf"^used a {CARD} token\.$"),
    ("burned", r"^burned (?P<amount>\d+) cards? from the top of their deck\.$"),
    ("extra_turn", rf"^queued an extra turn with {CARD}\.$"),
    ("control_given", rf"^gave control of {CARD}\.$"),
    ("floating_energy", r"^adjusted floating energy to (?P<amount>\d+)\.$"),
    ("floating_power", r"^adjusted floating power to (?P<amount>\d+)\.$"),
    ("repeat_marked", rf"^Marked Repeat for {CARD}\.$"),
    ("xp", r"^(?:Increased|Decreased) XP to (?P<amount>\d+)\. \((?P<delta>[+-]\d+)\)$"),
    # payment
    ("paid", r"^paid (?P<cost>.+?) — (?P<detail>.+)\.$"),
    # scoring
    ("conquered", r"^conquered (?P<battlefield>.+?) and scored (?P<amount>\d+)\.$"),
    ("conquered", r"^conquered (?P<battlefield>.+?) and drew (?P<drew>\d+)\.$"),
    ("conquered", r"^conquered (?P<battlefield>.+?)\.$"),
    ("held", r"^held (?P<battlefield>.+?) and scored (?P<amount>\d+)\.$"),
    ("scored", r"^scored (?P<amount>\d+) at turn start\. Score: (?P<score_from>\d+) → (?P<score_to>\d+)\.$"),
    ("scored", r"^scored (?P<amount>\d+)(?P<why>.*?)\.$"),
    ("scored", r"^set score to (?P<score_to>\d+)\. \((?P<delta>[+-]\d+)\)$"),
    # combat / showdown
    ("showdown", r"^ended showdown responses at (?P<battlefield>.+?)\.$"),
    ("showdown_dismissed", r"^dismissed showdown at (?P<battlefield>.+?)\.$"),
    ("passed_focus", r"^passed focus at (?P<battlefield>.+?)\.$"),
    ("chain_resolved", rf"^Chain resolved: {CARD}\.$"),
    # cards / information
    ("drew", r"^drew (?P<amount>\d+) cards?\.$"),
    ("looked", r"^looked at top (?P<amount>\d+) card\(s\) of their deck\.$"),
    ("deck_peek_closed", r"^closed deck peek\.$"),
    ("revealed_hand", r"^revealed their hand: (?P<cards>.+)\.$"),
    ("hid_hand", r"^hid their hand: (?P<cards>.+)\.$"),
    ("hid_hand", r"^hid (?P<cards>.+?) in their hand\.$"),
    ("deck_top", r"^put a looked-at card on top of their deck\.$"),
    ("revealed", rf"^revealed {CARD} on the board\.$"),
    ("revealed", rf"^revealed {CARD} from looked-at deck cards\.$"),
    ("revealed", rf"^revealed {CARD} in their hand\.$"),
    # turn structure
    ("ended_turn", r"^ended their turn\.$"),
    ("ended_phase", r"^ended (?P<phase>.+?) phase\.$"),
    ("rewound", r"^rewound to before their last action\.$"),
    ("rewound", r"^used Forward to restore the previous timeline\.$"),
    # pre-game
    ("battlefield_locked", r"^locked in a(?: random)? battlefield\.$"),
    ("battlefield_locked", r"^auto-selected the last remaining battlefield\.$"),
    ("battlefields_locked", r"^battlefields are locked\. Roll a d20 to decide first player\.$"),
    ("roll", r"^rolled (?P<roll>\d+)\. (?P<other>.+?) rolled (?P<other_roll>\d+)\.$"),
    ("initiative", r"^wins initiative \((?P<roll>\d+) vs (?P<other_roll>\d+)\) and decides who plays first\.$"),
    ("choose_starter", r"^must choose who starts\..*$"),
    ("choose_starter", r"^chooses who starts this game\.$"),
    ("first_turn", r"^chose (?P<first>.+?) to take the first turn\..*$"),
    ("sideboarding", r"^requested that all players skip sideboarding\.$"),
    ("sideboarding", r"^(?:accepted|declined) the request to skip sideboarding\.$"),
    ("sideboarding", r"^players agreed to skip sideboarding\..*$"),
    ("sideboarding", r"^locked in sideboarding\.$"),
    ("sideboarding", r"^sideboards are locked\..*$"),
    ("mulligan", r"^finalized mulligan \((?P<recycled>\d+) recycled, (?P<redrawn>\d+) redrawn\)\.$"),
    ("mulligan", r"^mulligans are complete\. Starting the game\.$"),
    ("capture_gap", r"^\[capture gap.*\]$"),
]
COMPILED = [(k, re.compile(rx)) for k, rx in RULES]
BATTLEFIELD_KINDS = {"conquered", "held", "showdown", "showdown_dismissed", "passed_focus"}


def parse_time(t):
    """'1:58' -> 118 seconds; None for anything odd (RiftLite shows a bogus
    huge value on the first entry of some replays)."""
    m = re.fullmatch(r"(?:(\d+):)?(\d+):(\d\d)", t or "")
    if not m:
        return None
    h, mi, s = (int(x) if x else 0 for x in m.groups())
    if h > 24:
        return None
    return h * 3600 + mi * 60 + s


def split_actor(text, players, capture_player):
    """Return (actor, rest, inferred). Actor names may contain spaces, so try
    the known names longest first; 'Both' is the system; actor-less lines
    ('Played X from hand.') are the capture player's own actions."""
    for name in sorted(players, key=len, reverse=True):
        if text.startswith(name + " "):
            return name, text[len(name) + 1:], False
    if text.startswith("Both "):
        return "both", text[5:], False
    if re.match(r"^(Played|Marked|Recycled|Banished|Increased|Decreased) ", text):
        return capture_player, text, True
    return None, text, False


def parse_line(text, players, capture_player):
    actor, rest, inferred = split_actor(text, players, capture_player)
    ev = {"actor": actor, "kind": "other", "raw": text}
    if inferred:
        ev["actor_inferred"] = True
    for kind, rx in COMPILED:
        m = rx.match(rest)
        if not m:
            continue
        ev["kind"] = kind
        for key, val in m.groupdict().items():
            if val is None:
                continue
            if key in ("amount", "recycled", "redrawn", "roll", "other_roll", "score_from", "score_to", "delta", "drew"):
                ev[key] = int(val)
            elif key == "cards":
                ev[key] = [c.strip() for c in re.split(r",\s*(?=[A-Z0-9])", val)]
            else:
                ev[key] = val.strip()
        if kind == "played" and "token" in rest and " token to " in rest:
            ev["token"] = True
        if kind == "played" and "from" not in ev:
            ev["from"] = "hand"
        if kind == "paid":
            cost = ev.get("cost", "")
            me = re.search(r"(\d+)E", cost)
            mp = re.search(r"(\d+)P", cost)
            ev["energy"] = int(me.group(1)) if me else 0
            ev["power"] = int(mp.group(1)) if mp else 0
        if kind in BATTLEFIELD_KINDS and "battlefield" not in ev and "to" in ev:
            ev["battlefield"] = ev["to"]
        if kind == "moved" and ev.get("to") not in ("base", "hand", "trash", "deck"):
            ev["battlefield"] = ev["to"]
        break
    return ev


# --------------------------------------------------------------------------
# List page
# --------------------------------------------------------------------------
MONTHS = {m: i for i, m in enumerate(
    "JAN FEB MAR APR MAY JUN JUL AUG SEP OCT NOV DEC".split(), 1)}


def parse_list_row(row):
    lines = row.get("lines", [])
    info = {"id": row.get("id"), "url": BASE + row["href"] if row.get("href") else None,
            "raw": " | ".join(lines), "partial": any(l.startswith("Partial capture") for l in lines),
            "recovered": "Recovered Atlas capture" in lines}
    for l in lines:
        m = re.match(r"^(\d+) ([A-Z]{3}) (\d{4})(?: · (.+))?$", l)
        if m:
            info["date"] = f"{int(m.group(3)):04d}-{MONTHS.get(m.group(2), 0):02d}-{int(m.group(1)):02d}"
            info["client"] = m.group(4)
            continue
        m = re.match(r"^(.+?) vs (.+?) · (BO\d|Match) · (.+)$", l)
        if m:
            info["players"] = [m.group(1), m.group(2)]
            info["format"] = m.group(3)
            info["result"] = m.group(4)
            continue
        m = re.match(r"^([\d,]+) messages$", l)
        if m:
            info["messages"] = int(m.group(1).replace(",", ""))
            continue
        m = re.match(r"^(.+?) vs (.+)$", l)
        if m and "title" not in info and "·" not in l:
            info["title"] = l
            info["legends_short"] = [m.group(1), m.group(2)]
    return info


# --------------------------------------------------------------------------
# Scraping
# --------------------------------------------------------------------------
def run_node(args, timeout=1800):
    cmd = ["node", str(SCRAPER)] + args + ["--quiet"]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if p.returncode != 0:
        raise RuntimeError(f"scraper failed: {' '.join(cmd)}\n{p.stderr[-2000:]}")
    return json.loads(p.stdout)


def fetch_list(refresh=False):
    cache = RAW_DIR / "list.json"
    if cache.exists() and not refresh:
        rows = json.loads(cache.read_text())
    else:
        rows = run_node(["list"])
        RAW_DIR.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps(rows, indent=1, ensure_ascii=False))
    return [parse_list_row(r) for r in rows if r.get("id")]


def fetch_replay(rid, step, refresh=False):
    cache = RAW_DIR / f"{rid}.json"
    if cache.exists() and not refresh:
        return json.loads(cache.read_text()), True
    raw = run_node(["replay", rid, "--step", str(step)])
    RAW_DIR.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps(raw, indent=1, ensure_ascii=False))
    return raw, False


# --------------------------------------------------------------------------
# Normalisation
# --------------------------------------------------------------------------
def slug(s):
    return re.sub(r"[^a-z0-9]+", "_", (s or "").lower()).strip("_") or "x"


def end_points(text, names):
    """Final POINTS per player from the board text of the last frame."""
    pts = {}
    for role in ("OPPONENT", "CAPTURE PLAYER"):
        m = re.search(role + r"\n(.+?)\n(?:[A-Z ]+\n)?POINTS\n(\d+)", text or "")
        if m and m.group(1) in names:
            pts[m.group(1)] = int(m.group(2))
    return pts


def build_game(raw, meta):
    heroes = (raw.get("header") or {}).get("heroes") or []
    cap = heroes[0] if heroes else {}
    opp = heroes[1] if len(heroes) > 1 else {}
    capture_player = cap.get("name") or (meta.get("players") or [None])[0]
    opponent = opp.get("name") or (meta.get("players") or [None, None])[1]
    players = [p for p in (capture_player, opponent) if p]

    events = []
    games = []
    for g in raw.get("games", []):
        score = {p: 0 for p in players}
        turns = {p: 0 for p in players}
        first = None
        gi = g["index"]
        start_idx = len(events)
        prev = None
        n_dup = 0
        for e in g["entries"]:
            ev = parse_line(e["text"], players, capture_player)
            rec = {"game": gi, "t": e["t"], "seconds": parse_time(e["t"]), "text": e["text"]}
            rec.update({k: v for k, v in ev.items() if k != "raw"})
            # a line identical to the previous one: usually genuine (two runes
            # exhausted in one second), in a few captures a site artefact; the
            # derived score/turn counts count each run once
            if prev == (e["t"], e["text"]):
                rec["repeat_of_previous"] = True
                n_dup += 1
                events.append(rec)
                continue
            prev = (e["t"], e["text"])
            events.append(rec)
            k = ev["kind"]
            if k in ("conquered", "held") and ev.get("actor") in score:
                score[ev["actor"]] += ev.get("amount", 0)
            elif k == "scored" and ev.get("actor") in score:
                score[ev["actor"]] = ev.get("score_to", score[ev["actor"]] + ev.get("amount", 0))
            elif k == "ended_turn" and ev.get("actor") in turns:
                turns[ev["actor"]] += 1
            elif k == "first_turn" and first is None:
                first = ev.get("first")
        retracted = [{"t": r["t"], "text": r["text"], "after_index": start_idx + r.get("after_index", 0)}
                     for r in g.get("retracted", [])]
        games.append({"game": gi, "events": [start_idx, len(events)], "first_player": first,
                      "turns_ended": turns, "score_from_log": score, "repeated_lines": n_dup,
                      "retracted_entries": retracted,
                      "t_first": g["entries"][0]["t"] if g["entries"] else None,
                      "t_last": g["entries"][-1]["t"] if g["entries"] else None})

    final_pts = end_points(raw.get("end_screen_text"), players)
    if games and final_pts:
        games[-1]["score_final_frame"] = final_pts

    n_parsed = sum(1 for e in events if e["kind"] != "other")
    kinds = {}
    for e in events:
        kinds[e["kind"]] = kinds.get(e["kind"], 0) + 1

    legends = {}
    for h in (cap, opp):
        if h.get("name"):
            legends[h["name"]] = {"legend": h.get("legend"),
                                  "cards": h.get("cards", [])}
    decks = raw.get("decks") or {}
    return {
        "source": "riftlite",
        "source_url": raw.get("url") or meta.get("url"),
        "id": raw.get("id"),
        "scraped_at": raw.get("scraped_at"),
        "date": meta.get("date"),
        "client": meta.get("client"),
        "players": players,
        "capture_player": capture_player,
        "opponent": opponent,
        "legends": legends,
        "format": meta.get("format"),
        "result": {"capture_player": meta.get("result"), "note":
                   "as shown on the public list, from the capture player's perspective"},
        "list_entry": meta.get("raw"),
        "messages_listed": meta.get("messages"),
        "partial_capture": meta.get("partial", False),
        "duration_ms": raw.get("duration_ms"),
        "decks": {"available": bool(decks.get("available")),
                  "panel_text": decks.get("text"),
                  "note": "RiftLite shows Match decks only to the replay owner; "
                          "hands revealed in the log (kind revealed_hand) are the public trace of the lists."},
        "games": games,
        "event_count": len(events),
        "parsed_count": n_parsed,
        "kind_counts": dict(sorted(kinds.items(), key=lambda kv: -kv[1])),
        "sweep": raw.get("sweep"),
        "events": events,
    }


def out_name(game, meta):
    legs = meta.get("legends_short") or [None, None]
    a = legs[0] or (game["legends"].get(game["capture_player"]) or {}).get("legend", "").split(",")[0]
    b = legs[1] or (game["legends"].get(game["opponent"]) or {}).get("legend", "").split(",")[0]
    return f"{game.get('date') or 'nodate'}_{slug(a)}_vs_{slug(b)}_{game['id']}.json"


# --------------------------------------------------------------------------
def print_list(rows):
    print(f"{'id':38} {'date':10} {'title':24} {'players':34} {'fmt':5} {'result':14} {'msgs':>6}  note")
    for r in rows:
        note = "PARTIAL" if r.get("partial") else ("recovered" if r.get("recovered") else "")
        print(f"{r['id']:38} {r.get('date',''):10} {r.get('title',''):24.24} "
              f"{' vs '.join(r.get('players', [])):34.34} {r.get('format',''):5} {r.get('result',''):14.14} "
              f"{r.get('messages',0):>6}  {note}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--id", action="append", default=[])
    ap.add_argument("--reparse", action="store_true", help="rebuild outputs from cached raw scrapes only")
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--include-partial", action="store_true")
    ap.add_argument("--step", type=float, default=30)
    ap.add_argument("--delay", type=float, default=3)
    ap.add_argument("--limit", type=int, default=50)
    args = ap.parse_args()

    rows = fetch_list(refresh=args.refresh and args.list)
    by_id = {r["id"]: r for r in rows}
    if args.list:
        print_list(rows)
        if not (args.all or args.id or args.reparse):
            return

    if args.reparse:
        ids = sorted(p.stem for p in RAW_DIR.glob("rl2_*.json"))
    elif args.all:
        ids = [r["id"] for r in rows if args.include_partial or not r.get("partial")]
    else:
        ids = args.id
    ids = ids[: args.limit]
    if not ids:
        ap.error("nothing to do: use --list, --all, --id or --reparse")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    summary = []
    for i, rid in enumerate(ids):
        meta = by_id.get(rid, {"id": rid, "url": f"{BASE}/replays/{rid}"})
        try:
            if args.reparse:
                raw, cached = json.loads((RAW_DIR / f"{rid}.json").read_text()), True
            else:
                raw, cached = fetch_replay(rid, args.step, refresh=args.refresh)
        except Exception as e:  # keep going with the other replays
            print(f"[{i+1}/{len(ids)}] {rid}: FAILED {e}", file=sys.stderr)
            summary.append((rid, meta, None))
            continue
        game = build_game(raw, meta)
        # drop stale files of the same id (renamed metadata)
        for old in OUT_DIR.glob(f"*_{rid}.json"):
            old.unlink()
        path = OUT_DIR / out_name(game, meta)
        path.write_text(json.dumps(game, indent=1, ensure_ascii=False) + "\n")
        summary.append((rid, meta, game))
        print(f"[{i+1}/{len(ids)}] {path.relative_to(ROOT)}  events={game['event_count']} "
              f"parsed={game['parsed_count']} games={len(game['games'])}"
              f"{' (cached)' if cached else ''}", file=sys.stderr)
        if not cached and i + 1 < len(ids):
            time.sleep(args.delay)

    print()
    print(f"{'id':38} {'players':30} {'legends':30} {'fmt':4} {'result':10} {'games':>5} {'events':>6} {'parsed':>6} {'other':>5}")
    for rid, meta, game in summary:
        if game is None:
            print(f"{rid:38} FAILED")
            continue
        legs = " vs ".join((game["legends"].get(p) or {}).get("legend", "?").split(",")[0] for p in game["players"])
        print(f"{rid:38} {' vs '.join(game['players']):30.30} {legs:30.30} {game['format'] or '':4} "
              f"{(game['result']['capture_player'] or ''):10.10} {len(game['games']):>5} {game['event_count']:>6} "
              f"{game['parsed_count']:>6} {game['event_count'] - game['parsed_count']:>5}")


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:  # e.g. `--list | head`
        sys.exit(0)
