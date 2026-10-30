#!/usr/bin/env python3
"""Train the two-head (policy + value) network used by `pmcts:eval=policy:<file>`
from self-play RBV3 records (riftbound --value-data with a searching agent) and,
optionally, the human imitation set (lab/tools/build_human_dataset.py).

usage: train_policy.py OUT.rbpn DATA.rbvd [DATA2.rbvd ...] [--human HUMAN.rbv3 ...]
                       [--hidden 64] [--epochs 8] [--policy-weight 1.0] [--human-weight 0.3]
                       [--entropy 0.01] [--prev PREV.rbpn --prev-weight 0.1]

Model (pure numpy, Adam, sparse batches as train_value.py): standardised
features -> one ReLU hidden layer shared by three heads — sigmoid P(the
perspective player wins), tanh of the final score margin, and a policy head
over the dense action vocabulary (openspiel/action_vocab.h) evaluated only on
each row's legal slots (a legal-masked softmax; every row stores its legal slot
list). Losses: win log-loss + margin MSE (as train_value.py), cross-entropy of
the legal softmax to the search's visit distribution (rows with source =
search) and to the human's one-hot (source = human, weighted by
--human-weight), minus --entropy times the policy entropy (keeps the policy
from collapsing), and — with --prev — the target is mixed with the previous
net's policy (--prev-weight), a KL pull toward the last iteration that damps
self-play cycling (the magnetic-mirror-descent idea).

Held out: --val-frac of the self-play games (by game id) for the value and
policy metrics, and --human-val-frac of the human games for the top-1 / top-3
agreement metrics (all human rows are validation when --human-weight is 0).
Human rows carry no value loss unless --human-value is given.

Writes OUT.rbpn (both heads; src/ml/policy_net.h), OUT.rbvn (the value head
alone, loadable by eval=net:) and OUT.json (the report).
"""
import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rbpn import read_rbpn, write_rbpn, write_rbvn  # noqa: E402
from train_value import SparseRows, read_records  # noqa: E402

SRC_NONE, SRC_SEARCH, SRC_HUMAN = 0, 1, 2
FLAG_CLEAN = 1


class PolicyRows:
    """Legal slot lists with targets, CSR like SparseRows."""

    def __init__(self, rows_idx, rows_val):
        lens = np.fromiter((len(r) for r in rows_idx), np.int64, len(rows_idx))
        self.indptr = np.concatenate([[0], np.cumsum(lens)])
        self.idx = np.concatenate(rows_idx) if rows_idx else np.zeros(0, np.int32)
        self.val = np.concatenate(rows_val) if rows_val else np.zeros(0, np.float32)
        self.lens = lens

    def gather(self, rows):
        """-> (segment row (position in `rows`), slot, target, segment starts) for rows with a policy."""
        starts = self.indptr[rows]
        lens = self.indptr[rows + 1] - starts
        total = int(lens.sum())
        if total == 0:
            return np.zeros(0, np.int64), np.zeros(0, np.int32), np.zeros(0, np.float32), np.zeros(0, np.int64)
        offs = np.repeat(np.cumsum(lens) - lens, lens)
        pos = np.repeat(starts, lens) + (np.arange(total) - offs)
        seg_row = np.repeat(np.arange(len(rows)), lens)
        seg_starts = np.flatnonzero(np.r_[True, seg_row[1:] != seg_row[:-1]])
        return seg_row, self.idx[pos], self.val[pos], seg_starts


def segment_softmax(logits, seg_starts):
    """Softmax within contiguous segments. -> (p, log p)."""
    seg_id = np.cumsum(np.isin(np.arange(len(logits)), seg_starts)) - 1
    mx = np.maximum.reduceat(logits, seg_starts)
    shifted = logits - mx[seg_id]
    e = np.exp(shifted)
    z = np.add.reduceat(e, seg_starts)
    logz = np.log(z)
    logp = shifted - logz[seg_id]
    return np.exp(logp), logp, seg_id


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="OUT.rbpn (OUT.rbvn and OUT.json are written beside it)")
    ap.add_argument("data", nargs="+", help="self-play data files, oldest first")
    ap.add_argument("--human", nargs="*", default=[], help="human imitation files (RBV3, source = human)")
    ap.add_argument("--hidden", type=int, default=64)
    ap.add_argument("--epochs", type=int, default=8)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--l2", type=float, default=1e-4)
    ap.add_argument("--batch", type=int, default=256)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--recency", type=float, default=0.7, help="row weight multiplier per file of age")
    ap.add_argument("--margin-weight", type=float, default=0.5)
    ap.add_argument("--policy-weight", type=float, default=1.0, help="weight of the search-target cross-entropy")
    ap.add_argument("--human-weight", type=float, default=0.3, help="weight of the human one-hot cross-entropy")
    ap.add_argument("--human-repeat", type=int, default=4, help="times each human training row appears per epoch")
    ap.add_argument("--human-value", action="store_true", help="also fit the value heads on human rows (log result)")
    ap.add_argument("--entropy", type=float, default=0.01, help="entropy bonus on the legal softmax")
    ap.add_argument("--prev", default="", help="previous RBPN: mix its policy into the targets")
    ap.add_argument("--prev-weight", type=float, default=0.1)
    ap.add_argument("--min-sims", type=int, default=0, help="ignore search targets from fewer simulations")
    ap.add_argument("--val-frac", type=float, default=0.1, help="share of self-play games held out")
    ap.add_argument("--human-val-frac", type=float, default=0.5, help="share of human games held out")
    ap.add_argument("--max-rows-per-epoch", type=int, default=400_000)
    ap.add_argument("--init-value", default="", help="RBVN/RBPN whose first layer and value head initialise the net")
    a = ap.parse_args(argv)
    t_start = time.time()
    rng = np.random.default_rng(a.seed)

    # ── data ──
    dim = vocab = None
    parts, file_report = [], []
    for k, p in enumerate(list(a.data) + list(a.human)):
        d = read_records(p)
        human = k >= len(a.data)
        dim = dim or d["dim"]
        if d["dim"] != dim:
            raise SystemExit("feature dimensions differ between files")
        if d["vocab"]:
            vocab = max(vocab or 0, d["vocab"])
        age = 0 if human else len(a.data) - 1 - k
        d["w"] = np.full(len(d["y"]), a.recency ** age, np.float32)
        d["file"] = k
        d["human"] = np.full(len(d["y"]), human, bool)
        parts.append(d)
        file_report.append({"file": p, "records": int(len(d["y"])), "format": d["version"], "human": human,
                            "games": int(len(np.unique(d["game"]))), "weight": float(a.recency ** age),
                            "with_policy": int((d["source"] != SRC_NONE).sum())})
    if not vocab:
        raise SystemExit("no RBV3 file with policy targets among the inputs (play with a searching agent and --value-data)")
    cat = lambda key: np.concatenate([d[key] for d in parts])  # noqa: E731
    y, margin, turns, w = cat("y"), cat("margin"), cat("turns"), cat("w")
    source, flags, sims, is_human = cat("source"), cat("flags"), cat("sims"), cat("human")
    games = np.concatenate([d["game"].astype(np.uint64) + (np.uint64(d["file"]) << np.uint64(32)) for d in parts])
    X = SparseRows(dim, sum((d["idx"] for d in parts), []), sum((d["val"] for d in parts), []))
    P = PolicyRows(sum((d["pol_idx"] for d in parts), []), sum((d["pol_val"] for d in parts), []))
    n = len(y)
    if n < 200:
        raise SystemExit(f"only {n} records: play more games first")
    slot_max = int(P.idx.max()) if len(P.idx) else 0
    if slot_max >= vocab:
        raise SystemExit(f"slot {slot_max} outside the vocab of {vocab}")
    has_policy = (P.lens > 0) & (source != SRC_NONE)
    has_policy &= ~((source == SRC_SEARCH) & (sims < a.min_sims))
    has_margin = ~np.isnan(margin)
    margin = np.nan_to_num(margin)
    value_rows = ~is_human | a.human_value                     # rows with a value loss

    # Hold-out: whole games by a hash of the game id; human games separately.
    h = (games * np.uint64(0x9E3779B97F4A7C15)) >> np.uint64(40)
    frac = np.where(is_human, a.human_val_frac, a.val_frac)
    is_val = (h % np.uint64(1000)) < (frac * 1000).astype(np.uint64)
    if a.human_weight <= 0:
        is_val |= is_human
    sp = ~is_human
    if (is_val & sp).sum() < 100 or (~is_val & sp).sum() < 100:
        order_sp = np.flatnonzero(sp)
        cut = order_sp[len(order_sp) - max(100, len(order_sp) // 10)]
        is_val = np.where(sp, np.arange(n) >= cut, is_val)
    tr = np.flatnonzero(~is_val)
    va_sp = np.flatnonzero(is_val & sp)
    va_hu = np.flatnonzero(is_val & is_human)
    tr_hu = np.flatnonzero(~is_val & is_human)

    # ── standardisation (label-free) ──
    s1 = np.bincount(X.idx, weights=X.val.astype(np.float64), minlength=dim)[:dim]
    s2 = np.bincount(X.idx, weights=X.val.astype(np.float64) ** 2, minlength=dim)[:dim]
    mean = s1 / n
    var = np.maximum(s2 / n - mean ** 2, 0)
    inv_std = np.where(var > 1e-8, 1.0 / np.sqrt(var + 1e-8), 0.0).astype(np.float32)
    mean = mean.astype(np.float32)
    live = int((inv_std > 0).sum())
    mz = (mean * inv_std).astype(np.float32)

    # ── parameters (z-scale first layer, see train_value.py) ──
    H = a.hidden
    V = vocab
    W1f = (rng.standard_normal((dim, H)) * np.sqrt(2.0 / max(1, live))).astype(np.float32)
    b1f = -(W1f * mz[:, None]).sum(0).astype(np.float32)
    w2 = (rng.standard_normal(H) * np.sqrt(1.0 / H)).astype(np.float32)
    w3 = (rng.standard_normal(H) * np.sqrt(1.0 / H)).astype(np.float32)
    vr = tr[value_rows[tr]]
    p_tr = float(np.average(y[vr], weights=w[vr])) if len(vr) else 0.5
    b2 = np.array([np.log(max(p_tr, 1e-3) / max(1 - p_tr, 1e-3))], np.float32)
    b3 = np.array([0.0], np.float32)
    Wp = (rng.standard_normal((V, H)) * 0.01).astype(np.float32)
    bp = np.zeros(V, np.float32)
    if a.init_value:
        if not a.init_value.endswith(".rbpn"):
            raise SystemExit("--init-value takes an RBPN file")
        init = read_rbpn(a.init_value)
        if init["W1"].shape == (H, dim):
            # Re-fold the stored raw-scale layer into this run's standardisation.
            W1f = np.where(inv_std[:, None] > 0, init["W1"].T / np.maximum(inv_std[:, None], 1e-12), 0).astype(np.float32)
            b1f = (init["b1"] - (W1f * mz[:, None]).sum(0)).astype(np.float32)
            w2, b2, w3, b3 = init["w2"].copy(), init["b2"].copy(), init["wm"].copy(), init["bm"].copy()
            if init["Wp"].shape == (V, H):
                Wp, bp = init["Wp"].copy(), init["bp"].copy()
    params = [W1f, b1f, w2, b2, w3, b3, Wp, bp]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    step = 0

    # Previous policy for the KL pull: its own z-scale fold.
    prev = None
    if a.prev and a.prev_weight > 0:
        pn = read_rbpn(a.prev)
        if pn["W1"].shape[1] != dim or pn["Wp"].shape[0] != V:
            raise SystemExit("--prev has different dimensions")
        prev = {"W1z": (pn["W1"] * pn["inv_std"][None, :]).T.astype(np.float32),
                "b1z": (pn["b1"] - pn["W1"] @ (pn["mean"] * pn["inv_std"])).astype(np.float32),
                "Wp": pn["Wp"], "bp": pn["bp"]}

    ones = np.ones(dim, np.float32)

    def hidden_of(rows, W1z, b1z, scale):
        row_of, fi, fv = X.gather(rows)
        fv = fv * scale[fi]
        a1 = np.tile(b1z, (len(rows), 1))
        np.add.at(a1, row_of, W1z[fi] * fv[:, None])
        return (row_of, fi, fv), a1

    def forward(rows):
        (row_of, fi, fv), a1 = hidden_of(rows, params[0], params[1], inv_std)
        hid = np.maximum(a1, 0)
        logit = hid @ params[2] + params[3][0]
        mpred = np.tanh(hid @ params[4] + params[5][0])
        seg_row, slot, tgt, seg_starts = P.gather(rows)
        pol = None
        if len(slot):
            plogit = (hid[seg_row] * params[6][slot]).sum(1) + params[7][slot]
            p, logp, seg_id = segment_softmax(plogit, seg_starts)
            pol = (seg_row, slot, tgt, seg_starts, p, logp, seg_id)
        return (row_of, fi, fv), a1, hid, logit, mpred, pol

    def prev_policy(rows, pol):
        """Previous net's probabilities on the batch's legal slots (aligned with `pol`)."""
        _, a1 = hidden_of(rows, prev["W1z"], prev["b1z"], ones)   # raw x: prev's own standardisation is folded in
        hid = np.maximum(a1, 0)
        seg_row, slot, _, seg_starts, *_ = pol
        plogit = (hid[seg_row] * prev["Wp"][slot]).sum(1) + prev["bp"][slot]
        return segment_softmax(plogit, seg_starts)[0]

    def policy_stats(rows, pol, per_row):
        """Per-row: top-1 hit, rank of the target's best slot, CE, entropy."""
        seg_row, slot, tgt, seg_starts, p, logp, seg_id = pol
        ce = -np.add.reduceat(tgt * logp, seg_starts)
        ent = -np.add.reduceat(p * logp, seg_starts)
        # Target argmax per segment (first maximum) and its rank under p.
        tmax = np.maximum.reduceat(tgt, seg_starts)
        pos = np.arange(len(tgt))
        tgt_best = np.minimum.reduceat(np.where(tgt >= tmax[seg_id], pos, len(tgt)), seg_starts)
        per_row["n_legal"].extend(np.diff(np.r_[seg_starts, len(tgt)]).tolist())
        p_best = p[tgt_best]
        rank = np.add.reduceat((p > p_best[seg_id]).astype(np.int64), seg_starts)
        rows_with = rows[seg_row[seg_starts]]
        per_row["row"].extend(rows_with.tolist())
        per_row["top1"].extend((rank == 0).tolist())
        per_row["top3"].extend((rank < 3).tolist())
        per_row["ce"].extend(ce.tolist())
        per_row["ent"].extend(ent.tolist())
        per_row["tgt_ent"].extend((-np.add.reduceat(np.where(tgt > 0, tgt * np.log(np.maximum(tgt, 1e-12)), 0), seg_starts)).tolist())

    def evaluate(rows):
        """Value metrics on rows with a value loss; policy metrics on rows with a target."""
        losses, correct, msq, mcount, vcount = [], 0.0, 0.0, 0, 0
        per_row = {"row": [], "top1": [], "top3": [], "ce": [], "ent": [], "tgt_ent": [], "n_legal": []}
        for k in range(0, len(rows), 2048):
            rr = rows[k:k + 2048]
            _, _, _, logit, mpred, pol = forward(rr)
            vm = value_rows[rr]
            if vm.any():
                pr = 1 / (1 + np.exp(-np.clip(logit[vm], -30, 30)))
                yy = y[rr][vm]
                losses.append(-(yy * np.log(pr + 1e-7) + (1 - yy) * np.log(1 - pr + 1e-7)))
                correct += float(((pr > 0.5) == (yy > 0.5)).sum())
                vcount += int(vm.sum())
                hm = has_margin[rr] & vm
                msq += float(((mpred[hm] - margin[rr][hm]) ** 2).sum())
                mcount += int(hm.sum())
            if pol is not None:
                policy_stats(rr, pol, per_row)
        out = {"rows": int(len(rows))}
        if vcount:
            out.update({"val_loss": float(np.concatenate(losses).mean()), "val_acc": correct / vcount,
                        "val_margin_mse": (msq / mcount if mcount else None), "value_rows": vcount})
        if per_row["row"]:
            rr = np.array(per_row["row"])
            keep = has_policy[rr]
            t1, t3 = np.array(per_row["top1"])[keep], np.array(per_row["top3"])[keep]
            clean = (flags[rr][keep] & FLAG_CLEAN) > 0
            out.update({"policy_rows": int(keep.sum()), "policy_top1": float(t1.mean()) if keep.any() else None,
                        "policy_top3": float(t3.mean()) if keep.any() else None,
                        "policy_ce": float(np.array(per_row["ce"])[keep].mean()) if keep.any() else None,
                        "policy_entropy": float(np.array(per_row["ent"])[keep].mean()) if keep.any() else None,
                        "target_entropy": float(np.array(per_row["tgt_ent"])[keep].mean()) if keep.any() else None,
                        "mean_legal": float(np.array(per_row["n_legal"])[keep].mean()) if keep.any() else None,
                        "clean_rows": int(clean.sum()),
                        "clean_top1": float(t1[clean].mean()) if clean.any() else None,
                        "clean_top3": float(t3[clean].mean()) if clean.any() else None})
        return out

    base_p = p_tr
    yv = y[va_sp]
    base_loss = float(-(yv * np.log(base_p) + (1 - yv) * np.log(1 - base_p)).mean()) if len(yv) else None
    print(f"{n} records: {int((~is_human).sum())} self-play ({len(np.unique(games[~is_human]))} games, "
          f"{int((has_policy & ~is_human).sum())} with search targets), {int(is_human.sum())} human "
          f"({len(np.unique(games[is_human]))} games; {len(tr_hu)} train / {len(va_hu)} validation); "
          f"dim {dim}, vocab {V}, hidden {H}, {live} live features; "
          f"baseline val log-loss {base_loss if base_loss is None else round(base_loss, 4)}", flush=True)

    # Row weights: recency x source weight; human rows repeated per epoch.
    wn = w / w[~is_human].mean()
    pol_w = np.where(is_human, a.human_weight, a.policy_weight).astype(np.float32)
    train_pool = np.concatenate([tr[~is_human[tr]]] + [tr_hu] * max(1, a.human_repeat))
    history = []
    best = (float("inf"), None, 0)
    for ep in range(a.epochs):
        order = rng.permutation(train_pool)
        if len(order) > a.max_rows_per_epoch:
            order = order[: a.max_rows_per_epoch]
        for k in range(0, len(order), a.batch):
            rr = order[k:k + a.batch]
            (row_of, fi, fv), a1, hid, logit, mpred, pol = forward(rr)
            B = len(rr)
            ww = wn[rr]
            vm = value_rows[rr].astype(np.float32)
            p = 1 / (1 + np.exp(-np.clip(logit, -30, 30)))
            g = ww * vm * (p - y[rr]) / B
            hm = has_margin[rr].astype(np.float32) * vm
            gm = ww * hm * a.margin_weight * 2 * (mpred - margin[rr]) * (1 - mpred ** 2) / B
            gh = np.outer(g, params[2]) + np.outer(gm, params[4])
            gWp = np.zeros_like(params[6])
            gbp = np.zeros_like(params[7])
            if pol is not None:
                seg_row, slot, tgt, seg_starts, pp, logp, seg_id = pol
                if prev is not None:
                    tgt = (1 - a.prev_weight) * tgt + a.prev_weight * prev_policy(rr, pol)
                ent = -np.add.reduceat(pp * logp, seg_starts)
                row_w = (ww[seg_row] * pol_w[rr][seg_row] * has_policy[rr][seg_row]) / B
                gl = row_w * ((pp - tgt) + a.entropy * pp * (logp + ent[seg_id]))
                np.add.at(gh, seg_row, gl[:, None] * params[6][slot])
                np.add.at(gWp, slot, gl[:, None] * hid[seg_row])
                np.add.at(gbp, slot, gl)
            gh *= (a1 > 0)
            gw2 = hid.T @ g + a.l2 * params[2]
            gb2 = np.array([g.sum()], np.float32)
            gw3 = hid.T @ gm + a.l2 * params[4]
            gb3 = np.array([gm.sum()], np.float32)
            gW1 = np.zeros_like(params[0])
            np.add.at(gW1, fi, gh[row_of] * fv[:, None])
            gW1 += a.l2 * params[0]
            gb1 = gh.sum(0)
            gWp += a.l2 * params[6]
            grads = [gW1, gb1.astype(np.float32), gw2.astype(np.float32), gb2, gw3.astype(np.float32), gb3,
                     gWp.astype(np.float32), gbp.astype(np.float32)]
            step += 1
            for i, (pp_, gg) in enumerate(zip(params, grads)):
                m[i] = 0.9 * m[i] + 0.1 * gg
                v[i] = 0.999 * v[i] + 0.001 * gg * gg
                mh = m[i] / (1 - 0.9 ** step)
                vh = v[i] / (1 - 0.999 ** step)
                pp_ -= (a.lr * mh / (np.sqrt(vh) + 1e-8)).astype(np.float32)
        ev_tr = evaluate(tr[~is_human[tr]][: min(len(tr), 20000)])
        ev_va = evaluate(va_sp)
        ev_hu = evaluate(va_hu) if len(va_hu) else {}
        row = {"epoch": ep + 1, "train": ev_tr, "val": ev_va, "human": ev_hu}
        history.append(row)
        print(f"epoch {ep + 1:2d}: train loss {ev_tr.get('val_loss', float('nan')):.4f} pol top1 {ev_tr.get('policy_top1') or 0:.3f} | "
              f"val loss {ev_va.get('val_loss', float('nan')):.4f} acc {ev_va.get('val_acc', float('nan')):.3f} "
              f"pol top1 {ev_va.get('policy_top1') or 0:.3f} top3 {ev_va.get('policy_top3') or 0:.3f} "
              f"ent {ev_va.get('policy_entropy') or 0:.2f}/{ev_va.get('target_entropy') or 0:.2f}"
              + (f" | human top1 {ev_hu.get('policy_top1') or 0:.3f} top3 {ev_hu.get('policy_top3') or 0:.3f} "
                 f"(clean {ev_hu.get('clean_top1') or 0:.3f}/{ev_hu.get('clean_top3') or 0:.3f}, n {ev_hu.get('policy_rows', 0)})"
                 if ev_hu else ""), flush=True)
        # Model selection: value loss plus policy cross-entropy on the held-out self-play games.
        score = ev_va.get("val_loss", 0.0) + (ev_va.get("policy_ce") or 0.0)
        if score < best[0]:
            best = (score, [p_.copy() for p_ in params], ep + 1)
    params = best[1]
    print(f"kept epoch {best[2]}")

    # ── unfold to the raw-scale layout and write ──
    W1f, b1f, w2, b2, w3, b3, Wp, bp = params
    W1 = W1f.T.copy().astype(np.float32)
    W1[:, inv_std <= 0] = 0.0
    b1 = (b1f + (W1f * mz[:, None]).sum(0)).astype(np.float32)
    net = {"mean": mean, "inv_std": inv_std, "W1": W1, "b1": b1, "w2": w2, "b2": b2, "wm": w3, "bm": b3, "Wp": Wp, "bp": bp}
    out = Path(a.out)
    write_rbpn(out, net)
    write_rbvn(out.with_suffix(".rbvn"), net)
    best_row = next(r for r in history if r["epoch"] == best[2])
    report = {"records": n, "self_play_records": int((~is_human).sum()), "human_records": int(is_human.sum()),
              "train": int(len(tr)), "validation": int(len(va_sp)), "human_validation": int(len(va_hu)),
              "games": int(len(np.unique(games[~is_human]))), "human_games": int(len(np.unique(games[is_human]))),
              "search_target_rows": int((has_policy & ~is_human).sum()),
              "dim": dim, "vocab": V, "hidden": H, "best_epoch": best[2],
              "best_val_loss": best_row["val"].get("val_loss"), "best_val_acc": best_row["val"].get("val_acc"),
              "best_val_margin_mse": best_row["val"].get("val_margin_mse"), "baseline_val_loss": base_loss,
              "policy_top1": best_row["val"].get("policy_top1"), "policy_top3": best_row["val"].get("policy_top3"),
              "policy_ce": best_row["val"].get("policy_ce"), "policy_entropy": best_row["val"].get("policy_entropy"),
              "target_entropy": best_row["val"].get("target_entropy"), "policy_val_rows": best_row["val"].get("policy_rows"),
              "human_top1": best_row["human"].get("policy_top1"), "human_top3": best_row["human"].get("policy_top3"),
              "human_clean_top1": best_row["human"].get("clean_top1"), "human_clean_top3": best_row["human"].get("clean_top3"),
              "human_val_rows": best_row["human"].get("policy_rows"), "human_clean_rows": best_row["human"].get("clean_rows"),
              "history": history, "recency": a.recency, "margin_weight": a.margin_weight,
              "policy_weight": a.policy_weight, "human_weight": a.human_weight, "entropy": a.entropy,
              "prev": a.prev or None, "prev_weight": a.prev_weight if a.prev else None,
              "files": file_report, "data": a.data, "human": a.human, "seconds": round(time.time() - t_start, 1)}
    out.with_suffix(".json").write_text(json.dumps(report, indent=1))
    print(f"wrote {out}, {out.with_suffix('.rbvn')} and {out.with_suffix('.json')} ({report['seconds']}s)")


if __name__ == "__main__":
    main(sys.argv[1:])
