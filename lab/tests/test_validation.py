import json
import random
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import validation
import riftlab
import ga

OPPS = [{"label": "opponent", "deck": "opp.txt", "weight": 1}]


def rows(wins, games):
    return {f"opponent:{seat}": {"wins": wins, "games": games} for seat in (1, 2)}


class ValidationTests(unittest.TestCase):
    def test_tie_not_recommended(self):
        r = validation.compare({"base": rows(500, 1000), "a": rows(500, 1000)}, "base", OPPS)
        self.assertLess(r["a"]["delta_interval"][0], 0)

    def test_clear_gain_and_multiple_comparisons(self):
        data = {"base": rows(300, 1000), "a": rows(800, 1000)}
        first = validation.compare(data, "base", OPPS)["a"]
        self.assertGreater(first["delta_interval"][0], 0)
        data["b"] = rows(700, 1000)
        self.assertLess(validation.compare(data, "base", OPPS)["a"]["delta_interval"][0],
                        first["delta_interval"][0])

    def test_small_lucky_sample_inconclusive(self):
        r = validation.compare({"base": rows(1, 2), "a": rows(2, 2)}, "base", OPPS)
        self.assertLess(r["a"]["delta_interval"][0], 0)

    def test_rng_exact_roundtrip(self):
        a = random.Random(41)
        a.random()
        b = random.Random()
        b.setstate(validation.as_tuple(json.loads(json.dumps(a.getstate()))))
        self.assertEqual([a.random() for _ in range(20)], [b.random() for _ in range(20)])

    def test_duplicate_opponents_rejected(self):
        with self.assertRaises(ValueError):
            validation.check_opponents(OPPS * 2)

    def test_missing_output_rejected(self):
        from subprocess import CompletedProcess
        with patch.object(riftlab.subprocess, "run", return_value=CompletedProcess([], 0, "ok", "")):
            with self.assertRaises(RuntimeError):
                riftlab.play_batch("a", "b", "random", "random", 4, 1, 1)

    def test_incomplete_batch_rejected(self):
        from subprocess import CompletedProcess
        with patch.object(riftlab.subprocess, "run", return_value=CompletedProcess(
                [], 0, "P1 wins: 1\nP2 wins: 1\nDraws: 0", "")):
            with self.assertRaises(RuntimeError):
                riftlab.play_batch("a", "b", "random", "random", 4, 1, 1)

    def test_file_content_changes_identity(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "net"
            p.write_bytes(b"old")
            old = validation.digest(p)
            p.write_bytes(b"new")
            self.assertNotEqual(old, validation.digest(p))

    def test_legacy_resume_rejected_before_loading_scores(self):
        search = ga.GA.__new__(ga.GA)
        search.spec = {"population": 4, "generations": 2}
        search.agent = search.opp_agent = "random"
        search.opponents = OPPS
        with patch.object(validation, "fingerprint", return_value={"new": True}):
            with self.assertRaisesRegex(ValueError, "Resume rejected"):
                search.run({"archive": {"untrusted": {}}})

    def test_changed_network_rejects_resume(self):
        search = ga.GA.__new__(ga.GA)
        search.spec = {"population": 4, "generations": 2}
        search.agent = search.opp_agent = "random"
        search.opponents = OPPS
        with patch.object(validation, "fingerprint", return_value={"network": "new"}):
            with self.assertRaisesRegex(ValueError, "Resume rejected"):
                search.run({"provenance": {"network": "old"}})

    def test_interrupted_search_matches_uninterrupted(self):
        import collections
        import copy
        import datetime as dt

        class Search(ga.GA):
            def __init__(self):
                self.spec = {"population": 4, "elite": 2, "generations": 3, "games": 4,
                             "name": "test", "seed": 7}
                self.rng = random.Random(7)
                self.base = {"maindeck": {"basecard": 39}}
                self.pool = ["basecard"]
                self.agent = self.opp_agent = "random"
                self.opponents = OPPS
                self.archive, self.log = {}, []
                self.started = dt.datetime.now(dt.timezone.utc)
                self.test_id, self.dry, self.confirmation = "test", False, None
                self.snapshots = []
                self.stop = False

            def register(self, main, gen):
                k = ga.key_of(main)
                self.archive.setdefault(k, {"main": main, "path": k, "label": k,
                    "results": {"opponent": {"wins": 0, "games": 0}}, "generations": []})
                return k

            def mutate(self, main):
                return {"card" + str(self.rng.randrange(100000)): 39}

            def crossover(self, a, b):
                return dict(a)

            def legal(self, main):
                return True

            def evaluate(self, keys, gen, games):
                for k in keys:
                    r = self.archive[k]["results"]["opponent"]
                    r["wins"] += int(k[0], 16) % 4
                    r["games"] += 4

            def write(self, population, complete):
                state = copy.deepcopy({"archive": self.archive, "log": self.log,
                    "test_id": self.test_id, "started": self.started.isoformat(),
                    "population": population, "rng": self.rng.getstate(),
                    "provenance": self.provenance, "confirmation": self.confirmation})
                self.snapshots.append(state)
                if self.stop:
                    raise InterruptedError()

        with patch.object(validation, "fingerprint", return_value={"fixed": True}), patch.object(
                validation, "confirm", return_value={"recommended": None}):
            full = Search()
            full.run()
            interrupted = Search()
            interrupted.stop = True
            with self.assertRaises(InterruptedError):
                interrupted.run()
            state = json.loads(json.dumps(interrupted.snapshots[0]))
            resumed = Search()
            with patch.object(riftlab, "parse_deck", side_effect=lambda p: {
                "maindeck": state["archive"][p.name]["main"]}):
                resumed.run(state)
            self.assertEqual(full.log, resumed.log)
            self.assertEqual(full.archive, resumed.archive)
            self.assertEqual(full.rng.getstate(), resumed.rng.getstate())

    def test_confirmation_excludes_eliminated_lucky_candidate(self):
        from types import SimpleNamespace
        base = ga.key_of({"basecard": 39})
        archive = {k: {"path": k} for k in (base, "survivor", "lucky")}
        search = SimpleNamespace(spec={"seed": 1, "generations": 2, "confirmation": {"games": 4}},
            base={"maindeck": {"basecard": 39}}, archive=archive,
            opponents=OPPS, agent="random", opp_agent="random", provenance={"test": 1},
            rate=lambda k: {"survivor": .5, "lucky": 1}[k])
        calls = []
        def batch(*args):
            calls.append(args)
            return {"p1": 1, "p2": 1, "draws": 0}
        with patch.object(riftlab, "play_batch", side_effect=batch), patch.object(
                validation, "fingerprint", return_value=search.provenance):
            result = validation.confirm(search, [base, "survivor"])
        self.assertNotIn("lucky", result["results"])
        self.assertEqual(result["status"], "inconclusive")
        self.assertTrue(all(args[-1] > 2000007 for args in calls))


if __name__ == "__main__":
    unittest.main()
