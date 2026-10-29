#!/usr/bin/env python3
"""Self-play learning loop for the Riftbound bot.

Each iteration:
  1. self-play: the current bot plays itself on random pairs of decks from the
     pool, recording sampled positions and the result (riftbound --value-data);
  2. training: a new value network is trained on the last iterations' records
     (lab/learn/train_value.py);
  3. gating: the new bot plays the current one (seats swapped); it becomes the
     current bot if it scores at least --accept of the games.

Everything is logged to lab/learn/ladder.json (read by the dashboard); networks
go to lab/learn/nets/. Iteration 0's data comes from random play.

usage: loop.py [--iterations 3] [--games 200] [--sims 30] [--eval-games 40] [--threads N]
"""
import argparse
import datetime as dt
import json
import os
import random
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LEARN = ROOT / "lab" / "learn"
DATA = ROOT / "data" / "learn"
NETS = LEARN / "nets"
LADDER = LEARN / "ladder.json"
ENGINE = Path(os.environ.get("RIFTBOUND_BIN", ROOT / "build" / "riftbound"))
sys.path.insert(0, str(ROOT / "lab"))
import riftlab  # noqa: E402


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
        cmd += ["--value-data", str(value_data), "--value-samples", "12"]
    out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(out.stderr[-1500:])
    w1 = int(re.search(r"P1 wins:\s+(\d+)", out.stdout).group(1))
    w2 = int(re.search(r"P2 wins:\s+(\d+)", out.stdout).group(1))
    return w1, w2


def bot(net, sims):
    return f"ismcts:sims={sims},eval=net:{net}" if net else "random"


def load_ladder():
    if LADDER.exists():
        return json.loads(LADDER.read_text())
    return {"description": "Self-play learning ladder: each row is one iteration.", "iterations": []}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--iterations", type=int, default=3)
    ap.add_argument("--games", type=int, default=200, help="self-play games per iteration")
    ap.add_argument("--sims", type=int, default=30)
    ap.add_argument("--eval-games", type=int, default=40)
    ap.add_argument("--accept", type=float, default=0.55)
    ap.add_argument("--window", type=int, default=3, help="iterations of data to train on")
    ap.add_argument("--threads", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args(argv)
    DATA.mkdir(parents=True, exist_ok=True)
    NETS.mkdir(parents=True, exist_ok=True)
    ladder = load_ladder()
    pool = deck_pool()
    rng = random.Random(a.seed + len(ladder["iterations"]))
    print(f"deck pool: {len(pool)} decks", flush=True)

    current = ladder.get("current_net")
    if current and not (ROOT / current).exists():
        current = None
    for _ in range(a.iterations):
        it = len(ladder["iterations"])
        t0 = time.time()
        row = {"iteration": it, "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
               "player": bot(current, a.sims), "self_play_games": a.games, "sims": a.sims}
        data_file = DATA / f"iter_{it:03d}.rbvd"
        if data_file.exists():
            data_file.unlink()
        # 1. self-play over random deck pairs, a few games per pair
        per_pair = 4
        games = a.games * 15 if current is None else a.games   # random play is cheap
        for k in range(max(1, games // per_pair)):
            d1, d2 = rng.choice(pool), rng.choice(pool)
            run_batch(d1, d2, bot(current, a.sims), bot(current, a.sims), per_pair, a.threads,
                      a.seed * 1000003 + it * 10007 + k, data_file)
        # 2. train on the window of recent data
        files = [str(DATA / f"iter_{j:03d}.rbvd") for j in range(max(0, it - a.window + 1), it + 1)
                 if (DATA / f"iter_{j:03d}.rbvd").exists()]
        new_net = NETS / f"net_{it:03d}.rbvn"
        tr = subprocess.run([sys.executable, str(LEARN / "train_value.py"), str(new_net), *files,
                             "--epochs", "8"], cwd=ROOT, capture_output=True, text=True)
        print(tr.stdout[-800:], flush=True)
        if tr.returncode != 0:
            raise RuntimeError(tr.stderr[-1500:])
        report = json.loads(new_net.with_suffix(".json").read_text())
        row["records"] = report["records"]
        row["val_loss"] = report["best_val_loss"]
        row["val_acc"] = report["best_val_acc"]
        row["baseline_val_loss"] = report["baseline_val_loss"]
        # 3. gate: new vs current, mirror match on a Riven list, seats swapped
        cand = str(new_net.relative_to(ROOT))
        deck = "decks/riven_sacramento.txt"
        half = a.eval_games // 2
        w1, _ = run_batch(deck, deck, bot(cand, a.sims), bot(current, a.sims), half, a.threads, 77 + it)
        _, w2 = run_batch(deck, deck, bot(current, a.sims), bot(cand, a.sims), half, a.threads, 99 + it)
        n = 2 * max(2, half)
        score = (w1 + w2) / n
        lo, hi = riftlab.wilson(w1 + w2, n)
        row["vs_current"] = {"opponent": bot(current, a.sims), "games": n, "wins": w1 + w2,
                             "rate": round(score, 4), "ci95": [round(lo, 4), round(hi, 4)]}
        # also vs random, the fixed yardstick
        r1, _ = run_batch(deck, deck, bot(cand, a.sims), "random", half, a.threads, 123 + it)
        _, r2 = run_batch(deck, deck, "random", bot(cand, a.sims), half, a.threads, 321 + it)
        row["vs_random"] = {"games": n, "wins": r1 + r2, "rate": round((r1 + r2) / n, 4)}
        accepted = current is None or score >= a.accept
        row["net"] = cand
        row["accepted"] = accepted
        row["minutes"] = round((time.time() - t0) / 60, 1)
        if accepted:
            current = cand
            ladder["current_net"] = cand
        ladder["iterations"].append(row)
        LADDER.write_text(json.dumps(ladder, indent=1))
        print(json.dumps(row), flush=True)


if __name__ == "__main__":
    main(sys.argv[1:])
