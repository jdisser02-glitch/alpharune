#!/usr/bin/env python3
"""Train the position-value network used by `eval=net:<file>` from self-play
records written by `riftbound --value-data FILE`.

usage: train_value.py OUT.rbvn DATA.rbvd [DATA2.rbvd ...] [--hidden 64] [--epochs 12]
                      [--recency 0.7] [--margin-weight 0.5]

Data files come in three formats (all read here):
  RBVD (v1): int8 label, int32 turn, uint16 nnz, nnz x (uint16 index, float32 value);
             12 reservoir-sampled decisions per game, the result as the only target.
  RBV2 (v2): int8 label (1 won / 0 lost / 2 draw), int8 perspective, int8 own score,
             int8 opponent score, int8 victory score, uint32 game id, int32 turn,
             int32 decision index, uint16 nnz, pairs; every decision of both players.
  RBV3 (v3): v2 plus a policy block per record (legal vocab slots and the search's
             visit shares, or a human's move) — ignored here, used by train_policy.py.

Model: standardised features, one ReLU hidden layer, two heads — sigmoid
P(the player whose view the features are from wins) and tanh of the final
score margin (own - opp) / victory, an auxiliary target that only shapes the
hidden layer (the saved RBVN file keeps the win head alone, so the C++ loader
and file format are unchanged). Pure numpy (Adam, L2), sparse batches.

Replay buffer: list the files oldest first; each file's rows are weighted by
recency ** (age in files from the newest). Validation holds out whole games
(game id hash) so positions of one game never straddle the split.
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

import numpy as np

DEBUG = bool(__import__('os').environ.get('TRAIN_DEBUG'))
PAIR = np.dtype([("i", "<u2"), ("v", "<f4")])


def read_records(path):
    """-> dict(dim, vocab, y, margin (nan when unknown), turns, game, idx (list), val (list),
    source, flags, sims, pol_idx (list of slot arrays), pol_val (list of target arrays), version)

    Formats: RBVD (v1), RBV2 (v2) and RBV3 (v3: v2 plus a policy block per record —
    uint8 source (0 none / 1 search / 2 human), uint8 flags (bit 0 clean boundary,
    bit 1 full simulations), uint16 sims, uint16 n_legal, n_legal x (uint16 vocab
    slot, float32 target share)). See src/ml/value_net.h."""
    data = Path(path).read_bytes()
    magic = data[:4]
    if magic not in (b"RBVD", b"RBV2", b"RBV3"):
        raise SystemExit(f"{path}: not a value-data file")
    version = {b"RBVD": 1, b"RBV2": 2, b"RBV3": 3}[magic]
    dim = struct.unpack_from("<i", data, 4)[0]
    vocab = 0
    pos, n = 8, len(data)
    if version == 3:
        vocab = struct.unpack_from("<i", data, 8)[0]
        pos = 12
    labels, margins, turns, games, idx, val = [], [], [], [], [], []
    sources, flags, sims, pol_idx, pol_val = [], [], [], [], []
    rec = 0
    empty_i, empty_v = np.zeros(0, np.int32), np.zeros(0, np.float32)
    while True:
        if version >= 2:
            if pos + 19 > n:
                break
            label, persp, own, opp, victory, gid, turn, decision, nnz = struct.unpack_from("<bbbbbIiiH", data, pos)
            pos += 19
            margins.append((own - opp) / max(1, victory))
            games.append(gid)
        else:
            if pos + 7 > n:
                break
            label = struct.unpack_from("<b", data, pos)[0]
            turn, nnz = struct.unpack_from("<iH", data, pos + 1)
            pos += 7
            margins.append(np.nan)
            games.append(0xFFFF0000 | (rec // 12))      # v1: 12 rows per game, in order
        if pos + 6 * nnz > n:
            break
        pairs = np.frombuffer(data, dtype=PAIR, count=nnz, offset=pos)
        pos += 6 * nnz
        if version == 3:
            if pos + 6 > n:
                break
            src, fl, sm, n_legal = struct.unpack_from("<BBHH", data, pos)
            pos += 6
            if pos + 6 * n_legal > n:
                break
            pp = np.frombuffer(data, dtype=PAIR, count=n_legal, offset=pos)
            pos += 6 * n_legal
            sources.append(src); flags.append(fl); sims.append(sm)
            pol_idx.append(pp["i"].astype(np.int32)); pol_val.append(pp["v"].astype(np.float32))
        else:
            sources.append(0); flags.append(0); sims.append(0)
            pol_idx.append(empty_i); pol_val.append(empty_v)
        labels.append(0.5 if label == 2 else float(label))
        turns.append(turn)
        idx.append(pairs["i"].astype(np.int32))
        val.append(pairs["v"].astype(np.float32))
        rec += 1
    return {"dim": dim, "vocab": vocab, "y": np.array(labels, np.float32), "margin": np.array(margins, np.float32),
            "turns": np.array(turns, np.int32), "game": np.array(games, np.uint32),
            "idx": idx, "val": val, "version": version,
            "source": np.array(sources, np.uint8), "flags": np.array(flags, np.uint8), "sims": np.array(sims, np.int32),
            "pol_idx": pol_idx, "pol_val": pol_val}


class SparseRows:
    """CSR rows; batches gather the non-zeros of the selected rows."""

    def __init__(self, dim, rows_idx, rows_val):
        self.dim = dim
        lens = np.fromiter((len(r) for r in rows_idx), np.int64, len(rows_idx))
        self.indptr = np.concatenate([[0], np.cumsum(lens)])
        self.idx = np.concatenate(rows_idx) if rows_idx else np.zeros(0, np.int32)
        self.val = np.concatenate(rows_val) if rows_val else np.zeros(0, np.float32)

    def gather(self, rows):
        """-> (row_of_nz, feature index, value) for the batch `rows`."""
        starts = self.indptr[rows]
        lens = self.indptr[rows + 1] - starts
        total = int(lens.sum())
        if total == 0:
            return np.zeros(0, np.int64), np.zeros(0, np.int32), np.zeros(0, np.float32)
        offs = np.repeat(np.cumsum(lens) - lens, lens)
        pos = np.repeat(starts, lens) + (np.arange(total) - offs)
        row_of = np.repeat(np.arange(len(rows)), lens)
        return row_of, self.idx[pos], self.val[pos]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("data", nargs="+", help="data files, oldest first")
    ap.add_argument("--hidden", type=int, default=64)
    ap.add_argument("--epochs", type=int, default=12)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--l2", type=float, default=1e-4)
    ap.add_argument("--batch", type=int, default=256)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--recency", type=float, default=0.7, help="row weight multiplier per file of age")
    ap.add_argument("--margin-weight", type=float, default=0.5, help="weight of the score-margin loss")
    ap.add_argument("--val-frac", type=float, default=0.1, help="share of games held out")
    ap.add_argument("--max-rows-per-epoch", type=int, default=400_000)
    a = ap.parse_args(argv)
    t_start = time.time()

    dim = None
    parts, file_report = [], []
    for k, p in enumerate(a.data):
        d = read_records(p)
        dim = dim or d["dim"]
        if d["dim"] != dim:
            raise SystemExit("feature dimensions differ between files")
        age = len(a.data) - 1 - k
        d["w"] = np.full(len(d["y"]), a.recency ** age, np.float32)
        d["file"] = k
        parts.append(d)
        file_report.append({"file": p, "records": int(len(d["y"])), "format": d["version"],
                            "games": int(len(np.unique(d["game"]))), "weight": float(a.recency ** age)})
    y = np.concatenate([d["y"] for d in parts])
    margin = np.concatenate([d["margin"] for d in parts])
    turns = np.concatenate([d["turns"] for d in parts])
    games = np.concatenate([d["game"].astype(np.uint64) + (np.uint64(d["file"]) << np.uint64(32)) for d in parts])
    w = np.concatenate([d["w"] for d in parts])
    X = SparseRows(dim, sum((d["idx"] for d in parts), []), sum((d["val"] for d in parts), []))
    n = len(y)
    if n < 200:
        raise SystemExit(f"only {n} records: play more games first")
    # Validation = whole games chosen by a hash of the game id.
    h = (games * np.uint64(0x9E3779B97F4A7C15)) >> np.uint64(40)
    is_val = (h % np.uint64(1000)) < np.uint64(int(a.val_frac * 1000))
    if is_val.sum() < 100 or (~is_val).sum() < 100:
        is_val = np.arange(n) >= n - max(100, n // 10)
    tr, va = np.flatnonzero(~is_val), np.flatnonzero(is_val)
    has_margin = ~np.isnan(margin)
    margin = np.nan_to_num(margin)

    # Standardisation (feature means / variances over all rows; label-free).
    s1 = np.bincount(X.idx, weights=X.val.astype(np.float64), minlength=dim)[:dim]
    s2 = np.bincount(X.idx, weights=X.val.astype(np.float64) ** 2, minlength=dim)[:dim]
    mean = s1 / n
    var = np.maximum(s2 / n - mean ** 2, 0)
    inv_std = np.where(var > 1e-8, 1.0 / np.sqrt(var + 1e-8), 0.0).astype(np.float32)
    mean = mean.astype(np.float32)
    live = int((inv_std > 0).sum())

    # Sparse parameterisation (see ValueNet::load): the weights stay in the
    # standardised scale, W1z[i, h] = W1[h, i], applied to x_i * inv_std_i over
    # the non-zero inputs only, and the mean shift of every feature is folded
    # into the bias: b1z = b1 - W1z^T (mean * inv_std). Keeping the weights in
    # z-scale matters for Adam, whose steps are scale-free: on raw counts
    # (deck sizes, turn and decision numbers) the same steps shift the
    # pre-activations by units and kill every ReLU within a few batches.
    rng = np.random.default_rng(a.seed)
    H = a.hidden
    mz = (mean * inv_std).astype(np.float32)
    W1f = (rng.standard_normal((dim, H)) * np.sqrt(2.0 / max(1, live))).astype(np.float32)
    b1f = -(W1f * mz[:, None]).sum(0).astype(np.float32)
    w2 = (rng.standard_normal(H) * np.sqrt(1.0 / H)).astype(np.float32)   # win head
    w3 = (rng.standard_normal(H) * np.sqrt(1.0 / H)).astype(np.float32)   # margin head
    p_tr = float(np.average(y[tr], weights=w[tr]))
    b2 = np.float32(np.log(max(p_tr, 1e-3) / max(1 - p_tr, 1e-3)))
    b3 = np.float32(0.0)
    params = [W1f, b1f, w2, np.array([b2], np.float32), w3, np.array([b3], np.float32)]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    step = 0

    def forward(rows):
        row_of, fi, fv = X.gather(rows)
        fv = fv * inv_std[fi]                      # standardised (the mean is in the bias)
        a1 = np.tile(params[1], (len(rows), 1))
        np.add.at(a1, row_of, params[0][fi] * fv[:, None])
        hid = np.maximum(a1, 0)
        logit = hid @ params[2] + params[3][0]
        mpred = np.tanh(hid @ params[4] + params[5][0])
        return (row_of, fi, fv), a1, hid, logit, mpred

    def evaluate(rows):
        losses, weights, correct, msq, mcount = [], [], 0.0, 0.0, 0
        for k in range(0, len(rows), 2048):
            rr = rows[k:k + 2048]
            _, _, _, logit, mpred = forward(rr)
            p = 1 / (1 + np.exp(-np.clip(logit, -30, 30)))
            yy = y[rr]
            losses.append(-(yy * np.log(p + 1e-7) + (1 - yy) * np.log(1 - p + 1e-7)))
            weights.append(np.ones(len(rr)))
            correct += float(((p > 0.5) == (yy > 0.5)).sum())
            hm = has_margin[rr]
            msq += float(((mpred[hm] - margin[rr][hm]) ** 2).sum())
            mcount += int(hm.sum())
        loss = float(np.concatenate(losses).mean())
        return loss, correct / len(rows), (msq / mcount if mcount else None)

    base_p = p_tr
    base_loss = float(-(y[va] * np.log(base_p) + (1 - y[va]) * np.log(1 - base_p)).mean())
    print(f"{n} records ({len(tr)} train / {len(va)} validation, {len(np.unique(games))} games), dim {dim}, "
          f"{live} live features, {int(has_margin.sum())} with a score margin; "
          f"baseline val log-loss {base_loss:.4f}", flush=True)
    history = []
    best = (float("inf"), None, 0)          # (val loss, params, epoch): early stopping
    wn = w / w.mean()
    for ep in range(a.epochs):
        order = rng.permutation(tr)
        if len(order) > a.max_rows_per_epoch:
            order = order[: a.max_rows_per_epoch]
        for k in range(0, len(order), a.batch):
            rr = order[k:k + a.batch]
            (row_of, fi, fv), a1, hid, logit, mpred = forward(rr)
            B = len(rr)
            ww = wn[rr]
            p = 1 / (1 + np.exp(-np.clip(logit, -30, 30)))
            g = ww * (p - y[rr]) / B                                 # dL/dlogit
            hm = has_margin[rr].astype(np.float32)
            gm = ww * hm * a.margin_weight * 2 * (mpred - margin[rr]) * (1 - mpred ** 2) / B   # dL/d(pre-tanh)
            gw2 = hid.T @ g + a.l2 * params[2]
            gb2 = np.array([g.sum()], np.float32)
            gw3 = hid.T @ gm + a.l2 * params[4]
            gb3 = np.array([gm.sum()], np.float32)
            gh = (np.outer(g, params[2]) + np.outer(gm, params[4])) * (a1 > 0)
            gW1 = np.zeros_like(params[0])
            np.add.at(gW1, fi, gh[row_of] * fv[:, None])
            gW1 += a.l2 * params[0]
            gb1 = gh.sum(0)
            grads = [gW1, gb1.astype(np.float32), gw2.astype(np.float32), gb2, gw3.astype(np.float32), gb3]
            if DEBUG and step % 10 == 0:
                bl = float(-(y[rr] * np.log(p + 1e-7) + (1 - y[rr]) * np.log(1 - p + 1e-7)).mean())
                print(f"    step {step}: batch loss {bl:.4f} active {(a1 > 0).mean():.3f} a1 std {a1.std():.2f} "
                      f"|gW1| {np.abs(gW1).max():.2e} |gh| {np.abs(gh).max():.2e} |g| {np.abs(g).max():.2e} p std {p.std():.3f}", flush=True)
            step += 1
            for i, (pp, gg) in enumerate(zip(params, grads)):
                m[i] = 0.9 * m[i] + 0.1 * gg
                v[i] = 0.999 * v[i] + 0.001 * gg * gg
                mh = m[i] / (1 - 0.9 ** step)
                vh = v[i] / (1 - 0.999 ** step)
                pp -= (a.lr * mh / (np.sqrt(vh) + 1e-8)).astype(np.float32)
        tl, ta, tm = evaluate(tr[: min(len(tr), 20000)])
        vl, vacc, vm = evaluate(va)
        history.append({"epoch": ep + 1, "train_loss": tl, "train_acc": ta, "train_margin_mse": tm,
                        "val_loss": vl, "val_acc": vacc, "val_margin_mse": vm})
        print(f"epoch {ep + 1:2d}: train loss {tl:.4f} acc {ta:.3f} | val loss {vl:.4f} acc {vacc:.3f}"
              + (f" margin mse {vm:.4f}" if vm is not None else ""), flush=True)
        if vl < best[0]:
            best = (vl, [p.copy() for p in params], ep + 1)
    params = best[1]
    print(f"kept epoch {best[2]} (val loss {best[0]:.4f})")

    # Unfold to the RBVN v1 layout: W1[h, i] = W1z[i, h], b1 = b1z + W1z^T (mean * inv_std).
    W1f, b1f, w2, b2 = params[0], params[1], params[2], params[3]
    W1 = W1f.T.copy().astype(np.float32)
    W1[:, inv_std <= 0] = 0.0
    b1 = (b1f + (W1f * mz[:, None]).sum(0)).astype(np.float32)
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "wb") as f:
        f.write(b"RBVN")
        f.write(struct.pack("<iii", 1, dim, H))
        for arr in (mean, inv_std, W1, b1, w2, b2):
            f.write(np.ascontiguousarray(arr, dtype="<f4").tobytes())
    # Accuracy by game phase on validation: early / mid / late turns.
    by_phase = {}
    for name, lo, hi in (("turns 1-5", 0, 5), ("turns 6-12", 6, 12), ("turns 13+", 13, 10_000)):
        rows = va[(turns[va] >= lo) & (turns[va] <= hi)]
        if len(rows) >= 20:
            by_phase[name] = {"records": int(len(rows)), "acc": evaluate(rows)[1]}
    best_row = next(h for h in history if h["epoch"] == best[2])
    report = {"records": n, "train": int(len(tr)), "validation": int(len(va)), "games": int(len(np.unique(games))),
              "dim": dim, "hidden": H, "best_epoch": best[2], "best_val_loss": best[0],
              "best_val_acc": best_row["val_acc"], "best_val_margin_mse": best_row["val_margin_mse"],
              "baseline_val_loss": base_loss, "history": history, "by_phase": by_phase,
              "recency": a.recency, "margin_weight": a.margin_weight, "files": file_report, "data": a.data,
              "seconds": round(time.time() - t_start, 1)}
    out.with_suffix(".json").write_text(json.dumps(report, indent=1))
    print(f"wrote {out} and {out.with_suffix('.json')} ({report['seconds']}s)")


if __name__ == "__main__":
    main(sys.argv[1:])
