"""End-to-end runner reports use the same case counts and failure reason as the IDE."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


def event(kind, name, details=""):
    return f"@kas3-test-v1 {kind} {len(name)} {len(details)}\n{name}\n{details}\n"


class ResultReportTest(unittest.TestCase):
    def test_counts_leaves_and_shows_diagnostic_when_target_cannot_run(self):
        cases = event("suiteStarted", "Physics") + event("testStarted", "ok") + event("testFinished", "ok")
        cases += event("testStarted", "broken") + event("testFailed", "broken", "Error: boom")
        cases += event("testFinished", "broken") + event("suiteFinished", "Physics") + "DONE"
        diagnostic = "tests/Score.as:4:7: error: unknown name"
        def target(name, output, **options):
            return dict(name=name, marker="DONE", command=[sys.executable, "-c", "import sys; print(sys.argv[1])",
                                                           output], **options)
        targets = [target("Models", cases, events=True), target("Build", "Compiling\n" + diagnostic, events=True),
                   target("Scene", "Godot Engine\nSCRIPT ERROR: bad type\n at: reload (res://main.gd:5)")]
        with tempfile.TemporaryDirectory() as folder:
            manifest = Path(folder) / "kas3-tests.json"
            manifest.write_text(json.dumps(dict(version=1, tests=targets)))
            result = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "runner.py"),
                                     str(manifest)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("AS3 tests: 1 passed, 3 failed", result.stdout)
        self.assertIn("message='" + diagnostic + "'", result.stdout)
        self.assertIn("message='SCRIPT ERROR: bad type'", result.stdout)

    def test_surfaces_typed_runtime_error_and_handles_empty_output(self):
        runtime_error = "TypeError: Error #1009: null object reference"
        targets = [
            dict(
                name="Crash",
                marker="DONE",
                command=[sys.executable, "-c", "import sys; print(sys.argv[1]); raise SystemExit(1)",
                         "Running\n" + runtime_error + "\n\tat Main/run()[Main.as:5]"],
            ),
            dict(
                name="Silent",
                marker="DONE",
                command=[sys.executable, "-c", "raise SystemExit(1)"],
            ),
        ]
        with tempfile.TemporaryDirectory() as folder:
            manifest = Path(folder) / "kas3-tests.json"
            manifest.write_text(json.dumps(dict(version=1, tests=targets)))
            result = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "runner.py"),
                                     str(manifest)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("AS3 tests: 0 passed, 2 failed", result.stdout)
        self.assertIn("message='" + runtime_error + "'", result.stdout)
        self.assertIn("message='Missing completion marker: DONE'", result.stdout)

    def test_streams_events_before_process_exits(self):
        script = (
            "import sys, time\n"
            "def emit(kind, name):\n"
            "    print(f'@kas3-test-v1 {kind} {len(name)} 0\\n{name}\\n', flush=True)\n"
            "emit('suiteStarted', 'Live')\n"
            "emit('testStarted', 'fast')\n"
            "emit('testFinished', 'fast')\n"
            "time.sleep(0.4)\n"
            "emit('testStarted', 'slow')\n"
            "emit('testFinished', 'slow')\n"
            "emit('suiteFinished', 'Live')\n"
            "print('DONE', flush=True)\n"
        )
        targets = [dict(name="LiveTarget", marker="DONE", events=True, command=[sys.executable, "-u", "-c", script])]
        with tempfile.TemporaryDirectory() as folder:
            manifest = Path(folder) / "kas3-tests.json"
            manifest.write_text(json.dumps(dict(version=1, tests=targets)))
            with subprocess.Popen(
                [sys.executable, "-u", str(Path(__file__).resolve().parents[1] / "runner.py"), str(manifest)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            ) as proc:
                saw_fast_while_running = False
                lines = []
                for line in proc.stdout:
                    lines.append(line)
                    if "testFinished name='fast'" in line and proc.poll() is None:
                        saw_fast_while_running = True
                proc.wait(timeout=5)
                returncode = proc.returncode
        self.assertEqual(returncode, 0)
        self.assertTrue(saw_fast_while_running, "Expected 'fast' testFinished to stream before runner exited")
        self.assertIn("AS3 tests: 2 passed, 0 failed", "".join(lines))
