"""The version 2 gate's rules, on synthetic rows."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gate  # noqa: E402
import run_bench  # noqa: E402


def rows(overrides=None, runs=None, drop=()):
    """Five runs per bench and language. By default version 2 is 1000, C# 1100, the floor 990."""
    table = {}
    for bench in run_bench.BENCHES:
        table[bench] = {"as3v2": [1000] * 5, "as3": [1300] * 5, "cs": [1100] * 5, "gd": [9000] * 5,
                        "floor": [990] * 5}
    for (bench, lang), value in (overrides or {}).items():
        table[bench][lang] = [value] * 5
    for (bench, lang), values in (runs or {}).items():
        table[bench][lang] = values
    out = []
    for bench, langs in table.items():
        for lang, values in langs.items():
            if (bench, lang) in drop or lang in drop:
                continue
            for value in values:
                out.append({"bench": bench, "lang": lang, "median_us": value, "p99_us": value, "max_us": value})
    return out


class GateTest(unittest.TestCase):
    def test_ahead_everywhere_passes(self):
        verdict = gate.verdict(rows())
        self.assertTrue(verdict["pass"])
        self.assertAlmostEqual(verdict["benches"]["move_nodes"]["ratio_cs_over_as3"], 1.1)

    def test_judges_exactly_the_nine_including_holdouts(self):
        verdict = gate.verdict(rows())
        judged = sorted(name for name, entry in verdict["benches"].items() if entry["pass"] is not None)
        self.assertEqual(judged, sorted(run_bench.JUDGED))
        self.assertTrue(verdict["benches"]["alloc"]["holdout"])
        self.assertFalse(verdict["benches"]["sieve"]["holdout"])

    def test_small_lead_passes_when_runs_do_not_overlap(self):
        self.assertTrue(gate.verdict(rows({("move_nodes", "cs"): 1020}))["pass"])

    def test_behind_fails(self):
        verdict = gate.verdict(rows({("sieve", "cs"): 990}))
        self.assertFalse(verdict["benches"]["sieve"]["pass"])
        self.assertFalse(verdict["pass"])

    def test_tie_fails(self):
        self.assertFalse(gate.verdict(rows({("nbody", "cs"): 1000}))["pass"])

    def test_overlapping_runs_fail(self):
        verdict = gate.verdict(rows(runs={
            ("move_nodes", "as3v2"): [1000, 1000, 1000, 1000, 1095],
            ("move_nodes", "cs"): [1100, 1100, 1100, 1100, 1090],
        }))
        self.assertFalse(verdict["benches"]["move_nodes"]["pass"])
        self.assertFalse(verdict["pass"])

    def test_holdout_failure_fails_the_gate(self):
        verdict = gate.verdict(rows({("alloc", "cs"): 900}))
        self.assertFalse(verdict["pass"])

    def test_unjudged_benchmarks_do_not_count(self):
        verdict = gate.verdict(rows({("array_conversion", "as3v2"): 99999}))
        self.assertTrue(verdict["pass"])
        self.assertIsNone(verdict["benches"]["array_conversion"]["pass"])

    def test_missing_csharp_is_not_a_pass(self):
        verdict = gate.verdict(rows(drop=("cs",)))
        self.assertFalse(verdict["pass"])
        self.assertIsNone(verdict["benches"]["move_nodes"]["cs_us"])

    def test_faster_than_floor_is_flagged_but_not_failed(self):
        verdict = gate.verdict(rows({("nbody", "floor"): 1100}))
        self.assertTrue(verdict["benches"]["nbody"]["below_floor"])
        self.assertFalse(verdict["benches"]["sieve"]["below_floor"])
        self.assertTrue(verdict["pass"])

    def test_within_three_percent_of_floor_is_not_flagged(self):
        self.assertFalse(gate.verdict(rows({("nbody", "floor"): 1025}))["benches"]["nbody"]["below_floor"])

    def test_language_order_rotates_each_repetition(self):
        langs = ["gd", "cs", "as3", "as3v2", "floor"]
        orders = [run_bench.rotated(langs, repetition) for repetition in range(5)]
        self.assertEqual(orders[0], langs)
        self.assertEqual(orders[1], ["cs", "as3", "as3v2", "floor", "gd"])
        self.assertEqual(len({tuple(order) for order in orders}), 5)

    def test_calibration_drift(self):
        self.assertFalse(gate.drifted(100.0, 104.9))
        self.assertTrue(gate.drifted(100.0, 105.1))
        self.assertTrue(gate.drifted(100.0, 94.0))


if __name__ == "__main__":
    unittest.main()
