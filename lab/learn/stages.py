#!/usr/bin/env python3
"""One iteration of the learning loop (loop.py), split into stages that run on
several machines at once (riftlab.yml, the "p_*" training jobs):

  plan      reads the ladder: iteration number, current / reference / anchor
            nets, the loop settings, and the list of evaluation tasks;
  selfplay  shard k of S plays its share of the self-play games and writes
            data/learn/iter_<it>_s<k>.rbvd;
  train     merges the shards into iter_<it>.rbvd (one file per iteration:
            the trainer weights recency per file) and trains the candidate;
            with --variant K, a population member: the loop settings with
            VARIANTS[K]'s training overrides, written as net_<it>_vK;
  screen    one block of SCREEN_GAMES mirror games of variant K against the
            current net (seeds of their own);
  pick      the variant with the best screen rate: its candidate goes on to
            the gate, the panel and the yardsticks on fresh seeds, so having
            picked the best of several does not inflate its gate rate;
  gate      one evaluation task: a mirror gate block, one panel matchup, or
            one yardstick (random, the reference net, the anchor nets);
  decide    adds the task results up, applies the same rules as loop.py
            (SPRT on all the gate games, then --accept; the panel floor) and
            writes the ladder row.

Every stage reads the loop's settings from the plan, so all machines play and
judge with the same bot; seeds come from loop.seed_base, so no two shards or
tasks replay the same deals.

usage:
  stages.py plan --out plan.json -- [loop.py options]
  stages.py selfplay --plan plan.json --shard K --shards S
  stages.py train --plan plan.json --out cand.json [--variant K]
  stages.py screen --plan plan.json --cand cand_K.json --variant K --block B --out screen.json
  stages.py pick --plan plan.json --out cand.json --cands cand_*.json --screens screen_*.json
  stages.py gate --plan plan.json --cand cand.json --task gate:0 --out result.json
  stages.py decide --plan plan.json --cand cand.json RESULT.json [...]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import os
import random
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import loop  # noqa: E402
from loop import seed_base  # noqa: E402

PER_PAIR = 4
# Training settings tried side by side every iteration (population). Each is a
# set of loop.py option overrides; the screen keeps the best for the gate.
VARIANTS = [
    {"epochs": 2, "lr": 3e-4, "q_weight": 0.5},           # variant 2 with half the value target from the search (RBV4)
    {"epochs": 1, "lr": 2e-4},                            # gentler still
    {"epochs": 2, "lr": 3e-4},                            # gentle fine-tune: won the 4 screens of 156-159
    {"epochs": 3, "lr": 3e-4},                            # a little more of the gentle one
]
# History: from 156 to 159 the screens (120 games each vs net_128) gave
#   loop's own 47.9%, no human imitation 47.1%, gentle (2 ep, 3e-4) 54.8%
#   (won all four), gentle + human 0.1 + prev 0.3 (3 ep, 5e-4) 51.5%;
# the gentle winner then scored 49.0% on the fresh-seed gate (winner's curse).
# From 161 to 183 the screen picked variant 0 (the loop's own settings) 2 times in 23, the gentle ones the
# rest; from 184 variant 0 is the gentle one with half its value target from the search (q_weight 0.5): its
# screen rates against variant 2's, on the same data, say whether that target helps.
# Specialists evolve their training settings (population-based training): the first iteration tries these, every
# later one keeps the settings whose candidate won the last screen and adds 3 mutations of them (learning rate x/÷2,
# epochs ±1, the share of the search's value in the value target, the weight of the legend's human moves, 64 or 128
# hidden units). A wider net starts as the
# narrower one (train_policy.py widening), and a variant is never narrower than the current specialist.
SPEC_SEED_VARIANTS = [
    {"epochs": 2, "lr": 3e-4},
    {"epochs": 2, "lr": 3e-4, "hidden": 128},
    {"epochs": 3, "lr": 3e-4, "hidden": 128, "q_weight": 0.5},
    {"epochs": 1, "lr": 2e-4, "q_weight": 0.5},
]
GENES = {"lr": (5e-5, 2e-3), "epochs": (1, 8), "q_weight": (0.0, 0.25, 0.5, 0.75), "hidden": (64, 128, 256),
         "spec_human_weight": (1.0, 3.0, 10.0)}


def mutate(parent: dict, rng: random.Random, min_hidden: int) -> dict:
    """One or two genes of `parent` changed (GENES)."""
    child = dict(parent)
    for gene in rng.sample(sorted(GENES), rng.choice([1, 2])):
        if gene == "lr":
            lo, hi = GENES["lr"]
            child["lr"] = float(min(hi, max(lo, child.get("lr", 3e-4) * rng.choice([0.5, 2.0]))))
        elif gene == "epochs":
            lo, hi = GENES["epochs"]
            child["epochs"] = int(min(hi, max(lo, child.get("epochs", 2) + rng.choice([-1, 1]))))
        elif gene == "q_weight":
            child["q_weight"] = rng.choice([q for q in GENES["q_weight"] if q != child.get("q_weight", 0.0)])
        elif gene == "spec_human_weight":   # how much the legend's own human moves weigh (imitation)
            child["spec_human_weight"] = rng.choice([w for w in GENES["spec_human_weight"] if w != child.get("spec_human_weight", 3.0)])
        else:
            child["hidden"] = rng.choice([h for h in GENES["hidden"] if h >= min_hidden])
    return child


def spec_variants(ladder: dict, it: int, seed: int, min_hidden: int, default_hidden: int) -> list[dict]:
    """The specialist's population for iteration `it` (see SPEC_SEED_VARIANTS)."""
    rows = [r for r in ladder.get("iterations", []) if r.get("screen") and r.get("screen_pick") is not None]
    if rows:
        last = rows[-1]
        parent = dict(last["screen"][str(last["screen_pick"])].get("overrides") or {})
        rng = random.Random(seed * 7919 + it)
        out, seen = [parent], set()
        while len(out) < 4:
            c = mutate(parent, rng, min_hidden)
            key = json.dumps(c, sort_keys=True)
            if key not in seen and c != parent:
                seen.add(key); out.append(c)
    else:
        out = [dict(v) for v in SPEC_SEED_VARIANTS]
    for v in out:
        v["hidden"] = max(int(v.get("hidden", default_hidden)), min_hidden)
    return out


SCREEN_BLOCKS = 6       # screen blocks per variant
SCREEN_GAMES = 20       # games per screen block (both seat orders)


def settings(plan: dict):
    a = loop.make_parser().parse_args(plan["args"])
    loop.use_track(a.specialist or None)
    return a


def spec(a):
    """The specialist's table (loop.SPECIALISTS), or None for the general net."""
    return loop.SPECIALISTS[a.specialist] if a.specialist else None


def mirror_decks(a):
    """Decks of the mirror games (screen, gate): the legend's own mirror for a specialist."""
    s = spec(a)
    return (s["mirror"], s["mirror"]) if s else (loop.GATE_DECK, loop.GATE_DECK)


def pilot(plan, deck):
    """Net that plays a jury deck: its legend's specialist when it has one, else the general net."""
    other = loop.specialist_of_deck(deck)
    return (plan.get("pilots") or {}).get(other) or plan["general"]


def spec_vs(L, it, me, opp, legend_deck, opp_deck, games, seed):
    """`games` games of `me` on legend_deck against `opp` on opp_deck, half with the legend on each seat
    (the same deals). Returns (wins of me, games)."""
    half = max(1, games // 2)
    w1, l1 = L.batch(legend_deck, opp_deck, me, opp, half, seed)
    l2, w2 = L.batch(opp_deck, legend_deck, opp, me, half, seed)
    # a batch killed by the timeout returns no win at all: its games are not counted (they used to count as losses)
    return w1 + w2, sum(max(2, half) for won in (w1 + l1, w2 + l2) if won > 0)


def continuous(a) -> bool:
    """--continuous on a specialist (lab/learn/TRAINING_KNOWLEDGE.md): one candidate, adopted unless clearly worse."""
    return bool(a.specialist and getattr(a, "continuous", False))


def panel_decks(a) -> list[str]:
    """Opponents of the panel: the held-out 'measure' lists in continuous mode, else the jury."""
    s = spec(a)
    return s.get("measure") or s["jury"] if continuous(a) else s["jury"]


def tasks_for(a) -> list[str]:
    blocks = max(1, a.gate_max // a.gate_block)
    out = [f"gate:{b}" for b in range(blocks)]
    if continuous(a):
        # The mirror against the net it would replace and the paired panel on the held-out opponents are the
        # guard; the frozen anchors (and the panel's own rates) are the progress curve.
        s = spec(a)
        mirror = [f"gate:{b}" for b in range(max(1, a.spec_mirror_games // a.gate_block))]
        return (mirror + [f"panel:{m}:0" for m in range(len(panel_decks(a)))]
                + [f"yard:anchor:{k}" for k in range(len(s.get("anchors") or []))] + ["yard:random"])
    if a.specialist:
        # A specialist is judged on the jury (candidate and current specialist on the same deals, per jury deck
        # and per legend deck, its first two), with a shorter mirror as a guard.
        s = spec(a)
        mirror = [f"gate:{b}" for b in range(max(1, a.spec_mirror_games // a.gate_block))]
        jury = [f"panel:{m}:{j}" for m in range(len(s["jury"])) for j in range(min(2, len(s["decks"])))]
        return mirror + jury + ["yard:random"]
    if a.panel_games > 0:
        out += [f"panel:{m}" for m in range(len(loop.PANEL))]
    out += ["yard:random", "yard:reference", "yard:anchors"]
    return out


def cmd_plan(args, rest):
    a = loop.make_parser().parse_args(rest)
    loop.use_track(None)
    general = loop.load_ladder().get("current_net")
    loop.use_track(a.specialist or None)
    ladder = loop.load_ladder()
    current = ladder.get("current_net")
    if current and not (loop.ROOT / current).exists():
        current = None
    reference = ladder.get("reference_net")
    if reference and not (loop.ROOT / reference).exists():
        reference = None
    committed = current
    if continuous(a):
        # The adopted net travels between iterations in the Actions cache (ladder head_net); when the cache lost
        # it, the last committed net (current_net) carries on.
        head = ladder.get("head_net")
        if head and (loop.ROOT / head).exists():
            current = head
    if a.specialist:
        # A specialist starts as the general net and keeps it as its reference until it adopts one of its own.
        current = current or general
        reference = reference or general
    plan = {"it": len(ladder["iterations"]), "current": current, "reference": reference,
            "anchors": [] if a.specialist else loop.pick_anchors(ladder), "args": rest, "tasks": tasks_for(a),
            "specialist": a.specialist or None, "general": general,
            "pilots": {k: v for k, v in loop.specialist_pilots().items() if k != a.specialist} if a.specialist else {},
            "ladder": os.path.relpath(loop.LADDER, loop.ROOT), "net_prefix": loop.NET_PREFIX,
            "league": loop.league_opponents(ladder, current, a.league_nets) if a.league > 0 and current and not a.specialist else [],
            "variants": ([{"hidden": max(loop.rbpn_hidden(loop.ROOT / current), a.hidden) if current and str(current).endswith(".rbpn") else a.hidden}]
                         if continuous(a) else
                         spec_variants(ladder, len(ladder["iterations"]), a.seed,
                                       max(loop.rbpn_hidden(loop.ROOT / current), a.hidden) if current and str(current).endswith(".rbpn") else a.hidden,
                                       a.hidden) if a.specialist else VARIANTS),
            "screen": [] if continuous(a) else [f"{k}:{b}" for k in range(len(VARIANTS)) for b in range(SCREEN_BLOCKS)],
            "continuous": continuous(a), "committed": committed,
            "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"), "t0": time.time()}
    Path(args.out).write_text(json.dumps(plan, indent=1))
    print(json.dumps({k: plan[k] for k in ("it", "current", "committed", "continuous", "tasks")}))


def cmd_selfplay(args, _):
    plan = json.loads(Path(args.plan).read_text())
    a = settings(plan)
    it, current = plan["it"], plan["current"]
    L = loop.Loop(a)
    loop.DATA.mkdir(parents=True, exist_ok=True)
    out = loop.DATA / f"iter_{it:03d}_s{args.shard}.rbvd"
    out.unlink(missing_ok=True)
    games = a.games * 3 if current is None else a.games
    if a.specialist:
        return selfplay_specialist(args, plan, a, L, out, games)
    pairs = L.self_play_pairs(random.Random(a.seed + it), loop.deck_pool(), max(1, games // PER_PAIR))
    league = loop.league_schedule(a.seed, it, len(pairs), a.league, plan.get("league") or [])
    t0 = time.time()
    met = {}
    for k, (d1, d2) in enumerate(pairs):
        if k % args.shards != args.shard:
            continue
        me = L.bot(current, getattr(a, "selfplay_sims", 0) or None)
        if league[k] is None:
            L.batch(d1, d2, me, me, PER_PAIR, seed_base(it, "selfplay", k), out)
            continue
        opp, seat = league[k]
        sp = getattr(a, "selfplay_sims", 0) or None
        other = L.material_bot(sp) if opp == loop.MATERIAL else L.bot(opp, sp)
        a1, a2 = (me, other) if seat == 1 else (other, me)
        w1, w2 = L.batch(d1, d2, a1, a2, PER_PAIR, seed_base(it, "selfplay", k), out, value_player=seat)
        m = met.setdefault(opp, {"games": 0, "wins": 0})
        m["games"] += w1 + w2
        m["wins"] += w1 if seat == 1 else w2
    meta = {"shard": args.shard, "minutes": round((time.time() - t0) / 60, 1), "batches": len(L.batches), "league": met}
    out.with_suffix(".json").write_text(json.dumps(meta))
    print(json.dumps(meta))


def selfplay_specialist(args, plan, a, L, out, games):
    """A specialist's self-play: the current specialist on one of its legend's decks against a jury deck
    piloted by pilot() (only the specialist's decisions recorded), and --spec-mirror of the pairs in the
    legend's mirror against itself (both seats recorded)."""
    sp = getattr(a, "selfplay_sims", 0) or None    # deeper search for the recorded games only
    s, it, me = spec(a), plan["it"], L.bot(plan["current"], sp, getattr(a, "selfplay_extra", ""))
    rng = random.Random(a.seed * 31 + it)
    t0, met = time.time(), {}
    for k in range(max(1, games // PER_PAIR)):
        # --spec-riven-weight: Riven jury decks drawn that many times as often (the matchup the simulation gets wrong)
        if continuous(a) and s.get("train"):
            # the 'train' opponents by their share of the legend's real games; the mirror meets any of the legend's lists
            pool, w = [d for d, _ in s["train"]], [x for _, x in s["train"]]
            mirror_deck = rng.choice(s["decks"])
        else:
            pool = s["jury"]
            w = [getattr(a, "spec_riven_weight", 1.0) if "riven" in Path(d).name else 1.0 for d in pool]
            mirror_deck = s["mirror"]
        legend_deck, opp_deck, mirror = rng.choice(s["decks"]), rng.choices(pool, weights=w)[0], rng.random() < a.spec_mirror
        if k % args.shards != args.shard:
            continue
        seed = seed_base(it, "selfplay", k)
        if mirror:
            L.batch(legend_deck, mirror_deck, me, me, PER_PAIR, seed, out)
            continue
        opp = L.bot(pilot(plan, opp_deck), sp)
        seat = 1 + (k // args.shards) % 2
        d1, d2, a1, a2 = (legend_deck, opp_deck, me, opp) if seat == 1 else (opp_deck, legend_deck, opp, me)
        w1, w2 = L.batch(d1, d2, a1, a2, PER_PAIR, seed, out, value_player=seat)
        m = met.setdefault(opp_deck, {"games": 0, "wins": 0})
        m["games"] += w1 + w2
        m["wins"] += w1 if seat == 1 else w2
    meta = {"shard": args.shard, "minutes": round((time.time() - t0) / 60, 1), "batches": len(L.batches), "jury": met}
    out.with_suffix(".json").write_text(json.dumps(meta))
    print(json.dumps(meta))


def merge_shards(it: int) -> Path:
    """iter_<it>_s*.rbvd -> iter_<it>.rbvd: the first file whole, the others
    without their 12-byte header (RBV3/RBV4 magic, feature dim, vocab size); every shard must
    carry the same header."""
    shards = sorted(loop.DATA.glob(f"iter_{it:03d}_s*.rbvd"))
    if not shards:
        raise SystemExit(f"no self-play shard for iteration {it} in {loop.DATA}")
    out = loop.DATA / f"iter_{it:03d}.rbvd"
    head = None
    with open(out, "wb") as f:
        for i, s in enumerate(shards):
            b = s.read_bytes()
            if len(b) < 12 or b[:4] not in (b"RBV3", b"RBV4"):
                raise SystemExit(f"{s}: not an RBV3/RBV4 file")
            if head is None:
                head = b[:12]
            elif b[:12] != head:
                raise SystemExit(f"{s}: header differs from {shards[0]}")
            f.write(b if i == 0 else b[12:])
    for s in shards:
        s.unlink()
    return out


def cmd_train(args, _):
    plan = json.loads(Path(args.plan).read_text())
    a = settings(plan)
    it = plan["it"]
    sp = [json.loads(p.read_text()) for p in loop.DATA.glob(f"iter_{it:03d}_s*.json")]
    merge_shards(it)
    for p in loop.DATA.glob(f"iter_{it:03d}_s*.json"):
        p.unlink()
    loop.NETS.mkdir(parents=True, exist_ok=True)
    row = {"iteration": it, "started_utc": plan["started_utc"], "player": loop.Loop(a).bot(plan["current"]),
           "self_play_games": a.games, "sims": a.sims, "agent": a.agent,
           "trainer": "train_policy" if a.policy else "train_value",
           "self_play_minutes": max((m["minutes"] for m in sp), default=None), "shards": len(sp)}
    league = {}
    for m in sp:
        for opp, r in (m.get("league") or {}).items():
            agg = league.setdefault(opp, {"games": 0, "wins": 0})
            agg["games"] += r["games"]; agg["wins"] += r["wins"]
    if league:
        row["league"] = {opp: dict(r, rate=round(r["wins"] / max(1, r["games"]), 3)) for opp, r in league.items()}
    jury = {}
    for m in sp:
        for opp, r in (m.get("jury") or {}).items():
            agg = jury.setdefault(opp, {"games": 0, "wins": 0})
            agg["games"] += r["games"]; agg["wins"] += r["wins"]
    if jury:
        row["self_play_jury"] = {opp: dict(r, rate=round(r["wins"] / max(1, r["games"]), 3)) for opp, r in jury.items()}
    if a.specialist:
        row["specialist"] = a.specialist
    suffix = ""
    if args.variant is not None:
        over = plan.get("variants", VARIANTS)[args.variant]
        for k, v in over.items():
            setattr(a, k, v)
        row["variant"] = {"index": args.variant, "overrides": over}
        suffix = f"_v{args.variant}"
    cand = loop.train_candidate(a, it, plan["current"], row, suffix)
    Path(args.out).write_text(json.dumps({"cand": cand, "row": row}, indent=1))
    print(cand)


def cmd_screen(args, _):
    """Block `block` of variant `variant`: SCREEN_GAMES mirror games against the current net."""
    plan = json.loads(Path(args.plan).read_text())
    a = settings(plan)
    it = plan["it"]
    cand = json.loads(Path(args.cand).read_text())["cand"]
    L = loop.Loop(a)
    seed = seed_base(it, "screen", args.variant * SCREEN_BLOCKS + args.block)
    if a.specialist:
        # Specialists screen on the jury (block b: jury deck b mod the jury), the measure they are adopted on;
        # every variant plays the same blocks on the same seeds, so their rates compare.
        s = spec(a)
        opp_deck = s["jury"][args.block % len(s["jury"])]
        w, g = spec_vs(L, it, L.bot(cand), L.bot(pilot(plan, opp_deck)), s["decks"][0], opp_deck, SCREEN_GAMES,
                       seed_base(it, "screen", args.block))
        l = g - w
    else:
        d1, d2 = mirror_decks(a)
        w, l, g = L.match({"matches": []}, it, L.bot(cand), L.bot(plan["current"]), SCREEN_GAMES,
                          seed, "screen", d1, d2, record=False)
    res = {"variant": args.variant, "block": args.block, "wins": w, "losses": l, "games": g,
           "batches": [list(b) for b in L.batches]}
    Path(args.out).write_text(json.dumps(res))
    print(json.dumps({k: v for k, v in res.items() if k != "batches"}))


def pick(plan: dict, cands: dict, screens: list[dict]) -> dict:
    """The candidate of the variant with the best screen rate (ties: lower index),
    with every variant's screen in its row."""
    tot = {}
    for r in screens:
        t = tot.setdefault(r["variant"], [0, 0])
        t[0] += r["wins"]; t[1] += r["games"]
    if not screens:
        # no screen (continuous training): the one candidate, or the one with the lowest held-out loss
        best = min(sorted(cands), key=lambda k: cands[k]["row"].get("val_loss") or 0.0)
        return json.loads(json.dumps(cands[best]))
    missing = [k for k in cands if k not in tot] + [k for k in tot if k not in cands]
    if missing:
        raise SystemExit(f"screen and candidates do not match: {missing}")
    best = max(sorted(tot), key=lambda k: tot[k][0] / tot[k][1])
    out = json.loads(json.dumps(cands[best]))
    out["row"]["screen"] = {str(k): {"games": n, "wins": w, "rate": round(w / n, 4),
                                     "overrides": plan.get("variants", VARIANTS)[k]} for k, (w, n) in sorted(tot.items())}
    out["row"]["screen_pick"] = best
    return out


def cmd_pick(args, _):
    plan = json.loads(Path(args.plan).read_text())
    cands = {}
    for f in args.cands:
        c = json.loads(Path(f).read_text())
        cands[c["row"]["variant"]["index"]] = c
    screens = [json.loads(Path(f).read_text()) for f in args.screens]
    out = pick(plan, cands, screens)
    Path(args.out).write_text(json.dumps(out, indent=1))
    print(out["cand"], json.dumps(out["row"].get("screen")))


def cmd_gate(args, _):
    plan = json.loads(Path(args.plan).read_text())
    a = settings(plan)
    it, current, reference = plan["it"], plan["current"], plan["reference"]
    cand = json.loads(Path(args.cand).read_text())["cand"]
    L = loop.Loop(a)
    ladder = {"matches": []}
    kind, _, idx = args.task.partition(":")
    res = {"task": args.task, "kind": kind}
    d1, d2 = mirror_decks(a)
    if kind == "gate":
        w, l, g = L.match(ladder, it, L.bot(cand), L.bot(current), a.gate_block, seed_base(it, "gate", int(idx)), "gate", d1, d2)
        res.update(wins=w, losses=l, games=g)
    elif kind == "panel" and a.specialist:
        # Paired: the candidate and the current specialist each play the legend against this jury deck on the
        # same deals; the candidate must not do worse than the net it would replace.
        s = spec(a)
        m, _, j = idx.partition(":")
        m, js = int(m), ([int(j)] if j else range(min(2, len(s["decks"]))))
        opp_deck = panel_decks(a)[m]
        opp = L.bot(pilot(plan, opp_deck))
        per = max(4, a.spec_jury_games if j else a.panel_games)
        cw = cn = bw = bn = 0
        for j in js:
            legend_deck = s["decks"][j]
            seed = seed_base(it, "panel", 2 * m + j)
            w, n = spec_vs(L, it, L.bot(cand), opp, legend_deck, opp_deck, per, seed)
            cw += w; cn += n
            w, n = spec_vs(L, it, L.bot(current), opp, legend_deck, opp_deck, per, seed)
            bw += w; bn += n
        res.update(name=Path(opp_deck).stem, deck=opp_deck, wins=cw, losses=cn - cw, games=cn, base_wins=bw, base_games=bn)
    elif kind == "panel":
        m = int(idx)
        name, d_a, d_b = loop.PANEL[m]
        per = max(4, a.panel_games // 2)
        w1, l1, g1 = L.match(ladder, it, L.bot(cand), L.bot(current), per, seed_base(it, "panel", 2 * m),
                             "panel:" + name, d_a, d_b, record=False)
        w2, l2, g2 = L.match(ladder, it, L.bot(cand), L.bot(current), per, seed_base(it, "panel", 2 * m + 1),
                             "panel:" + name, d_b, d_a, record=False)
        res.update(name=name, wins=w1 + w2, losses=l1 + l2, games=g1 + g2)
    elif kind == "yard" and idx == "random":
        r_w, _, r_n = L.match(ladder, it, L.bot(cand), "random", a.yardstick_games, seed_base(it, "vs_random"), "vs_random", d1, d2)
        res["vs_random"] = {"games": r_n, "wins": r_w, "rate": round(r_w / r_n, 4)}
    elif kind == "yard" and idx.startswith("anchor:"):
        # the candidate against one frozen net of the specialist's 'anchors', in the legend's mirror
        k = int(idx.split(":")[1])
        anc = spec(a)["anchors"][k]
        if anc != cand:
            g_w, _, g_n = L.match(ladder, it, L.bot(cand), L.bot(anc), a.spec_anchor_games, seed_base(it, "anchor", k), "vs_anchor", d1, d2)
            res["vs_anchors"] = {anc: {"games": g_n, "wins": g_w, "rate": round(g_w / max(1, g_n), 4)}}
    elif kind == "yard" and idx == "reference":
        if reference and reference != cand:
            f_w, _, f_n = L.match(ladder, it, L.bot(cand), L.bot(reference), a.yardstick_games,
                                  seed_base(it, "vs_reference"), "vs_reference")
            res["vs_reference"] = {"opponent": L.bot(reference), "games": f_n, "wins": f_w, "rate": round(f_w / f_n, 4)}
    elif kind == "yard" and idx == "anchors":
        anchors = {}
        if a.anchor_games > 0:
            for j, anc in enumerate(x for x in plan["anchors"] if x != cand):
                g_w, _, g_n = L.match(ladder, it, L.bot(cand), L.bot(anc), a.anchor_games, seed_base(it, "anchor", j), "vs_anchor")
                anchors[anc] = {"games": g_n, "wins": g_w, "rate": round(g_w / g_n, 4)}
        if anchors:
            res["vs_anchors"] = anchors
    else:
        raise SystemExit(f"unknown task {args.task}")
    res["matches"] = ladder["matches"]
    res["batches"] = [list(b) for b in L.batches]
    Path(args.out).write_text(json.dumps(res, indent=1))
    print(json.dumps({k: v for k, v in res.items() if k not in ("matches", "batches")}))


def decide(plan: dict, cand_info: dict, results: list[dict], ladder: dict) -> dict:
    """The ladder row of the iteration, with the same rules as loop.py; updates
    `ladder` (matches, current / reference / anchor nets, Elo, the row)."""
    a = settings(plan)
    it, current, reference = plan["it"], plan["current"], plan["reference"]
    cand, row = cand_info["cand"], dict(cand_info["row"])
    missing = set(plan["tasks"]) - {r["task"] for r in results}
    if missing:
        raise SystemExit(f"missing task results: {sorted(missing)}")
    for r in results:
        ladder.setdefault("matches", []).extend(r.get("matches", []))
    gate = [r for r in results if r["kind"] == "gate"]
    wins, losses, n = (sum(r[k] for r in gate) for k in ("wins", "losses", "games"))
    llr = loop.sprt_llr(wins, losses)
    upper, lower = math.log((1 - 0.05) / 0.05), math.log(0.05 / (1 - 0.05))
    decision = "better" if llr >= upper else "worse" if llr <= lower else ("rate" if n >= a.gate_min else "short")
    lo, hi = loop.riftlab.wilson(wins, n)
    row["vs_current"] = {"opponent": loop.Loop(a).bot(current), "games": n, "wins": wins, "rate": round(wins / n, 4),
                         "ci95": [round(lo, 4), round(hi, 4)], "llr": round(llr, 3), "sprt": decision}
    mirror_ok = current is None or decision == "better" or (decision == "rate" and wins / n >= a.accept)
    if a.specialist and current is not None:
        # The mirror is a guard for a specialist: not clearly worse than the net it would replace.
        mirror_ok = wins / n >= a.spec_mirror_floor
        row["vs_current"]["guard_floor"] = a.spec_mirror_floor
    panel = [r for r in results if r["kind"] == "panel"]
    panel_ok = True
    if panel and a.specialist:
        c_w, c_n = sum(r["wins"] for r in panel), sum(r["games"] for r in panel)
        b_w, b_n = sum(r["base_wins"] for r in panel), sum(r["base_games"] for r in panel)
        pc, pb = c_w / c_n, b_w / b_n
        diff = pc - pb
        half = 1.96 * math.sqrt(pc * (1 - pc) / c_n + pb * (1 - pb) / b_n)   # unpaired: the shared deals only narrow it
        # Adopted on the jury: the candidate must beat the current specialist there by --spec-jury-accept.
        # Continuous training turns the rule round: refused only when it trails by more than --spec-guard.
        need = a.spec_guard if continuous(a) else a.spec_jury_accept
        panel_ok = diff >= need
        per = {}
        for r in panel:
            p_ = per.setdefault(r["name"], {"deck": r["deck"], "games": 0, "wins": 0, "base_games": 0, "base_wins": 0})
            for k in ("games", "wins", "base_games", "base_wins"):
                p_[k] += r[k]
        row["panel"] = {"games": c_n, "wins": c_w, "rate": round(pc, 4), "base_rate": round(pb, 4),
                        "diff": round(diff, 4), "diff_ci95": [round(diff - half, 4), round(diff + half, 4)],
                        "accept": need, "ok": panel_ok, "primary": True, "held_out": continuous(a),
                        "matchups": {k: {"deck": v["deck"], "games": v["games"], "rate": round(v["wins"] / v["games"], 4),
                                         "base_rate": round(v["base_wins"] / v["base_games"], 4)} for k, v in per.items()}}
    elif panel and current is not None:
        p_w, p_l, p_n = (sum(r[k] for r in panel) for k in ("wins", "losses", "games"))
        p_lo, p_hi = loop.riftlab.wilson(p_w, p_n)
        panel_ok = p_w / p_n >= a.panel_floor
        row["panel"] = {"games": p_n, "wins": p_w, "rate": round(p_w / p_n, 4), "ci95": [round(p_lo, 4), round(p_hi, 4)],
                        "floor": a.panel_floor, "ok": panel_ok,
                        "matchups": {r["name"]: {"games": r["games"], "wins": r["wins"], "rate": round(r["wins"] / r["games"], 4)}
                                     for r in panel}}
        ladder["matches"].append({"iteration": it, "label": "panel", "a": loop.Loop.key(loop.Loop(a).bot(cand)),
                                  "b": loop.Loop.key(loop.Loop(a).bot(current)), "games": p_n, "wins_a": p_w, "wins_b": p_l})
    for r in results:
        if r["kind"] == "yard":
            for k in ("vs_random", "vs_reference"):
                if k in r:
                    row[k] = r[k]
            if "vs_anchors" in r:      # one task per anchor in continuous mode
                row.setdefault("vs_anchors", {}).update(r["vs_anchors"])
    if continuous(a) and row.get("vs_anchors"):
        g = sum(v["games"] for v in row["vs_anchors"].values())
        row["anchor_rate"] = round(sum(v["wins"] for v in row["vs_anchors"].values()) / g, 4) if g else None
        row["anchor_games"] = g
    if plan["anchors"] and not ladder.get("anchor_nets"):
        ladder["anchor_nets"] = plan["anchors"]
    human_ok = True
    hl = row.get("human_legend")
    if a.specialist and hl and (hl.get("rows") or 0) >= 200:
        base = next((r.get("human_legend", {}).get("top1") for r in reversed(ladder.get("iterations", []))
                     if r.get("accepted") and r.get("human_legend")), None)
        if base is not None and hl.get("top1") is not None:
            # A drop counts only beyond the noise of two agreement rates on this many held-out rows
            # (95%, unpaired: conservative), and never less than --spec-human-margin.
            noise = 1.96 * math.sqrt(2 * base * (1 - base) / hl["rows"])
            tol = max(a.spec_human_margin, noise)
            human_ok = hl["top1"] >= base - tol
            hl.update(base_top1=base, tolerance=round(tol, 4), ok=human_ok)
    accepted = mirror_ok and panel_ok and human_ok
    row["net"] = cand
    row["accepted"] = accepted
    if continuous(a):
        # The adopted net becomes the head (it plays the next self-play and is what the next candidate starts
        # from); it replaces the committed current_net only every --commit-every iterations, so the repository
        # does not grow by a net per iteration.
        row["continuous"] = True
        row["base"] = current
        if accepted:
            current = cand
            ladder["head_net"] = cand
            committed = plan.get("committed")
            if it % max(1, a.commit_every) == 0 or not committed or not (loop.ROOT / committed).exists():
                ladder["current_net"] = cand
                row["committed"] = True
    elif accepted:
        current = cand
        ladder["current_net"] = cand
    if reference is None:
        ladder["reference_net"] = cand
    elo = loop.elo_fit(ladder.get("matches", []))
    ladder["elo"] = elo
    row["elo"] = elo.get(f"net:{cand}")
    row["elo_current"] = elo.get(f"net:{current}") if current else 0.0
    row["minutes"] = round((time.time() - plan["t0"]) / 60, 1)
    row["parallel"] = True
    ladder["iterations"].append(row)
    return row


def cmd_decide(args, _):
    plan = json.loads(Path(args.plan).read_text())
    settings(plan)
    ladder = loop.load_ladder()
    if len(ladder["iterations"]) != plan["it"]:
        raise SystemExit(f"the ladder moved on: {len(ladder['iterations'])} iterations, the plan is for {plan['it']}")
    results = [json.loads(Path(p).read_text()) for p in args.results]
    row = decide(plan, json.loads(Path(args.cand).read_text()), results, ladder)
    loop.LADDER.parent.mkdir(parents=True, exist_ok=True)
    loop.LADDER.write_text(json.dumps(ladder, indent=1))
    if args.summary:
        # for the workflow: which net is the head (to cache) and which nets belong in the repository
        Path(args.summary).write_text(json.dumps({"continuous": bool(plan.get("continuous")), "accepted": bool(row.get("accepted")),
                                                  "head": ladder.get("head_net"), "current": ladder.get("current_net"),
                                                  "reference": ladder.get("reference_net"), "committed": bool(row.get("committed"))}))
    print(json.dumps(row))


def main(argv):
    rest = []
    if "--" in argv:
        i = argv.index("--")
        argv, rest = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan"); p.add_argument("--out", required=True)
    p = sub.add_parser("selfplay"); p.add_argument("--plan", required=True)
    p.add_argument("--shard", type=int, required=True); p.add_argument("--shards", type=int, required=True)
    p = sub.add_parser("train"); p.add_argument("--plan", required=True); p.add_argument("--out", required=True)
    p.add_argument("--variant", type=int, default=None)
    p = sub.add_parser("screen"); p.add_argument("--plan", required=True); p.add_argument("--cand", required=True)
    p.add_argument("--variant", type=int, required=True); p.add_argument("--block", type=int, required=True)
    p.add_argument("--out", required=True)
    p = sub.add_parser("pick"); p.add_argument("--plan", required=True); p.add_argument("--out", required=True)
    p.add_argument("--cands", nargs="+", required=True); p.add_argument("--screens", nargs="*", default=[])
    p = sub.add_parser("gate"); p.add_argument("--plan", required=True); p.add_argument("--cand", required=True)
    p.add_argument("--task", required=True); p.add_argument("--out", required=True)
    p = sub.add_parser("decide"); p.add_argument("--plan", required=True); p.add_argument("--cand", required=True)
    p.add_argument("--summary", default="")
    p.add_argument("results", nargs="+")
    args = ap.parse_args(argv)
    {"plan": cmd_plan, "selfplay": cmd_selfplay, "train": cmd_train, "screen": cmd_screen, "pick": cmd_pick,
     "gate": cmd_gate, "decide": cmd_decide}[args.cmd](args, rest)


if __name__ == "__main__":
    main(sys.argv[1:])
