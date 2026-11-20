"""A log may arrive late, omit a trailing newline, or never receive a completion marker."""
import importlib.util
import io
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location("simulator_log", Path(__file__).resolve().parents[1] / "simulator_log.py")
logs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(logs)


class SimulatorLogTest(unittest.TestCase):
    def test_final_line_without_newline(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "log"
            path.write_bytes(b"output\nDONE")
            process = Mock(returncode=0)
            process.poll.return_value = 0
            with patch.object(logs.sys, "stdout", Mock(buffer=io.BytesIO())):
                self.assertEqual(0, logs.follow(path, process, b"DONE", time.monotonic() + 1))

    def test_missing_log_times_out_and_empty_marker_is_invalid(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "missing"
            process = Mock()
            process.poll.return_value = None
            with self.assertRaisesRegex(ValueError, "timed out"):
                logs.follow(path, process, b"DONE", time.monotonic())
            with self.assertRaisesRegex(ValueError, "marker"):
                logs.follow(path, process, b"", time.monotonic() + 1)
