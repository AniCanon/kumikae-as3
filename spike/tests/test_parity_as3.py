"""The AS3-shaped C++ benchmarks do the same work as the GDScript ones."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import godot_bin  # noqa: E402
import run_bench  # noqa: E402


class ParityAs3Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        godot_bin.ensure_imported()

    def test_parity_with_gdscript(self):
        for bench in run_bench.ORIGINAL:
            with self.subTest(bench=bench):
                run_bench.parity(bench, ["gd", "as3"])


if __name__ == "__main__":
    unittest.main()
