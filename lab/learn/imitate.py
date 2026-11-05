#!/usr/bin/env python3
"""Imitation loop for one legend: teach a net to make the human's move in that legend's RiftLite games.

The legend's human decisions (lab/tools/split_human_by_legend.py: human_<legend>.rbv3) are replayed game by
game. Every round trains on all games but one and asks the net, on the held-out game, which move the human
made; a round per game (leave-one-game-out) says how well the imitation generalises rather than memorises.
The epoch count that generalises best is then used to train on every game, and that net is written out with
its agreement on the games it learned (memorised) and on held-out games (generalised).

    python3 lab/learn/imitate.py --legend riven --init lab/learn/nets/net_164_v3.rbpn --out lab/learn/nets/imit_riven.rbpn
        [--hidden 128] [--epochs 1 2 4 8] [--lr 3e-4] [--human-weight 10] [--anchor data/learn/iter_039.rbvd]

Runs on one core (OMP/BLAS threads set to 1). Training goes through train_policy.py (same layout, widening,
human weighting), with a small self-play file as the value head's anchor so imitation does not erase the
net's sense of who is winning.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

for _v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_v, "1")
import numpy as np  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "lab" / "tools"))
from rbpn import read_rbpn  # noqa: E402
from train_value import read_records  # noqa: E402
from split_human_by_legend import records  # noqa: E402


def split_by_game(path: Path):
    """{game id: bytes of its records}, and the file header."""
    b = path.read_bytes()
    out = {}
    for s, e, _ in records(b):
        gid = struct.unpack_from("<I", b, s + 5)[0]
        out.setdefault(gid, []).append(b[s:e])
    return b[:12], {g: b"".join(v) for g, v in out.items()}


def agreement(net_path: Path, data_path: Path) -> tuple[float, int]:
    """Share of the file's human decisions where the net's policy head ranks the human's move first."""
    n = read_rbpn(str(net_path))
    d = read_records(str(data_path))
    hits = rows = 0
    for idx, val, slots, tgt in zip(d["idx"], d["val"], d["pol_idx"], d["pol_val"]):
        if not len(slots) or tgt.max() <= 0:
            continue
        x = np.zeros(n["W1"].shape[1], np.float32)
        x[idx] = val
        h = np.maximum(n["b1"] + n["W1"] @ ((x - n["mean"]) * n["inv_std"]), 0)
        logits = n["Wp"][slots] @ h + n["bp"][slots]
        hits += int(np.argmax(logits) == np.argmax(tgt))
        rows += 1
    return (hits / rows if rows else float("nan")), rows


def train(out: Path, human: Path, a, epochs: int, init: str) -> None:
    cmd = [sys.executable, str(HERE / "train_policy.py"), str(out), str(a.anchor), "--human", str(human),
           "--human-weights", str(a.human_weight), "--human-val-frac", "0", "--human-repeat", str(a.repeat),
           "--epochs", str(epochs), "--lr", str(a.lr), "--hidden", str(a.hidden), "--init-value", init,
           "--keep", "last" if a.memorise else "best"]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"train_policy failed:\n{r.stderr[-1500:]}")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--legend", required=True)
    ap.add_argument("--human-dir", default=str(ROOT / "data" / "learn" / "human"))
    ap.add_argument("--init", required=True, help="RBPN the imitation starts from (the legend's specialist, or the general net)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--anchor", default=str(ROOT / "data" / "learn" / "iter_039.rbvd"), help="self-play data that keeps the value head")
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--epochs", type=int, nargs="+", default=[1, 2, 4, 8])
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--human-weight", type=float, default=10.0)
    ap.add_argument("--repeat", type=int, default=8, help="times each human decision appears per epoch")
    ap.add_argument("--memorise", action="store_true",
                    help="keep the last epoch instead of the best on self-play: the net learns the human moves through, "
                         "at the risk of playing worse elsewhere (the held-out rounds measure what is left of it)")
    a = ap.parse_args(argv)

    src = Path(a.human_dir) / f"human_{a.legend}.rbv3"
    if not src.exists():
        subprocess.run([sys.executable, str(ROOT / "lab/tools/split_human_by_legend.py"), "--out-dir", a.human_dir, "--min", "1"],
                       check=True, capture_output=True)
    head, games = split_by_game(src)
    if len(games) < 2:
        raise SystemExit(f"{src}: {len(games)} game(s), need 2 for a held-out measure")
    report = {"legend": a.legend, "init": a.init, "games": len(games), "decisions": None, "settings": vars(a), "folds": {}}
    base_all = agreement(Path(a.init), src)
    report["decisions"] = base_all[1]
    report["init_agreement"] = round(base_all[0], 4)
    print(f"{a.legend}: {len(games)} games, {base_all[1]} decisions; {Path(a.init).name} agrees {base_all[0]:.3f}", flush=True)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        held = {e: [0, 0] for e in a.epochs}
        base_held = [0, 0]
        for k, (gid, data) in enumerate(sorted(games.items())):
            tr, te = tmp / "train.rbv3", tmp / "test.rbv3"
            tr.write_bytes(head + b"".join(v for g, v in games.items() if g != gid))
            te.write_bytes(head + data)
            b_acc, b_n = agreement(Path(a.init), te)
            base_held[0] += b_acc * b_n; base_held[1] += b_n
            for e in a.epochs:
                net = tmp / f"fold{k}_e{e}.rbpn"
                train(net, tr, a, e, a.init)
                acc, n = agreement(net, te)
                held[e][0] += acc * n; held[e][1] += n
                print(f"  held-out game {k + 1}/{len(games)} ({n} decisions): {e} epoch(s) {acc:.3f}  (start {b_acc:.3f})", flush=True)
        gen = {e: h[0] / h[1] for e, h in held.items() if h[1]}
        best = max(gen, key=gen.get)
        report["held_out"] = {"init": round(base_held[0] / base_held[1], 4), **{f"{e}_epochs": round(v, 4) for e, v in gen.items()}}
        report["best_epochs"] = best
        print(f"held-out agreement: start {base_held[0] / base_held[1]:.3f}; " + ", ".join(f"{e} ep {v:.3f}" for e, v in gen.items()), flush=True)
        train(Path(a.out), src, a, best, a.init)
    mem = agreement(Path(a.out), src)
    report["memorised"] = round(mem[0], 4)
    Path(a.out).with_suffix(".imitation.json").write_text(json.dumps(report, indent=1))
    print(f"wrote {a.out}: agrees {mem[0]:.3f} with the {a.legend} games it learned (held-out estimate {gen[best]:.3f})", flush=True)


if __name__ == "__main__":
    main(sys.argv[1:])
