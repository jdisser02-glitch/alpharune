#!/usr/bin/env python3
"""Train the position-value network used by `eval=net:<file>` from self-play
records written by `riftbound --value-data FILE`.

usage: train_value.py OUT.rbvn DATA.rbvd [DATA2.rbvd ...] [--hidden 64] [--epochs 12]

Model: standardize features, one ReLU hidden layer, sigmoid output = P(the
player whose view the features are from wins). Pure numpy (Adam, L2).
Prints train / validation log-loss and accuracy against the constant baseline
and writes a JSON report next to the network (OUT.json).
"""
import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np


def read_records(path):
    data = Path(path).read_bytes()
    if data[:4] != b"RBVD":
        raise SystemExit(f"{path}: not a value-data file")
    dim = struct.unpack_from("<i", data, 4)[0]
    pos = 8
    labels, turns, indptr, idx, val = [], [], [0], [], []
    n = len(data)
    while pos + 7 <= n:
        label = struct.unpack_from("<b", data, pos)[0]
        turn, nnz = struct.unpack_from("<iH", data, pos + 1)
        pos += 7
        rec = np.frombuffer(data, dtype=np.dtype([("i", "<u2"), ("v", "<f4")]), count=nnz, offset=pos)
        pos += 6 * nnz
        labels.append(label)
        turns.append(turn)
        idx.append(rec["i"].astype(np.int32))
        val.append(rec["v"].astype(np.float32))
        indptr.append(indptr[-1] + nnz)
    return dim, np.array(labels, np.float32), np.array(turns, np.int32), indptr, idx, val


class Sparse:
    def __init__(self, dim, rows_idx, rows_val):
        self.dim = dim
        self.idx = rows_idx
        self.val = rows_val

    def dense(self, rows):
        x = np.zeros((len(rows), self.dim), np.float32)
        for k, r in enumerate(rows):
            x[k, self.idx[r]] = self.val[r]
        return x


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("data", nargs="+")
    ap.add_argument("--hidden", type=int, default=64)
    ap.add_argument("--epochs", type=int, default=12)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--l2", type=float, default=1e-4)
    ap.add_argument("--batch", type=int, default=256)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args(argv)

    dim = None
    y_all, t_all, idx_all, val_all = [], [], [], []
    for p in a.data:
        d, y, t, _, idx, val = read_records(p)
        dim = dim or d
        if d != dim:
            raise SystemExit("feature dimensions differ between files")
        y_all.append(y); t_all.append(t); idx_all += idx; val_all += val
    y = np.concatenate(y_all)
    turns = np.concatenate(t_all)
    X = Sparse(dim, idx_all, val_all)
    n = len(y)
    if n < 200:
        raise SystemExit(f"only {n} records: play more games first")
    # Validation = the last 10% of records (later games), so positions of one
    # game rarely straddle the split.
    n_val = max(100, n // 10)
    tr, va = np.arange(0, n - n_val), np.arange(n - n_val, n)

    # Standardization from the training rows.
    s1 = np.zeros(dim, np.float64); s2 = np.zeros(dim, np.float64)
    for r in tr:
        s1[X.idx[r]] += X.val[r]
        s2[X.idx[r]] += X.val[r].astype(np.float64) ** 2
    mean = s1 / len(tr)
    var = np.maximum(s2 / len(tr) - mean ** 2, 0)
    inv_std = np.where(var > 1e-8, 1.0 / np.sqrt(var + 1e-8), 0.0).astype(np.float32)
    mean = mean.astype(np.float32)

    rng = np.random.default_rng(a.seed)
    H = a.hidden
    W1 = (rng.standard_normal((H, dim)) * np.sqrt(2.0 / max(1, (inv_std > 0).sum()))).astype(np.float32)
    b1 = np.zeros(H, np.float32)
    w2 = (rng.standard_normal(H) * np.sqrt(1.0 / H)).astype(np.float32)
    b2 = np.float32(np.log(max(y[tr].mean(), 1e-3) / max(1 - y[tr].mean(), 1e-3)))
    params = [W1, b1, w2, np.array([b2], np.float32)]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    step = 0

    def forward(rows):
        z = (X.dense(rows) - mean) * inv_std
        a1 = z @ params[0].T + params[1]
        h = np.maximum(a1, 0)
        logit = h @ params[2] + params[3][0]
        return z, a1, h, logit

    def evaluate(rows):
        losses, correct = [], 0
        for k in range(0, len(rows), 1024):
            rr = rows[k:k + 1024]
            _, _, _, logit = forward(rr)
            p = 1 / (1 + np.exp(-np.clip(logit, -30, 30)))
            yy = y[rr]
            losses.append(-(yy * np.log(p + 1e-7) + (1 - yy) * np.log(1 - p + 1e-7)))
            correct += int(((p > 0.5) == (yy > 0.5)).sum())
        return float(np.concatenate(losses).mean()), correct / len(rows)

    base_p = float(y[tr].mean())
    base_loss = float(-(y[va] * np.log(base_p) + (1 - y[va]) * np.log(1 - base_p)).mean())
    print(f"{n} records ({len(tr)} train / {len(va)} validation), dim {dim}, "
          f"{int((inv_std > 0).sum())} live features; baseline val log-loss {base_loss:.4f}")
    history = []
    best = (float("inf"), None, 0)          # (val loss, params, epoch): early stopping
    for ep in range(a.epochs):
        order = rng.permutation(tr)
        for k in range(0, len(order), a.batch):
            rr = order[k:k + a.batch]
            z, a1, h, logit = forward(rr)
            p = 1 / (1 + np.exp(-np.clip(logit, -30, 30)))
            g = (p - y[rr]) / len(rr)                      # dL/dlogit
            gw2 = h.T @ g + a.l2 * params[2]
            gb2 = np.array([g.sum()], np.float32)
            gh = np.outer(g, params[2]) * (a1 > 0)
            gW1 = gh.T @ z + a.l2 * params[0]
            gb1 = gh.sum(0)
            grads = [gW1.astype(np.float32), gb1.astype(np.float32), gw2.astype(np.float32), gb2]
            step += 1
            for i, (pp, gg) in enumerate(zip(params, grads)):
                m[i] = 0.9 * m[i] + 0.1 * gg
                v[i] = 0.999 * v[i] + 0.001 * gg * gg
                mh = m[i] / (1 - 0.9 ** step)
                vh = v[i] / (1 - 0.999 ** step)
                pp -= (a.lr * mh / (np.sqrt(vh) + 1e-8)).astype(np.float32)
        tl, ta = evaluate(tr[: min(len(tr), 5000)])
        vl, vacc = evaluate(va)
        history.append({"epoch": ep + 1, "train_loss": tl, "train_acc": ta, "val_loss": vl, "val_acc": vacc})
        print(f"epoch {ep + 1:2d}: train loss {tl:.4f} acc {ta:.3f} | val loss {vl:.4f} acc {vacc:.3f}", flush=True)
        if vl < best[0]:
            best = (vl, [p.copy() for p in params], ep + 1)
    params = best[1]
    print(f"kept epoch {best[2]} (val loss {best[0]:.4f})")

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "wb") as f:
        f.write(b"RBVN")
        f.write(struct.pack("<iii", 1, dim, H))
        for arr in (mean, inv_std, params[0], params[1], params[2], params[3]):
            f.write(np.ascontiguousarray(arr, dtype="<f4").tobytes())
    # Accuracy by game phase on validation: early / mid / late turns.
    by_phase = {}
    for name, lo, hi in (("turns 1-5", 0, 5), ("turns 6-12", 6, 12), ("turns 13+", 13, 10_000)):
        rows = va[(turns[va] >= lo) & (turns[va] <= hi)]
        if len(rows) >= 20:
            by_phase[name] = {"records": int(len(rows)), "acc": evaluate(rows)[1]}
    report = {"records": n, "train": len(tr), "validation": len(va), "dim": dim, "hidden": H,
              "best_epoch": best[2], "best_val_loss": best[0],
              "best_val_acc": next(h["val_acc"] for h in history if h["epoch"] == best[2]),
              "baseline_val_loss": base_loss, "history": history, "by_phase": by_phase,
              "data": a.data}
    out.with_suffix(".json").write_text(json.dumps(report, indent=1))
    print(f"wrote {out} and {out.with_suffix('.json')}")


if __name__ == "__main__":
    main(sys.argv[1:])
