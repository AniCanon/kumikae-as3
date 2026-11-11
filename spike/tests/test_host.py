"""AS3-shaped classes run as Godot scripts: callbacks, signals, uncaught errors, missing methods."""
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import godot_bin  # noqa: E402


class HostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        godot_bin.ensure_imported()
        cls.result = subprocess.run(
            [godot_bin.godot_path(), "--headless", "--fixed-fps", "60", "--quit-after", "600", "--path", godot_bin.PROJECT,
             "res://tests/host_test.tscn"], capture_output=True, text=True, timeout=120)

    def test_exits_cleanly(self):
        self.assertEqual(self.result.returncode, 0, self.result.stderr)

    def test_engine_callbacks_reach_the_class(self):
        self.assertIn("count=5", self.result.stdout)

    def test_signal_round_trip(self):
        self.assertIn("pinged=105", self.result.stdout)

    def test_uncaught_error_prints_as3_trace_once(self):
        err = self.result.stderr
        self.assertEqual(err.count("Error: boom"), 1, err)
        self.assertEqual(err.count("\tat Probe/explode() [Probe.as:12]"), 1, err)
        self.assertLess(err.index("Error: boom"), err.index("\tat Probe/explode() [Probe.as:12]"))

    def test_keeps_running_after_error(self):
        self.assertIn("alive", self.result.stdout)

    def test_missing_method_does_not_crash(self):
        self.assertIn("survived", self.result.stdout)


if __name__ == "__main__":
    unittest.main()
