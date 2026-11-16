import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("protocol", Path(__file__).resolve().parents[1] / "protocol.py")
protocol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)


def event(kind, name, text=""):
    return f"@kas3-test-v1 {kind} {len(name)} {len(text)}\n{name}\n{text}\n"


class ProtocolTest(unittest.TestCase):
    def test_multiline_and_punctuation_round_trip(self):
        name = "quoted 'λ"
        data = event("suiteStarted", "suite") + event("testStarted", name) + event("testFailed", name, "x\ny")
        data += event("testFinished", name) + event("suiteFinished", "suite")
        events = protocol.parse(data)
        self.assertEqual(events[2], ("testFailed", name, "x\ny"))

    def test_incomplete_and_out_of_order_reports_fail(self):
        for data in [event("testFinished", "orphan"), event("suiteStarted", "suite"), "@kas3-test-v1 malformed\n"]:
            with self.assertRaises(ValueError):
                protocol.parse(data)

    def test_names_cannot_break_case_selection(self):
        for name in ["line\nbreak", "reserved::separator"]:
            with self.assertRaises(ValueError):
                protocol.parse(event("suiteStarted", name) + event("testStarted", "case") + event("testFinished", "case") + event("suiteFinished", name))
