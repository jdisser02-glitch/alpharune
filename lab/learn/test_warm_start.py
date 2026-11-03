#!/usr/bin/env python3
"""A candidate started from the current net (train_policy.py --init-value,
loop.py --warm) is that net until it learns: trained with a zero learning
rate, it gives the same hidden layer and win logit on inputs whose features
the new data has seen (2026-11-03: the re-fold divided by the new scale
instead of converting between the two scales, and a warm start began at a
validation loss of 6.4).

    RIFTBOUND_ROOT=. python3 lab/learn/test_warm_start.py      (needs build/riftbound)
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from rbpn import read_rbpn  # noqa: E402
import loop  # noqa: E402

ENGINE = Path(os.environ.get("RIFTBOUND_BIN", ROOT / "build" / "riftbound"))
NET = ROOT / "lab" / "learn" / "nets" / "net_128.rbpn"


@unittest.skipUnless(ENGINE.exists() and NET.exists(), "needs build/riftbound and lab/learn/nets/net_128.rbpn")
class WarmStart(unittest.TestCase):
    def test_zero_learning_rate_keeps_the_net(self):
        with tempfile.TemporaryDirectory() as d:
            data = Path(d) / "d.rbvd"
            agent = f"pmcts:sims=4,eval=policy:{NET},worlds=2"
            subprocess.run([str(ENGINE), "--deck1", "decks/riven_sacramento.txt", "--deck2", "decks/riven_sacramento.txt",
                            "--agent1", agent, "--agent2", agent, "--games", "2", "--threads", "2", "--seed", "3",
                            "--render-html", "off", "--web", "off", "--value-data", str(data), "--value-samples", "0"],
                           cwd=ROOT, check=True, capture_output=True)
            out = Path(d) / "same.rbpn"
            subprocess.run([sys.executable, str(HERE / "train_policy.py"), str(out), str(data), "--epochs", "1", "--lr", "0",
                            "--hidden", str(loop.rbpn_hidden(NET)), "--init-value", str(NET)],
                           cwd=ROOT, check=True, capture_output=True)
            a, b = read_rbpn(NET), read_rbpn(out)
        live = np.where((a["inv_std"] > 0) & (b["inv_std"] > 0))[0]
        self.assertGreater(len(live), 50)
        rng = np.random.default_rng(0)
        x = np.zeros((40, a["W1"].shape[1]), np.float32)     # features the new data never saw are 0
        for r in x:
            idx = rng.choice(live, 40, replace=False)
            r[idx] = np.abs(a["mean"][idx] + rng.standard_normal(40) / np.maximum(a["inv_std"][idx], 1e-6) * 0.5)

        def hidden(n):
            return np.maximum(n["b1"] + ((x - n["mean"]) * n["inv_std"]) @ n["W1"].T, 0)
        self.assertLess(float(np.abs(hidden(a) - hidden(b)).max()), 1e-3)
        np.testing.assert_allclose(hidden(a) @ a["w2"] + a["b2"], hidden(b) @ b["w2"] + b["b2"], atol=1e-3)

    def test_the_width_is_read_from_the_file(self):
        self.assertEqual(loop.rbpn_hidden(NET), 64)


if __name__ == "__main__":
    unittest.main()
