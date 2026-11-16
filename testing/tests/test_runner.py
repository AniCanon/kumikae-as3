import importlib.util
import pathlib
import sys
import tempfile
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("kas3_test_runner", ROOT / "runner.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class RunnerTest(unittest.TestCase):
    def test_service_text_is_escaped(self):
        self.assertEqual(runner.escape("a|'\n[]"), "a|||'|n|[|]")

    def test_success_requires_marker_and_zero_exit(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            for command, expected in [
                ([sys.executable, "-c", "print('DONE')"], True),
                ([sys.executable, "-c", "print('nothing')"], False),
                ([sys.executable, "-c", "print('NOT DONE')"], False),
                ([sys.executable, "-c", "print('DONE'); raise SystemExit(1)"], False),
                ([sys.executable, "-c", "import time; time.sleep(5)"], False),
            ]:
                result = runner.run_case(root, {"name": "case", "command": command, "marker": "DONE", "timeout": 0.1})
                self.assertEqual(result.passed, expected, result.output)

    def test_failure_summary_is_one_line_of_the_details(self):
        # The details may be a whole stack trace: the tree's one line is its first, the rest stays in details.
        self.assertEqual(runner.summary("Error: boom\n\tat Case/run()[tests/Case.as:3]\n\tat Suite/run()[Suite.as:9]"), "Error: boom")
        self.assertEqual(runner.summary("\n  expected 2, got 3  \nmore"), "expected 2, got 3")
        self.assertEqual(runner.summary("plain"), "plain")
        self.assertEqual(runner.summary(""), "Assertion failed")
        self.assertEqual(runner.summary(" \n\t\n"), "Assertion failed")

    def test_manifest_rejects_duplicate_names_and_empty_suite(self):
        for value in [{"version": 1, "tests": []}, {"version": 1, "tests": [{"name": "x"}, {"name": "x"}]}]:
            with self.assertRaises(ValueError):
                runner.validate(value)

    def test_case_filter_cannot_silently_run_nothing(self):
        with self.assertRaises(ValueError):
            runner.select([{"name": "one"}], ["missing"])

    def test_timeout_includes_children_after_parent_exits(self):
        with tempfile.TemporaryDirectory() as folder:
            code = "import subprocess,sys; subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(2)'])"
            start = time.monotonic()
            result = runner.run_case(pathlib.Path(folder), {"command": [sys.executable, "-c", code], "marker": "DONE", "timeout": 0.3})
            self.assertFalse(result.passed)
            self.assertLess(time.monotonic() - start, 1.5)

    def test_ordinary_output_is_not_a_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            result = runner.run_case(pathlib.Path(folder), {
                "command": [sys.executable, "-c", "print('inputs fail gracefully'); print('DONE')"],
                "marker": "DONE",
            })
            self.assertTrue(result.passed)

    def test_selecting_parent_keeps_all_cases(self):
        tests = [{"name": "suite", "events": True, "entryClass": "Tests"}]
        self.assertEqual(runner.select(tests, ["suite", "suite::group::case"])[0]["_filters"], [])
        self.assertEqual(runner.select(tests, ["suite::group::case", "suite"])[0]["_filters"], [])

    def test_every_requested_case_must_run(self):
        output = "@kas3-test-v1 suiteStarted 1 0\ng\n\n@kas3-test-v1 testStarted 1 0\na\n\n@kas3-test-v1 testFinished 1 0\na\n\n@kas3-test-v1 suiteFinished 1 0\ng\n\nDONE"
        with tempfile.TemporaryDirectory() as folder:
            result = runner.run_case(pathlib.Path(folder), {
                "command": [sys.executable, "-c", "import sys; print(sys.argv[1])", output],
                "marker": "DONE", "events": True, "_filters": ["g::a", "g::missing"],
            })
            self.assertFalse(result.passed)

    def test_manifest_names_reserve_case_separator(self):
        with self.assertRaises(ValueError):
            runner.validate({"version": 1, "tests": [{"name": "Platform::Desktop", "command": ["true"], "marker": "DONE"}]})

    def test_globs_do_not_repeat_the_explicit_entry(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            (root / "tests").mkdir()
            entry = root / "tests/Suite.as"
            entry.touch()
            fixture = root / "tests/Case.as"
            fixture.touch()
            for name in ["tests/./Suite.as", "{project}/tests/Suite.as"]:
                case = {"command": ["{compiler}", "run", name, "{sources}"],
                        "sources": ["tests/**/*.as", "tests/*.as"]}
                command = runner.expand(root, case, "kas3c", "godot")["command"]
                self.assertEqual(len(command), 4, command)
                self.assertEqual(command[-1], str(fixture.resolve()))
