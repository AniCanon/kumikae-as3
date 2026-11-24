import subprocess
import sys
import unittest
from pathlib import Path

from by_name_gate import failures, batch_sizes, swap_placement, short_pairs, layout_mean


class GateTests(unittest.TestCase):
    def test_placement_swaps_whole_methods_and_only_short_pairs(self):
        source = ("\t\tpublic function timerRead():int { return 1; }\n"
                  "\t\tprivate function unrelated():void {}\n"
                  "\t\tpublic function timerTypedRead():int { return 2; }\n")
        self.assertEqual(swap_placement(source, [("timerRead", "timerTypedRead")]), (
            "\t\tpublic function timerTypedRead():int { return 2; }\n"
            "\t\tprivate function unrelated():void {}\n"
            "\t\tpublic function timerRead():int { return 1; }\n"))
        self.assertEqual(short_pairs({"timer/read": [0.04], "timerTyped/read": [0.05],
                                      "proven/read": [1.01], "typed/read": [1.02]}),
                         [("timer/read", "timerTyped/read")])

    def test_batch_sizes_use_fastest_mode_and_match_controls(self):
        pilots = {"timer/read": [0.04, 0.50], "timerTyped/read": [0.02, 0.49],
                  "node/write": [1.2, 2.0], "foreign/call": [None, 2.5]}
        rounds = batch_sizes(pilots)
        self.assertGreaterEqual(rounds["timer/read"] * 0.02 / 20000, 1.0)
        self.assertEqual(rounds["timer/read"], rounds["timerTyped/read"])
        self.assertEqual(rounds["node/write"], 20000)
        self.assertEqual(rounds["foreign/call"], 20000)

    def test_short_pair_keeps_a_five_percent_cost_across_layouts(self):
        pilots = {"timer/read": [0.021], "timerTyped/read": [0.020]}
        counts = batch_sizes(pilots)
        self.assertEqual(short_pairs(pilots), [("timer/read", "timerTyped/read")])
        times = {}
        for row, (cost,) in pilots.items():
            batches = [cost * counts[row] / 20000 * factor for factor in (1.1, 0.9)]
            self.assertGreater(min(batches), 0.5)
            cost = layout_mean(*batches) * 20000 / counts[row]
            times[row] = {"native": [cost] * 7}
        self.assertEqual(failures(times, None), [
            "timer/read: native slower than typed (0.021 vs 0.020 ms, tolerance 3%)"])

    def test_native_uses_medians(self):
        times = {"node/read": {"native": [1, 12, 13], "interpreter": [9, 10, 20]}}
        self.assertEqual(failures(times, None), [
            "node/read: native slower than interpreter (12.000 vs 10.000 ms, tolerance 3%)"])

    def test_tolerance_includes_the_boundary(self):
        self.assertEqual(failures({"node/read": {"native": [103], "interpreter": [100]}}, None), [])

    def test_against_main(self):
        times = {"alias/write": {"native": [12], "interpreter": [12]}}
        main = {"alias/write": {"native": [10], "interpreter": [10]}}
        self.assertEqual(failures(times, main), [
            f"alias/write: {mode} slower than main (12.000 vs 10.000 ms, tolerance 3%)"
            for mode in ("interpreter", "native")])

    def test_against_managed(self):
        times = {"alias/write": {"native": [12], "interpreter": [12], "C#": [10]}}
        self.assertEqual(failures(times, None), [
            f"alias/write: {mode} slower than C# (12.000 vs 10.000 ms, tolerance 3%)"
            for mode in ("interpreter", "native")])

    def test_against_typed(self):
        for inferred, typed in (("proven", "typed"), ("number", "numberTyped")):
            with self.subTest(inferred=inferred):
                times = {inferred + "/read": {"native": [12], "interpreter": [12]},
                         typed + "/read": {"native": [10], "interpreter": [10]}}
                self.assertEqual(failures(times, None), [
                    f"{inferred}/read: {mode} slower than typed (12.000 vs 10.000 ms, tolerance 3%)"
                    for mode in ("interpreter", "native")])

    def test_gate_requires_against(self):
        script = Path(__file__).with_name("by_name_gate.py")
        run = subprocess.run([sys.executable, script, ".", "--gate"], capture_output=True, text=True)
        self.assertEqual(run.returncode, 2)
        self.assertIn("--gate requires --against", run.stderr)


if __name__ == "__main__":
    unittest.main()
