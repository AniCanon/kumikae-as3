"""Version 2 host behaviour: traces built on the error path, native closures, stale closures."""
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import godot_bin  # noqa: E402


class Host2Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        godot_bin.ensure_imported()
        cls.result = subprocess.run(
            [godot_bin.godot_path(), "--headless", "--fixed-fps", "60", "--quit-after", "600", "--path",
             godot_bin.PROJECT, "res://tests/host_test2.tscn"], capture_output=True, text=True, timeout=120)

    def test_exits_cleanly(self):
        self.assertEqual(self.result.returncode, 0, self.result.stderr)

    def test_error_in_signal_handler_does_not_stop_the_emitter(self):
        # The handler ran (state 1), threw, and the emitter carried on to its next statement (state 2).
        self.assertIn("after_fire=2", self.result.stdout)

    def test_handler_error_is_reported_once_with_its_site(self):
        err = self.result.stderr
        self.assertEqual(err.count("Error: in handler"), 1, err)
        self.assertEqual(err.count("\tat V2Probe/on_boom() [V2Probe.as:12]"), 1, err)

    def test_trace_lists_every_frame_the_error_passed_through(self):
        err = self.result.stderr
        self.assertIn("TypeError: Error #1009", err)
        self.assertIn("\tat V2Probe/leaf() [V2Probe.as:16]\n\tat V2Probe/middle() [V2Probe.as:15]\n"
                      "\tat V2Probe/chain() [V2Probe.as:14]", err)

    def test_keeps_running_after_errors(self):
        self.assertIn("alive", self.result.stdout)

    def test_closure_calls_the_method_directly(self):
        self.assertIn("closure_valid=true", self.result.stdout)
        self.assertIn("called=12", self.result.stdout)

    def test_closure_is_invalid_once_its_object_is_freed(self):
        self.assertIn("valid_after_free=false", self.result.stdout)


if __name__ == "__main__":
    unittest.main()
