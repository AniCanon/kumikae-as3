"""The gate's pass/fail rules, on synthetic rows."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gate  # noqa: E402

GAMEPLAY = ["move_nodes", "per_node_process", "ray_queries", "signal_traffic"]
COMPUTE = ["nbody", "sieve"]


def rows(overrides=None, runs=None):
    """Five runs per bench and language: AS3 1000, C# 1200 on gameplay; equal on compute."""
    table = {}
    for bench in GAMEPLAY:
        table[bench] = {"as3": [1000] * 5, "cs": [1200] * 5, "gd": [3000] * 5}
    for bench in COMPUTE + ["array_conversion", "empty"]:
        table[bench] = {"as3": [1000] * 5, "cs": [1000] * 5, "gd": [9000] * 5}
    for (bench, lang), value in (overrides or {}).items():
        table[bench][lang] = [value] * 5
    for (bench, lang), values in (runs or {}).items():
        table[bench][lang] = values
    out = []
    for bench, langs in table.items():
        for lang, values in langs.items():
            for value in values:
                out.append({"bench": bench, "lang": lang, "median_us": value, "p99_us": value, "max_us": value})
    return out


class GateTest(unittest.TestCase):
    def test_clear_win_passes(self):
        verdict = gate.verdict(rows())
        self.assertTrue(verdict["pass"])
        self.assertAlmostEqual(verdict["benches"]["move_nodes"]["ratio_cs_over_as3"], 1.2)
        self.assertEqual(verdict["benches"]["move_nodes"]["group"], "gameplay")

    def test_nine_percent_fails(self):
        verdict = gate.verdict(rows({("move_nodes", "cs"): 1090}))
        self.assertFalse(verdict["benches"]["move_nodes"]["pass"])
        self.assertFalse(verdict["pass"])

    def test_exactly_ten_percent_passes(self):
        self.assertTrue(gate.verdict(rows({("move_nodes", "cs"): 1100}))["pass"])

    def test_overlap_fails(self):
        verdict = gate.verdict(rows(runs={
            ("move_nodes", "as3"): [1000, 1000, 1000, 1000, 1250],
            ("move_nodes", "cs"): [1200, 1200, 1200, 1200, 1150],
        }))
        self.assertFalse(verdict["benches"]["move_nodes"]["pass"])
        self.assertFalse(verdict["pass"])

    def test_compute_within_noise_passes(self):
        self.assertTrue(gate.verdict(rows({("nbody", "as3"): 1020}))["pass"])

    def test_compute_beyond_noise_fails(self):
        verdict = gate.verdict(rows({("nbody", "as3"): 1040}))
        self.assertFalse(verdict["benches"]["nbody"]["pass"])
        self.assertFalse(verdict["pass"])

    def test_conversion_not_judged(self):
        verdict = gate.verdict(rows({("array_conversion", "as3"): 10000}))
        self.assertTrue(verdict["pass"])
        self.assertIsNone(verdict["benches"]["array_conversion"]["pass"])

    def test_missing_csharp_is_not_a_pass(self):
        without_cs = [row for row in rows() if row["lang"] != "cs"]
        verdict = gate.verdict(without_cs)
        self.assertFalse(verdict["pass"])
        self.assertIsNone(verdict["benches"]["move_nodes"]["cs_us"])


if __name__ == "__main__":
    unittest.main()
