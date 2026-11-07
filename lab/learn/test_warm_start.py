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
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import rbpn  # noqa: E402
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

    def test_widening_keeps_the_net(self):
        """A 64-unit net widened to 128 (net2net, the specialists' start) gives the same outputs at a zero learning rate."""
        with tempfile.TemporaryDirectory() as d:
            data = Path(d) / "d.rbvd"
            agent = f"pmcts:sims=4,eval=policy:{NET},worlds=2"
            subprocess.run([str(ENGINE), "--deck1", "decks/riven_sacramento.txt", "--deck2", "decks/riven_sacramento.txt",
                            "--agent1", agent, "--agent2", agent, "--games", "2", "--threads", "2", "--seed", "5",
                            "--render-html", "off", "--web", "off", "--value-data", str(data), "--value-samples", "0"],
                           cwd=ROOT, check=True, capture_output=True)
            out = Path(d) / "wide.rbpn"
            r = subprocess.run([sys.executable, str(HERE / "train_policy.py"), str(out), str(data), "--epochs", "1", "--lr", "0",
                                "--hidden", "128", "--init-value", str(NET)], cwd=ROOT, check=True, capture_output=True, text=True)
            self.assertIn("widening", r.stdout)
            a, b = read_rbpn(NET), read_rbpn(out)
        self.assertEqual(b["W1"].shape[0], 128)
        live = np.where((a["inv_std"] > 0) & (b["inv_std"] > 0))[0]
        rng = np.random.default_rng(1)
        x = np.zeros((40, a["W1"].shape[1]), np.float32)
        for r_ in x:
            idx = rng.choice(live, 40, replace=False)
            r_[idx] = np.abs(a["mean"][idx] + rng.standard_normal(40) / np.maximum(a["inv_std"][idx], 1e-6) * 0.5)

        def out_of(n):
            h = np.maximum(n["b1"] + ((x - n["mean"]) * n["inv_std"]) @ n["W1"].T, 0)
            return h @ n["w2"] + n["b2"], h @ n["Wp"].T + n["bp"]
        (va, pa), (vb, pb) = out_of(a), out_of(b)
        np.testing.assert_allclose(va, vb, atol=1e-3)
        np.testing.assert_allclose(pa, pb, atol=1e-3)

    def test_the_width_is_read_from_the_file(self):
        self.assertEqual(loop.rbpn_hidden(NET), 64)

    def test_warm_start_from_an_old_size_net_pads_the_policy_head(self):
        """A net from before the MakeChoiceOpponent slots (22971 policy rows) warm-starts a full-size net: the engine's
        data carries the new vocab, and each new enemy-pick row starts as the same card's friendly-pick row."""
        old = read_rbpn(NET, pad=False)
        self.assertEqual(old["Wp"].shape[0], rbpn.VOCAB_SIZE_BEFORE_OPPONENT_PICKS)
        with tempfile.TemporaryDirectory() as d:
            data = Path(d) / "d.rbvd"
            agent = f"pmcts:sims=4,eval=policy:{NET},worlds=2"
            subprocess.run([str(ENGINE), "--deck1", "decks/riven_sacramento.txt", "--deck2", "decks/riven_sacramento.txt",
                            "--agent1", agent, "--agent2", agent, "--games", "2", "--threads", "2", "--seed", "7",
                            "--render-html", "off", "--web", "off", "--value-data", str(data), "--value-samples", "0"],
                           cwd=ROOT, check=True, capture_output=True)
            self.assertEqual(struct.unpack_from("<i", data.read_bytes(), 8)[0], rbpn.VOCAB_SIZE,
                             "rbpn.VOCAB_SIZE must match openspiel::kVocabSize")
            out = Path(d) / "padded.rbpn"
            subprocess.run([sys.executable, str(HERE / "train_policy.py"), str(out), str(data), "--epochs", "1", "--lr", "0",
                            "--hidden", str(loop.rbpn_hidden(NET)), "--init-value", str(NET)],
                           cwd=ROOT, check=True, capture_output=True)
            b = read_rbpn(out, pad=False)
        V0, base, opp = rbpn.VOCAB_SIZE_BEFORE_OPPONENT_PICKS, rbpn.MAKECHOICE_BASE, rbpn.OPPONENT_PICK_BASE
        self.assertEqual(b["Wp"].shape[0], rbpn.VOCAB_SIZE, "new nets are written at the full vocab")
        np.testing.assert_array_equal(b["Wp"][:V0], old["Wp"])
        np.testing.assert_array_equal(b["bp"][:V0], old["bp"])
        np.testing.assert_array_equal(b["Wp"][opp:], old["Wp"][base + 1:base + 1 + rbpn.NUM_CARD_DEFS])
        np.testing.assert_array_equal(b["bp"][opp:], old["bp"][base + 1:base + 1 + rbpn.NUM_CARD_DEFS])


class PadPolicy(unittest.TestCase):
    """rbpn.read_rbpn pads a pre-growth policy head as the C++ loader does (no engine needed)."""

    def _net(self, vocab, hidden=3, in_dim=5, seed=0):
        rng = np.random.default_rng(seed)
        return {"mean": np.zeros(in_dim, np.float32), "inv_std": np.ones(in_dim, np.float32),
                "W1": rng.normal(size=(hidden, in_dim)).astype(np.float32), "b1": np.zeros(hidden, np.float32),
                "w2": np.ones(hidden, np.float32), "b2": np.zeros(1, np.float32),
                "wm": np.ones(hidden, np.float32), "bm": np.zeros(1, np.float32),
                "Wp": rng.normal(size=(vocab, hidden)).astype(np.float32), "bp": rng.normal(size=vocab).astype(np.float32)}

    def test_old_size_head_is_padded_with_copies(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "old.rbpn"
            src = self._net(rbpn.VOCAB_SIZE_BEFORE_OPPONENT_PICKS)
            rbpn.write_rbpn(p, src)
            raw, net = read_rbpn(p, pad=False), read_rbpn(p)
        self.assertEqual(raw["Wp"].shape[0], rbpn.VOCAB_SIZE_BEFORE_OPPONENT_PICKS)
        self.assertEqual(net["Wp"].shape, (rbpn.VOCAB_SIZE, 3))
        np.testing.assert_array_equal(net["Wp"][:len(raw["Wp"])], raw["Wp"])
        for card in (1, 300, rbpn.NUM_CARD_DEFS):
            enemy, friendly = rbpn.OPPONENT_PICK_BASE + card - 1, rbpn.MAKECHOICE_BASE + card
            self.assertEqual(rbpn.legacy_row_of(enemy), friendly)
            np.testing.assert_array_equal(net["Wp"][enemy], raw["Wp"][friendly])
            self.assertEqual(net["bp"][enemy], raw["bp"][friendly])
        # Old slots score as before: the reference forward agrees on them.
        x = np.arange(5, dtype=np.float32)
        slots = [1, rbpn.MAKECHOICE_BASE + 300, 9000, rbpn.VOCAB_SIZE_BEFORE_OPPONENT_PICKS - 1]
        np.testing.assert_allclose(rbpn.forward(net, x, slots)[2], rbpn.forward(raw, x, slots)[2], rtol=1e-6)

    def test_small_and_full_heads_are_left_alone(self):
        for vocab in (40, rbpn.VOCAB_SIZE):
            net = rbpn.pad_policy(self._net(vocab))
            self.assertEqual(net["Wp"].shape[0], vocab)


if __name__ == "__main__":
    unittest.main()
