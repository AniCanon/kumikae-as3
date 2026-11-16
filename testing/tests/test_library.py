"""Run the real AS3 lifecycle fixture when an existing compiler is supplied."""
import importlib.util
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMPILER = os.environ.get("KAS3C", "")
spec = importlib.util.spec_from_file_location("protocol", ROOT / "protocol.py")
protocol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)


@unittest.skipUnless(COMPILER, "Set KAS3C to an existing compiler")
class LibraryTest(unittest.TestCase):
    def test_real_interpreter_preserves_lifecycle_and_failure_events(self):
        sources = sorted((ROOT / "as3").rglob("*.as"))
        command = [COMPILER, "run", str(ROOT / "tests/fixtures/LibraryTest.as"),
                   str(ROOT / "tests/fixtures/ProbeCase.as"), *map(str, sources)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS framework lifecycle", result.stdout.splitlines())
        events = protocol.parse(result.stdout)
        self.assertEqual(sum(kind == "testStarted" for kind, _, _ in events), 6)
        self.assertEqual(sum(kind == "testFailed" for kind, _, _ in events), 3)

    def test_failure_keeps_assertion_and_hook_lines(self):
        command = [COMPILER, "run", str(ROOT / "tests/fixtures/LibraryTest.as"),
                   str(ROOT / "tests/fixtures/ProbeCase.as"), *map(str, sorted((ROOT / "as3").rglob("*.as")))]
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        failures = {name: details for kind, name, details in protocol.parse(result.stdout) if kind == "testFailed"}
        for name, statement in [("failure", 'Assert.assertTrue(!fail'),
                                ("setup failure", 'throw new Error("Setup failed")'),
                                ("cleanup failure", 'throw new Error("Cleanup failed")')]:
            lines = (ROOT / "tests/fixtures/ProbeCase.as").read_text().splitlines()
            line = next(i for i, text in enumerate(lines, 1) if statement in text)
            self.assertIn("ProbeCase.as:" + str(line) + "]", failures[name])
