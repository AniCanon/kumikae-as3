from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import unittest


class SimulatorReportTest(unittest.TestCase):
    def test_only_this_launch_can_finish_the_suite(self):
        spec = spec_from_file_location("reports", Path(__file__).resolve().parents[1] / "simulator_report.py")
        reports = module_from_spec(spec)
        spec.loader.exec_module(reports)
        self.assertFalse(reports.finished({"version": 1, "run": "old", "passed": True}, "new"))
        self.assertTrue(reports.finished({"version": 1, "run": "new", "passed": True, "checks": 103}, "new"))
        with self.assertRaises(ValueError):
            reports.finished({"version": 1, "run": "new", "passed": False}, "new")
        for count in [None, True, -1, 0]:
            with self.assertRaises(ValueError):
                reports.finished({"version": 1, "run": "new", "passed": True, "checks": count}, "new")
