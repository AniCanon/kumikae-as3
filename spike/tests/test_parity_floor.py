"""The plain-C++ floor versions do the same work as the GDScript benchmarks."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import godot_bin  # noqa: E402
import run_bench  # noqa: E402

FLOOR_BENCHES = ["move_nodes", "per_node_process", "ray_queries", "signal_traffic", "nbody", "sieve"]


class ParityFloorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        godot_bin.ensure_imported()

    def test_parity_with_gdscript(self):
        for bench in FLOOR_BENCHES:
            with self.subTest(bench=bench):
                run_bench.parity(bench, ["gd", "floor"])


if __name__ == "__main__":
    unittest.main()
