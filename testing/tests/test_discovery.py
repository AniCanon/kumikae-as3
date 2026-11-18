import json
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest


class DiscoveryTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("KAS3C"), "Set KAS3C to run the compiler discovery check")
    def test_new_annotated_method_runs_without_registration(self):
        runner = Path(__file__).resolve().parents[1] / "runner.py"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "tests").mkdir()
            (root / "tests/Score.as").write_text('''package qa { import app.anicanon.kumikae.testing.Assert; public class Score {
                // [Test] public function ignored():void {}
                [Test] public function scores():void { Assert.assertEquals(1, 1); }
                [Test] public function fails():void { throw new Error("Intentional failure"); }
            } }''')
            manifest = root / "kas3-tests.json"
            manifest.write_text(json.dumps({"version": 1, "tests": [{"name": "Model", "discover": "tests"}]}))
            base = [sys.executable, "-I", str(runner), str(manifest), "--compiler", os.environ["KAS3C"]]
            listed = subprocess.run(base + ["--list"], capture_output=True, text=True)
            self.assertEqual(0, listed.returncode, listed.stdout + listed.stderr)
            self.assertEqual(["scores", "fails"], [row["method"] for row in json.loads(listed.stdout)[0]["cases"]])
            for name, code in [("scores", 0), ("fails", 1), ("missing", 1)]:
                result = subprocess.run(base + ["--case", "Model::qa.Score::" + name], capture_output=True, text=True)
                self.assertEqual(code, result.returncode, result.stdout + result.stderr)
                if name == "scores":
                    self.assertNotIn("Score::fails", result.stdout)

    @unittest.skipUnless(os.environ.get("KAS3C"), "Requires the compiler")
    def test_fixture_can_be_named_test_case(self):
        runner = Path(__file__).resolve().parents[1] / "runner.py"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / "tests" / "qa"
            folder.mkdir(parents=True)
            (folder / "TestCase.as").write_text("package qa { public class TestCase {"
                                                "[Test] public function works():void {} } }")
            manifest = root / "kas3-tests.json"
            manifest.write_text(json.dumps({"version": 1, "tests": [{"name": "Game", "discover": "tests"}]}))
            result = subprocess.run([sys.executable, "-I", str(runner), str(manifest), "--compiler",
                                     os.environ["KAS3C"]], capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_unrelated_target_does_not_require_a_compiler(self):
        runner = Path(__file__).resolve().parents[1] / "runner.py"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "kas3-tests.json"
            manifest.write_text(json.dumps({"version": 1, "tests": [
                {"name": "Model", "discover": "tests"},
                {"name": "Scene", "marker": "pass", "command": [sys.executable, "-c", "print('pass')"]}]}))
            result = subprocess.run([sys.executable, "-I", str(runner), str(manifest), "--case", "Scene"],
                                    capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)

    @unittest.skipUnless(os.environ.get("KAS3C"), "Requires the compiler")
    def test_same_fixture_name_in_two_packages(self):
        runner = Path(__file__).resolve().parents[1] / "runner.py"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for package in ["first", "second"]:
                folder = root / "tests" / package
                folder.mkdir(parents=True)
                (folder / "Score.as").write_text("package " + package + " { public class Score {"
                    + "[Test] public function score():void {} } }")
            manifest = root / "kas3-tests.json"
            manifest.write_text(json.dumps({"version": 1, "tests": [{"name": "Model", "discover": "tests"}]}))
            result = subprocess.run([sys.executable, "-I", str(runner), str(manifest), "--compiler", os.environ["KAS3C"]],
                                    capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            for package in ["first", "second"]:
                self.assertIn("Model::" + package + ".Score::score", result.stdout)
