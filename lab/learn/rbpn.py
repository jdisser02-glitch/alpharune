#!/usr/bin/env python3
"""The RBPN (policy + value network) and RBVN (value only) file formats, shared
by train_policy.py, the test-fixture generator and anyone reading a net back.

RBPN v1 (little-endian): "RBPN", int32 version 1, int32 in_dim, int32 hidden,
int32 vocab, then float32 arrays mean[in], inv_std[in], W1[hidden, in],
b1[hidden], w2[hidden], b2[1] (win logit), wm[hidden], bm[1] (margin, tanh),
Wp[vocab, hidden], bp[vocab] (policy logits). C++ reader: src/ml/policy_net.cpp.

RBVN v1: "RBVN", int32 1, in_dim, hidden, then mean, inv_std, W1, b1, w2, b2.

    python3 lab/learn/rbpn.py --make-fixture tests/data/tiny_policy   # tiny net + reference outputs
"""
import json
import struct
import sys
from pathlib import Path

import numpy as np

FIELDS = ("mean", "inv_std", "W1", "b1", "w2", "b2", "wm", "bm", "Wp", "bp")


def write_rbpn(path, net):
    """net: dict with the FIELDS arrays (W1 [hidden, in], Wp [vocab, hidden])."""
    hidden, in_dim = net["W1"].shape
    vocab = net["Wp"].shape[0]
    assert net["Wp"].shape[1] == hidden
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"RBPN")
        f.write(struct.pack("<iiii", 1, in_dim, hidden, vocab))
        for k in FIELDS:
            f.write(np.ascontiguousarray(np.atleast_1d(net[k]), dtype="<f4").tobytes())


def write_rbvn(path, net):
    hidden, in_dim = net["W1"].shape
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"RBVN")
        f.write(struct.pack("<iii", 1, in_dim, hidden))
        for k in ("mean", "inv_std", "W1", "b1", "w2", "b2"):
            f.write(np.ascontiguousarray(np.atleast_1d(net[k]), dtype="<f4").tobytes())


def read_rbpn(path):
    data = Path(path).read_bytes()
    if data[:4] != b"RBPN":
        raise ValueError(f"{path}: not an RBPN file")
    version, in_dim, hidden, vocab = struct.unpack_from("<iiii", data, 4)
    if version != 1:
        raise ValueError(f"{path}: unsupported RBPN version {version}")
    pos = 20
    shapes = {"mean": (in_dim,), "inv_std": (in_dim,), "W1": (hidden, in_dim), "b1": (hidden,), "w2": (hidden,),
              "b2": (1,), "wm": (hidden,), "bm": (1,), "Wp": (vocab, hidden), "bp": (vocab,)}
    net = {}
    for k in FIELDS:
        n = int(np.prod(shapes[k]))
        net[k] = np.frombuffer(data, dtype="<f4", count=n, offset=pos).reshape(shapes[k]).astype(np.float32)
        pos += 4 * n
    return net


def forward(net, x_dense, slots):
    """Reference dense forward: -> (win, margin, policy over `slots`).
    Duplicate slots each report the slot's probability; a slot out of range
    gets 0 and is left out of the normalisation."""
    z = (np.asarray(x_dense, np.float64) - net["mean"]) * net["inv_std"]
    hid = np.maximum(net["W1"].astype(np.float64) @ z + net["b1"], 0.0)
    win = 1.0 / (1.0 + np.exp(-(hid @ net["w2"] + net["b2"][0])))
    margin = np.tanh(hid @ net["wm"] + net["bm"][0])
    vocab = net["Wp"].shape[0]
    uniq = sorted({s for s in slots if 0 <= s < vocab})
    logits = {s: float(net["Wp"][s].astype(np.float64) @ hid + net["bp"][s]) for s in uniq}
    if uniq:
        mx = max(logits.values())
        z = sum(np.exp(v - mx) for v in logits.values())
        probs = {s: float(np.exp(v - mx) / z) for s, v in logits.items()}
    else:
        probs = {}
    return float(win), float(margin), [probs.get(s, 0.0) for s in slots]


def make_fixture(prefix, in_dim=50, hidden=6, vocab=40, seed=3):
    """A small random net and its reference outputs on a few sparse inputs,
    for tests/test_policy_net.cpp."""
    rng = np.random.default_rng(seed)
    net = {
        "mean": rng.normal(0.2, 0.3, in_dim).astype(np.float32),
        "inv_std": np.abs(rng.normal(1.5, 0.5, in_dim)).astype(np.float32),
        "W1": rng.normal(0, 0.3, (hidden, in_dim)).astype(np.float32),
        "b1": rng.normal(0, 0.1, hidden).astype(np.float32),
        "w2": rng.normal(0, 0.5, hidden).astype(np.float32),
        "b2": np.array([0.1], np.float32),
        "wm": rng.normal(0, 0.5, hidden).astype(np.float32),
        "bm": np.array([-0.05], np.float32),
        "Wp": rng.normal(0, 0.4, (vocab, hidden)).astype(np.float32),
        "bp": rng.normal(0, 0.2, vocab).astype(np.float32),
    }
    net["inv_std"][7] = 0.0   # a dead feature
    write_rbpn(str(prefix) + ".rbpn", net)
    cases = []
    inputs = [
        ([(0, 1.0), (3, 2.0), (7, 5.0), (49, -1.0)], [5, 11, 11, 39, 2]),
        ([(12, 1.0)], [0, 1, 2]),
        ([], [17, 40, 3]),                      # all-zero input; slot 40 is out of range
        ([(i, float(1 + i % 3)) for i in range(0, 50, 4)], [21, 22, 23, 24, 25, 26]),
    ]
    for sparse, slots in inputs:
        x = np.zeros(in_dim, np.float32)
        for i, v in sparse:
            x[i] = v
        win, margin, policy = forward(net, x, slots)
        cases.append({"features": sparse, "slots": slots, "win": win, "margin": margin, "policy": policy})
    ref = {"in_dim": in_dim, "hidden": hidden, "vocab": vocab, "seed": seed, "cases": cases}
    Path(str(prefix) + ".json").write_text(json.dumps(ref, indent=1) + "\n")
    return ref


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--make-fixture":
        ref = make_fixture(sys.argv[2])
        print(f"wrote {sys.argv[2]}.rbpn and .json ({len(ref['cases'])} cases)")
    else:
        print(__doc__)
