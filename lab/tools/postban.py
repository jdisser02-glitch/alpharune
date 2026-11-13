#!/usr/bin/env python3
"""Lists without the cards banned on 23 Nov 2026 (lab/meta/banned_2026-11-23.json).

No tournament has been played without these cards, so the lab has no real post-ban list to import. This makes the
nearest thing, by one fixed rule, so that a pilot still plays cards it knows:

    python3 lab/tools/postban.py patch LIST --like 'GLOB of the same legend's lists' --out lab/decks/postban/X.txt

  every banned main-deck card is replaced, copy by copy, by the card the legend's other lists play most (number of
  lists, then copies) that the list can still take (three copies at most, legal for the legend); a banned
  battlefield by the legend's most played battlefield that is not banned and not already in the list (else the most played of all tournament lists); a banned
  card in the sideboard is dropped. The chosen champion is never replaced by this command (Riven: see below).

    python3 lab/tools/postban.py riven-centres

  writes the Riven starting lists of the post-ban search under lab/decks/postban/riven_*: every tournament Riven
  list's main deck patched as above, and the hand-built ones, all written in the Riven, Shattered frame (a search
  uses a starting list's main deck only: the champion, runes and battlefields come from its own base, which is
  riven_frame_shattered.txt or riven_frame_deserter.txt). `check LIST...` prints the banned cards a list still plays.
"""
import argparse
import collections
import glob
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lab"))
import riftlab  # noqa: E402

BAN = json.loads((ROOT / "lab/meta/banned_2026-11-23.json").read_text())
BANNED = set(BAN["cards"]) | set(BAN["battlefields"])


def ranked(like, section):
    """Cards of `section` in the lists matching the globs, most played first: (lists, copies)."""
    lists, copies = collections.Counter(), collections.Counter()
    for pat in like:
        for f in sorted(glob.glob(str(ROOT / pat))):
            d = riftlab.parse_deck(Path(f))
            for c, n in d[section].items():
                lists[c] += 1; copies[c] += n
    return [c for c in sorted(lists, key=lambda c: (-lists[c], -copies[c], c)) if c not in BANNED]


def patch(deck, like, reg, champion=None, keep_frame=True):
    """(patched deck, [changes]). `champion`: a new chosen champion (its copies in the main deck are capped so the
    name stays at three)."""
    d = {s: collections.OrderedDict(deck[s]) for s in riftlab.SECTIONS}
    log = []
    if champion:
        d["champion"] = collections.OrderedDict({champion: 1})
    missing = 0
    for c in list(d["maindeck"]):
        if c in BANNED:
            missing += d["maindeck"].pop(c); log.append(f"-{deck['maindeck'][c]} {c}")
    for c in list(d["sideboard"]):
        if c in BANNED:
            d["sideboard"].pop(c); log.append(f"-{deck['sideboard'][c]} {c} (sideboard)")
    order = ranked(like, "maindeck")
    added = collections.Counter()
    while missing:
        for c in order:
            trial = dict(d); trial["maindeck"] = collections.OrderedDict(d["maindeck"]); trial["maindeck"][c] = trial["maindeck"].get(c, 0) + 1
            if trial["maindeck"][c] + d["champion"].get(c, 0) + d["sideboard"].get(c, 0) > 3:
                continue
            errs = [e for e in riftlab.validate(trial, reg) if "40 cards" not in e and "battlefield" not in e]
            if errs:
                continue
            d["maindeck"] = trial["maindeck"]; added[c] += 1; missing -= 1
            break
        else:
            raise SystemExit("no legal replacement left")
        # one copy per pass over the ranking, so the replacements spread like the lists they come from
        order = order[order.index(c) + 1:] + order[:order.index(c) + 1]
    log += [f"+{n} {c}" for c, n in added.items()]
    if keep_frame:
        # (the legend's own lists first, then every tournament list: Master Yi's two lists play both banned ones)
        bfs = ranked(like, "battlefields") + ranked(["lab/decks/meta/*/*.txt"], "battlefields")
        for b in list(d["battlefields"]):
            if b in BANNED:
                d["battlefields"].pop(b)
                new = next(x for x in bfs if x not in d["battlefields"])
                d["battlefields"][new] = 1; log.append(f"battlefield {b} -> {new}")
    return d, log


def still_banned(deck):
    return sorted({c for s in riftlab.SECTIONS for c in deck[s] if c in BANNED})


RIVEN_LIKE = ["lab/decks/meta/*/*riven*.txt"]
# Hand-built starting lists (13 Nov, lab/OPTIMISER_KNOWLEDGE.md): the 35 cards of decks/riven_best_field.txt that are
# still legal, plus four to seven cards for what the bans took (the champion's plan, the draw of Stellacorn Herder,
# the protection of Zhonya's Hourglass).
CORE = {"Astral Heron": 3, "Blade of the Exile": 3, "Scuttle Crab": 3, "Tideturner": 2, "Bedeviling Stranger": 3,
        "Reckless Innovator": 2, "Blitzcrank, Impassive": 2, "Defy": 3, "Discipline": 3, "Gust": 2, "Switcheroo": 2,
        "Rebuke": 2, "Traveling Merchant": 2, "Baron Nashor": 1, "Storm Raptor": 1, "Hush": 1}
HAND = {
    # the same deck with the most played Riven cards in the freed slots
    "hand_plain": {"Traveling Merchant": +1, "Reckless Innovator": +1, "Not So Fast": +1, "Star-Crossed": +1},
    # Riven, Shattered's plan: Equipment on her (2 damage per Equipment when she attacks, Weaponmaster equips on play)
    "hand_equipment": {"Baron Nashor": -1, "Storm Raptor": -1, "Hush": -1, "Doran's Shield": +2, "Edge of Night": +2,
                       "Sterak's Gage": +2, "Doran's Ring": +1},
    # Riven, Deserter's plan: cards that exhaust a friendly unit (each gives her +1 Might, and she readies a unit)
    "hand_exhaust": {"Baron Nashor": -1, "Storm Raptor": -1, "Hush": -1, "Ki Burst": +2, "Life from Stone": +2,
                     "Meditation": +1, "Entrenched Boar": +2},
    # draw in place of Stellacorn Herder, counters in place of Zhonya's Hourglass
    "hand_draw": {"Ezreal, Prodigy": +2, "Not So Fast": +1, "Traveling Merchant": +1},
}


# The two frames of the post-ban search. Battlefields: the two legal ones Riven lists play most (Hunters' Circle 16
# of 23 lists, Rakelstake 7) and Windswept Hillock (2, as Valley of Idols); the runes of the tournament lists
# (6/6 for Shattered, whose Equipment asks for Calm power; 5/7 for Deserter, a Chaos card).
FRAMES = {"shattered": ("Riven, Shattered", {"Calm Rune": 6, "Chaos Rune": 6}),
          "deserter": ("Riven, Deserter", {"Calm Rune": 5, "Chaos Rune": 7})}
BATTLEFIELDS = ["Hunters' Circle", "Rakelstake", "Windswept Hillock"]


def framed(base, main, frame="shattered"):
    d = dict(base)
    d["champion"] = collections.OrderedDict({FRAMES[frame][0]: 1})
    d["runes"] = collections.OrderedDict(FRAMES[frame][1])
    d["battlefields"] = collections.OrderedDict((b, 1) for b in BATTLEFIELDS)
    d["maindeck"] = collections.OrderedDict(sorted((k, v) for k, v in main.items() if v > 0))
    d["sideboard"] = collections.OrderedDict()
    return d


def riven_centres(reg):
    out = ROOT / "lab/decks/postban"
    base = riftlab.parse_deck(ROOT / "decks/riven_best_field.txt")
    seen = set()
    for f in sorted(glob.glob(str(ROOT / RIVEN_LIKE[0]))):
        d, log = patch(riftlab.parse_deck(Path(f)), RIVEN_LIKE, reg, keep_frame=False)
        key = tuple(sorted(d["maindeck"].items()))
        if key in seen:
            continue
        seen.add(key)
        d = framed(base, d["maindeck"])
        assert not riftlab.validate(d, reg) and not still_banned(d), (f, riftlab.validate(d, reg))
        p = out / f"riven_main_{Path(f).parent.name.rsplit('_', 1)[0][:28]}_{Path(f).stem}.txt"
        riftlab.write_deck(d, p, [f"Main deck of {Path(f).relative_to(ROOT)} without the cards banned on 23 Nov 2026",
                                  "(lab/tools/postban.py riven-centres): " + ", ".join(log) + ".",
                                  "Champion, runes and battlefields: the Riven, Shattered frame of the post-ban search."])
        print(p.relative_to(ROOT), "|", ", ".join(log))
    for name, ch in HAND.items():
        m = collections.Counter(CORE); m.update(ch)
        d = framed(base, m)
        assert not riftlab.validate(d, reg) and not still_banned(d), (name, riftlab.validate(d, reg))
        p = out / f"riven_{name}.txt"
        riftlab.write_deck(d, p, [f"Hand-built post-ban Riven main deck '{name}' (lab/tools/postban.py, 13 Nov 2026).",
                                  "Champion, runes and battlefields: the Riven, Shattered frame of the post-ban search."])
        print(p.relative_to(ROOT))
    m = collections.Counter(CORE); m.update(HAND["hand_plain"])
    for frame in FRAMES:
        d = framed(base, m, frame)
        assert not riftlab.validate(d, reg), riftlab.validate(d, reg)
        p = out / f"riven_frame_{frame}.txt"
        riftlab.write_deck(d, p, [f"Reference and frame of lab/specs/deckopt/riven-postban-{frame}.json (13 Nov 2026): champion",
                                  f"{FRAMES[frame][0]}, the legal cards of decks/riven_best_field.txt and the four most played Riven",
                                  "cards in the slots of Zhonya's Hourglass and Stellacorn Herder. A starting point, not a result."])
        print(p.relative_to(ROOT))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("patch"); p.add_argument("list"); p.add_argument("--like", nargs="+", required=True)
    p.add_argument("--out", required=True); p.add_argument("--champion")
    sub.add_parser("riven-centres")
    c = sub.add_parser("check"); c.add_argument("lists", nargs="+")
    a = ap.parse_args()
    reg = riftlab.Registry.load()
    if a.cmd == "patch":
        d, log = patch(riftlab.parse_deck(ROOT / a.list), a.like, reg, a.champion)
        errs = riftlab.validate(d, reg)
        if errs or still_banned(d):
            raise SystemExit(f"{a.list}: {errs} {still_banned(d)}")
        riftlab.write_deck(d, ROOT / a.out, [f"{a.list} without the cards banned on 23 Nov 2026 (lab/tools/postban.py patch):",
                                             ", ".join(log) + ".", "Not a list anyone registered: the nearest legal one by a fixed rule."])
        print(a.out, "|", ", ".join(log))
    elif a.cmd == "riven-centres":
        riven_centres(reg)
    else:
        for f in a.lists:
            print(f, still_banned(riftlab.parse_deck(ROOT / f)) or "clean")


if __name__ == "__main__":
    main()
