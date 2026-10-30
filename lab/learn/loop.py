#!/usr/bin/env python3
"""Self-play learning loop for the Riftbound bot.

Each iteration:
  1. self-play: the current bot plays itself on random pairs of decks from the
     pool, recording every decision of both players with the result and the
     final score margin (riftbound --value-data, RBV2 records);
  2. training: a new value network is trained on a rolling replay buffer of the
     last --buffer iterations, recent data weighted more (train_value.py) — or,
     with --policy, a two-head policy + value network (train_policy.py) from the
     searches' visit distributions and the human imitation set, which the bot
     then uses as its PUCT prior (pmcts:eval=policy:<file>);
  3. gating: the new bot plays the current one on the Riven mirror, seats
     swapped, in blocks, under a sequential probability ratio test (H0 50% vs
     H1 60%, alpha = beta = 0.05): it stops as soon as the candidate is clearly
     better or clearly worse, otherwise after --gate-max games, when the plain
     win rate decides (>= --accept);
  4. yardsticks: the candidate also plays random and the fixed reference net
     (net_000, kept forever) so iterations stay comparable, and a Bradley-Terry
     Elo over every match ever recorded (random anchored at 0) is written per
     net into ladder.json.

Everything is logged to lab/learn/ladder.json (read by the dashboard); networks
go to lab/learn/nets/. Iteration 0's data comes from random play.

usage: loop.py [--iterations 3] [--games 200] [--sims 30] [--threads N] [--agent pmcts|ismcts]
               [--policy [--human data/learn/human.rbv3] [--human-weight 0.3]]
"""
import argparse
import datetime as dt
import json
import math
import os
import random
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LEARN = ROOT / "lab" / "learn"
# RIFTBOUND_LEARN_DIR redirects the ladder, nets and data (a dry run, a fork).
_OUT = Path(os.environ.get("RIFTBOUND_LEARN_DIR", LEARN))
DATA = (ROOT / "data" / "learn") if "RIFTBOUND_LEARN_DIR" not in os.environ else _OUT / "data"
NETS = _OUT / "nets"
LADDER = _OUT / "ladder.json"
ENGINE = Path(os.environ.get("RIFTBOUND_BIN", ROOT / "build" / "riftbound"))
sys.path.insert(0, str(ROOT / "lab"))
import riftlab  # noqa: E402

GATE_DECK = "decks/riven_sacramento.txt"


def deck_pool():
    reg = riftlab.Registry.load()
    pool = []
    for p in sorted((ROOT / "decks").glob("riven_*.txt")) + sorted((ROOT / "lab" / "decks" / "meta").rglob("*.txt")):
        if not riftlab.validate(riftlab.parse_deck(p), reg):
            pool.append(str(p.relative_to(ROOT)))
    return pool


def run_batch(deck1, deck2, a1, a2, games, threads, seed, value_data=None):
    cmd = [str(ENGINE), "--deck1", deck1, "--deck2", deck2, "--agent1", a1, "--agent2", a2,
           "--games", str(max(2, games)), "--threads", str(threads), "--seed", str(seed),
           "--render-html", "off", "--web", "off"]
    if value_data:
        cmd += ["--value-data", str(value_data), "--value-samples", "0"]
    out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(out.stderr[-1500:])
    w1 = int(re.search(r"P1 wins:\s+(\d+)", out.stdout).group(1))
    w2 = int(re.search(r"P2 wins:\s+(\d+)", out.stdout).group(1))
    return w1, w2


class Loop:
    def __init__(self, a):
        self.a = a

    def bot(self, net, sims=None):
        """Agent spec for a network (None = random play). A .rbpn (two-head)
        network is used by pmcts as value head + policy prior; a .rbvn as the
        value head alone."""
        if not net:
            return "random"
        sims = sims or self.a.sims
        policy = str(net).endswith(".rbpn")
        if self.a.agent == "ismcts" and not policy:
            return f"ismcts:sims={sims},eval=net:{net}"
        kind = "policy" if policy else "net"
        return f"pmcts:sims={sims},eval={kind}:{net},worlds={self.a.worlds}"

    @staticmethod
    def key(spec):
        """Elo key of an agent spec: the network (or 'random')."""
        m = re.search(r"eval=(?:net|policy):([^,]+)", spec)
        return f"net:{m.group(1)}" if m else spec

    def match(self, ladder, it, a1, a2, games, seed, label):
        """`games` games split over both seat orders. Records the pairing for Elo."""
        half = max(1, games // 2)
        w1, l1 = run_batch(GATE_DECK, GATE_DECK, a1, a2, half, self.a.threads, seed)
        l2, w2 = run_batch(GATE_DECK, GATE_DECK, a2, a1, half, self.a.threads, seed + 1)
        n = 2 * max(2, half)
        wins = w1 + w2
        losses = l1 + l2
        ladder.setdefault("matches", []).append({"iteration": it, "label": label, "a": self.key(a1), "b": self.key(a2),
                                                 "games": n, "wins_a": wins, "wins_b": losses})
        return wins, losses, n


def sprt_llr(wins, losses, p0=0.5, p1=0.6):
    """Log-likelihood ratio of H1 (p1) against H0 (p0); draws are ignored."""
    return wins * math.log(p1 / p0) + losses * math.log((1 - p1) / (1 - p0))


def elo_fit(matches, anchor="random", iters=200):
    """Bradley-Terry strengths by minorisation-maximisation (Hunter 2004) over
    all pairings, with half a virtual win each way per pairing so a perfect
    record stays finite. Elo = 400 log10(gamma / gamma_anchor)."""
    players = sorted({m["a"] for m in matches} | {m["b"] for m in matches} | {anchor})
    ix = {p: i for i, p in enumerate(players)}
    k = len(players)
    w = [[0.0] * k for _ in range(k)]     # w[i][j] = wins of i over j
    for m in matches:
        i, j = ix[m["a"]], ix[m["b"]]
        draws = m["games"] - m["wins_a"] - m["wins_b"]
        w[i][j] += m["wins_a"] + 0.5 * draws + 0.5
        w[j][i] += m["wins_b"] + 0.5 * draws + 0.5
    gamma = [1.0] * k
    for _ in range(iters):
        new = []
        for i in range(k):
            wi = sum(w[i])
            denom = sum((w[i][j] + w[j][i]) / (gamma[i] + gamma[j]) for j in range(k) if j != i and (w[i][j] + w[j][i]) > 0)
            new.append(wi / denom if denom > 0 else gamma[i])
        s = new[ix[anchor]] or 1.0
        gamma = [g / s for g in new]
    return {p: round(400 * math.log10(max(gamma[ix[p]], 1e-9)), 1) for p in players}


def load_ladder():
    if LADDER.exists():
        return json.loads(LADDER.read_text())
    return {"description": "Self-play learning ladder: each row is one iteration.", "iterations": []}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--iterations", type=int, default=3)
    ap.add_argument("--games", type=int, default=200, help="self-play games per iteration")
    ap.add_argument("--sims", type=int, default=30)
    ap.add_argument("--agent", choices=["pmcts", "ismcts"], default="pmcts")
    ap.add_argument("--worlds", type=int, default=8, help="pmcts determinizations per decision")
    ap.add_argument("--gate-min", type=int, default=200, help="games before the plain win rate may decide")
    ap.add_argument("--gate-max", type=int, default=400)
    ap.add_argument("--gate-block", type=int, default=40, help="games per gate block (half per seat order)")
    ap.add_argument("--accept", type=float, default=0.55)
    ap.add_argument("--yardstick-games", type=int, default=100, help="games vs random and vs the reference net")
    ap.add_argument("--buffer", type=int, default=6, help="iterations of data in the replay buffer")
    ap.add_argument("--recency", type=float, default=0.7, help="weight multiplier per iteration of age")
    ap.add_argument("--epochs", type=int, default=8)
    ap.add_argument("--policy", action="store_true",
                    help="train a two-head policy + value net (train_policy.py) and search with it as the prior")
    ap.add_argument("--human", default=str(ROOT / "data" / "learn" / "human.rbv3"),
                    help="human imitation set for --policy (lab/tools/build_human_dataset.py); '' for none")
    ap.add_argument("--human-weight", type=float, default=0.3)
    ap.add_argument("--entropy", type=float, default=0.01, help="--policy: entropy bonus")
    ap.add_argument("--prev-weight", type=float, default=0.1, help="--policy: pull toward the previous net's policy")
    ap.add_argument("--threads", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args(argv)
    DATA.mkdir(parents=True, exist_ok=True)
    NETS.mkdir(parents=True, exist_ok=True)
    ladder = load_ladder()
    pool = deck_pool()
    rng = random.Random(a.seed + len(ladder["iterations"]))
    L = Loop(a)
    print(f"deck pool: {len(pool)} decks; agent {a.agent}", flush=True)

    current = ladder.get("current_net")
    if current and not (ROOT / current).exists():
        current = None
    reference = ladder.get("reference_net")
    if reference and not (ROOT / reference).exists():
        reference = None
    _OUT.mkdir(parents=True, exist_ok=True)
    for _ in range(a.iterations):
        it = len(ladder["iterations"])
        t0 = time.time()
        row = {"iteration": it, "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
               "player": L.bot(current), "self_play_games": a.games, "sims": a.sims, "agent": a.agent,
               "trainer": "train_policy" if a.policy else "train_value"}
        data_file = DATA / f"iter_{it:03d}.rbvd"
        if data_file.exists():
            data_file.unlink()
        # 1. self-play over random deck pairs, a few games per pair
        per_pair = 4
        games = a.games * 3 if current is None else a.games   # random play is cheap
        for k in range(max(1, games // per_pair)):
            d1, d2 = rng.choice(pool), rng.choice(pool)
            run_batch(d1, d2, L.bot(current), L.bot(current), per_pair, a.threads,
                      a.seed * 1000003 + it * 10007 + k, data_file)
        row["self_play_minutes"] = round((time.time() - t0) / 60, 1)
        # 2. train on the replay buffer (oldest first; train_value.py weights by recency)
        files = [str(DATA / f"iter_{j:03d}.rbvd") for j in range(max(0, it - a.buffer + 1), it + 1)
                 if (DATA / f"iter_{j:03d}.rbvd").exists()]
        if a.policy:
            new_net = NETS / f"net_{it:03d}.rbpn"
            cmd = [sys.executable, str(LEARN / "train_policy.py"), str(new_net), *files,
                   "--epochs", str(a.epochs), "--recency", str(a.recency), "--human-weight", str(a.human_weight),
                   "--entropy", str(a.entropy)]
            if a.human and Path(a.human).exists():
                cmd += ["--human", a.human]
            if current and str(current).endswith(".rbpn") and a.prev_weight > 0:
                cmd += ["--prev", str(ROOT / current), "--prev-weight", str(a.prev_weight)]
        else:
            new_net = NETS / f"net_{it:03d}.rbvn"
            cmd = [sys.executable, str(LEARN / "train_value.py"), str(new_net), *files,
                   "--epochs", str(a.epochs), "--recency", str(a.recency)]
        tr = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        print(tr.stdout[-1200:], flush=True)
        if tr.returncode != 0:
            raise RuntimeError(tr.stderr[-1500:])
        report = json.loads(new_net.with_suffix(".json").read_text())
        row["records"] = report["records"]
        row["games_in_buffer"] = report.get("games")
        row["val_loss"] = report["best_val_loss"]
        row["val_acc"] = report["best_val_acc"]
        row["val_margin_mse"] = report.get("best_val_margin_mse")
        row["baseline_val_loss"] = report["baseline_val_loss"]
        if a.policy:
            row["policy"] = {k: report.get(k) for k in ("search_target_rows", "policy_val_rows", "policy_top1", "policy_top3",
                                                         "policy_ce", "policy_entropy", "target_entropy",
                                                         "human_records", "human_val_rows", "human_top1", "human_top3",
                                                         "human_clean_rows", "human_clean_top1", "human_clean_top3")}
        cand = str(new_net.relative_to(ROOT)) if new_net.is_relative_to(ROOT) else str(new_net)
        # 3. gate: candidate vs current, Riven mirror, seats swapped, SPRT in blocks
        wins = losses = n = 0
        llr, decision = 0.0, None
        upper, lower = math.log((1 - 0.05) / 0.05), math.log(0.05 / (1 - 0.05))
        blocks = 0
        while n < a.gate_max:
            w, l, g = L.match(ladder, it, L.bot(cand), L.bot(current), a.gate_block,
                              77 + it * 1000 + 2 * blocks, "gate")
            wins += w; losses += l; n += g; blocks += 1
            llr = sprt_llr(wins, losses)
            if blocks >= 2 and llr >= upper:
                decision = "better"
                break
            if blocks >= 2 and llr <= lower:
                decision = "worse"
                break
            if current is None and n >= a.gate_block:
                break           # the first net replaces random play without a contest
        score = wins / n
        lo, hi = riftlab.wilson(wins, n)
        if decision is None:
            decision = "rate" if n >= a.gate_min else "short"
        row["vs_current"] = {"opponent": L.bot(current), "games": n, "wins": wins, "rate": round(score, 4),
                             "ci95": [round(lo, 4), round(hi, 4)], "llr": round(llr, 3), "sprt": decision}
        # 4. yardsticks: random, and the fixed reference net
        r_w, _, r_n = L.match(ladder, it, L.bot(cand), "random", a.yardstick_games, 123 + it * 1000, "vs_random")
        row["vs_random"] = {"games": r_n, "wins": r_w, "rate": round(r_w / r_n, 4)}
        if reference and reference != cand:
            f_w, _, f_n = L.match(ladder, it, L.bot(cand), L.bot(reference), a.yardstick_games,
                                  321 + it * 1000, "vs_reference")
            row["vs_reference"] = {"opponent": L.bot(reference), "games": f_n, "wins": f_w, "rate": round(f_w / f_n, 4)}
        accepted = current is None or decision == "better" or (decision == "rate" and score >= a.accept)
        row["net"] = cand
        row["accepted"] = accepted
        if accepted:
            current = cand
            ladder["current_net"] = cand
        if reference is None:
            reference = cand
            ladder["reference_net"] = cand
        elo = elo_fit(ladder.get("matches", []))
        ladder["elo"] = elo
        row["elo"] = elo.get(f"net:{cand}")
        row["elo_current"] = elo.get(f"net:{current}") if current else 0.0
        row["minutes"] = round((time.time() - t0) / 60, 1)
        ladder["iterations"].append(row)
        LADDER.write_text(json.dumps(ladder, indent=1))
        print(json.dumps(row), flush=True)


if __name__ == "__main__":
    main(sys.argv[1:])
