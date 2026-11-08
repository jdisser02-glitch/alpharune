#!/usr/bin/env python3
"""replay_imitate — teach a net to replay the human games move for move (Jeremy, 8 Nov).

Each human game (RiftLite replays and Play games: lab/learn/human.rbv3, both players' decisions) is replayed
from the start with its recorded moves. At every decision the net is asked which move it would make; at the
first move that differs from the human's, the game counts as broken there and the net is corrected on the
decisions it got wrong; then every game is replayed again from the start. Rounds go on until the net
reproduces at least --target of the human moves (and as many whole games as it can), with no reward, no
self-play and no hand-written rule: only the human games.

A replay that follows the recorded moves passes through exactly the recorded positions, so "replay until the
first deviation, correct, start again" trains on the same positions as supervised learning on the human
moves; what this loop adds is the stopping rule (the games reproduced) and the per-game report.

Two numbers per round:
  - on the games it learns: share of human moves reproduced, games reproduced whole, mean first deviation;
  - on --holdout games it never trains on: share of human moves it would make there (does it learn principles,
    or only these games).

    python3 lab/learn/replay_imitate.py --init lab/learn/nets/net_164_v3.rbpn --out lab/learn/nets/replay_base.rbpn
        [--target 0.9] [--rounds 30] [--epochs 2] [--holdout 0.2] [--hidden 128]
"""
from __future__ import annotations

import argparse
import json
import os
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

for _v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_v, "2")
import numpy as np  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "lab" / "tools"))
from rbpn import read_rbpn  # noqa: E402
from train_value import read_records  # noqa: E402
from split_human_by_legend import records  # noqa: E402


def split_by_game(path: Path):
    """(header, {game id: [record bytes, in order]})."""
    b = path.read_bytes()
    out = {}
    for s, e, _ in records(b):
        out.setdefault(struct.unpack_from("<I", b, s + 5)[0], []).append(b[s:e])
    return b[:12], out


def moves(net, data_path: Path):
    """Per record, in file order: (game id, net's move == human's move), human decisions with a policy target only."""
    d = read_records(str(data_path))
    W1z = (net["W1"] * net["inv_std"][None, :]).astype(np.float32)
    b1z = (net["b1"] - net["W1"] @ (net["mean"] * net["inv_std"])).astype(np.float32)
    out = []
    for g, idx, val, slots, tgt in zip(d["game"], d["idx"], d["val"], d["pol_idx"], d["pol_val"]):
        if not len(slots) or tgt.max() <= 0:
            continue
        h = np.maximum(b1z + W1z[:, idx] @ val, 0)
        logits = net["Wp"][slots] @ h + net["bp"][slots]
        out.append((int(g), bool(np.argmax(logits) == np.argmax(tgt))))
    return out


def report(rows):
    """Share of moves reproduced, games reproduced whole, mean index of the first deviation (share of the game)."""
    by = {}
    for g, ok in rows:
        by.setdefault(g, []).append(ok)
    whole = sum(all(v) for v in by.values())
    first = [next((i for i, ok in enumerate(v) if not ok), len(v)) / max(1, len(v)) for v in by.values()]
    return {"moves": round(sum(ok for _, ok in rows) / max(1, len(rows)), 4), "decisions": len(rows),
            "games": len(by), "games_whole": whole, "first_deviation": round(float(np.mean(first)) if first else 0, 3)}


def train(out: Path, human: Path, init: Path, a) -> None:
    cmd = [sys.executable, str(HERE / "train_policy.py"), str(out), str(a.anchor), "--human", str(human),
           "--human-weights", str(a.human_weight), "--human-val-frac", "0", "--human-repeat", str(a.repeat),
           "--epochs", str(a.epochs), "--lr", str(a.lr), "--hidden", str(a.hidden), "--init-value", str(init),
           "--keep", "last", "--entropy", "0", "--seed", str(a.seed)]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"train_policy failed:\n{r.stderr[-1500:]}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--human", default=str(ROOT / "lab" / "learn" / "human.rbv3"))
    ap.add_argument("--init", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--anchor", default=str(ROOT / "data" / "learn" / "iter_039.rbvd"),
                    help="self-play data that keeps the value head (it carries no policy target here)")
    ap.add_argument("--target", type=float, default=0.9, help="share of human moves to reproduce")
    ap.add_argument("--rounds", type=int, default=30)
    ap.add_argument("--epochs", type=int, default=2, help="training epochs per round")
    ap.add_argument("--holdout", type=float, default=0.2, help="share of games never trained on (0: none)")
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--human-weight", type=float, default=20.0)
    ap.add_argument("--repeat", type=int, default=8)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args(argv)

    head, games = split_by_game(Path(a.human))
    ids = sorted(games)
    random.Random(a.seed).shuffle(ids)
    n_hold = int(round(len(ids) * a.holdout))
    hold, learn = set(ids[:n_hold]), set(ids[n_hold:])
    log = {"init": a.init, "settings": vars(a), "games_learned": len(learn), "games_held_out": len(hold), "rounds": []}
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        tr, te = tmp / "learn.rbv3", tmp / "holdout.rbv3"
        tr.write_bytes(head + b"".join(b"".join(games[g]) for g in sorted(learn)))
        te.write_bytes(head + b"".join(b"".join(games[g]) for g in sorted(hold)))
        cur = Path(a.init)
        net = read_rbpn(str(cur))
        r0 = {"round": 0, "learned": report(moves(net, tr)), "held_out": report(moves(net, te)) if hold else None}
        log["rounds"].append(r0)
        print(f"round 0 ({cur.name}): learned games {r0['learned']}  held-out {r0['held_out']}", flush=True)
        for k in range(1, a.rounds + 1):
            nxt = tmp / f"round_{k}.rbpn"
            train(nxt, tr, cur, a)
            net = read_rbpn(str(nxt))
            row = {"round": k, "learned": report(moves(net, tr)), "held_out": report(moves(net, te)) if hold else None}
            log["rounds"].append(row)
            print(f"round {k}: learned games {row['learned']}  held-out {row['held_out']}", flush=True)
            cur = nxt
            if row["learned"]["moves"] >= a.target:
                break
        out = Path(a.out)
        for suf in (".rbpn", ".rbvn", ".json"):
            src = cur.with_suffix(suf)
            if src.exists():
                out.with_suffix(suf).write_bytes(src.read_bytes())
    out.with_suffix(".replay.json").write_text(json.dumps(log, indent=1))
    last = log["rounds"][-1]
    print(f"wrote {out}: reproduces {last['learned']['moves']:.1%} of the human moves of {len(learn)} games "
          f"({last['learned']['games_whole']} whole); on {len(hold)} unseen games {last['held_out']['moves'] if hold else float('nan'):.1%}")


if __name__ == "__main__":
    sys.exit(main())
