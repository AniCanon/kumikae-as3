"""Tests for mobile engine test staging, device resolution, and runner integration."""
import importlib.util
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMPILER = os.environ.get("KAS3C", "")


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


mobile = _load("mobile_runner", ROOT / "mobile_runner.py")
discovery = _load("discovery_for_mobile_test", ROOT / "discovery.py")
runner = _load("runner_for_mobile_test", ROOT / "runner.py")


class MobileRunnerTest(unittest.TestCase):
    def test_validates_mobile_runtimes(self):
        for runtime in ("ios-simulator", "android", "web"):
            manifest = {"version": 1, "tests": [{"name": "Mobile", "discover": "tests", "runtime": runtime}]}
            runner.validate(manifest)
        with self.assertRaises(ValueError):
            runner.validate({"version": 1, "tests": [{"name": "Bad", "discover": "tests", "runtime": "psp"}]})

    @unittest.skipUnless(COMPILER, "Set KAS3C to type-check staged mobile test runners")
    def test_stage_project_bundles_runner_scene_and_compiles(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = (Path(tmp) / "game").resolve()
            (project / "src").mkdir(parents=True)
            (project / "src/build").mkdir(parents=True)
            (project / "src/build/Helper.as").write_text("package build { public class Helper {} }\n")
            (project / "build").mkdir()
            (project / "build/ignored.txt").write_text("root build ignored")
            (project / "tests").mkdir()
            (project / "project.godot").write_text(
                '[application]\nconfig/name="Sample"\nrun/main_scene="res://scenes/main.tscn"\n'
            )
            (project / "src/Model.as").write_text("package game { public class Model { public var n:int = 1; } }\n")
            (project / "tests/SampleTest.as").write_text(
                "package qa {\n"
                "    import app.anicanon.kumikae.testing.Assert;\n"
                "    import app.anicanon.kumikae.testing.TestScene;\n"
                "    import game.Model;\n"
                "    public class SampleTest {\n"
                "        [Test] public function runsOnDevice():void {\n"
                "            Assert.assertEquals(1, new Model().n);\n"
                "            TestScene.afterFrames(1, function():void {});\n"
                "        }\n"
                "    }\n"
                "}\n"
            )
            target = discovery.expand_target(
                project,
                {"name": "Mobile", "discover": "tests", "runtime": "ios-simulator"},
                COMPILER,
            )
            staged = Path(tmp) / "staged"
            staged.mkdir()
            for tier in ("as3", "godot"):
                shutil.copytree(ROOT / tier / "app", project / "tests/vendor/app", dirs_exist_ok=True)
            original = (project / "tests/SampleTest.as").read_text()
            scene = mobile.stage_project(project, staged, target, ["qa.SampleTest::runsOnDevice"])

            self.assertEqual("res://tests/kas3_runner/TestRunner.tscn", scene)
            self.assertIn('run/main_scene="res://tests/kas3_runner/TestRunner.tscn"', (staged / "project.godot").read_text())
            self.assertIn('ExtResource("1")', (staged / "tests/kas3_runner/TestRunner.tscn").read_text())
            self.assertIn('Suite.only.push("qa.SampleTest::runsOnDevice")', (staged / "tests/kas3_runner/Kas3EngineDriver.as").read_text())
            self.assertFalse((staged / "tests/kas3_runner/run_tests.gd").exists())
            self.assertTrue((staged / "src/build/Helper.as").is_file())
            self.assertFalse((staged / "build").exists())

            (staged / "tests/SampleTest.as").write_text(original + "\n")
            self.assertEqual(original, (project / "tests/SampleTest.as").read_text())
            as_files = sorted(str(p) for p in staged.rglob("*.as"))
            check = subprocess.run([COMPILER, "check", *as_files], capture_output=True, text=True)
            self.assertEqual(0, check.returncode, check.stdout + check.stderr)

    def test_explicit_device_selection_and_list_output(self):
        self.assertEqual("SIM-123", mobile.resolve_ios_device("SIM-123"))
        self.assertEqual("emulator-5554", mobile.resolve_android_device(Path("/bin/false"), "emulator-5554"))
        if COMPILER:
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "tests").mkdir()
                (root / "tests/OneTest.as").write_text("package qa { public class OneTest { [Test] public function ok():void {} } }\n")
                manifest = root / "kas3-tests.json"
                manifest.write_text(json.dumps({
                    "version": 1,
                    "tests": [{"name": "Phone", "discover": "tests", "runtime": "android"}],
                }))
                cmd = [sys.executable, "-I", str(ROOT / "runner.py"), str(manifest), "--compiler", COMPILER, "--list"]
                for args, runtime in (([], "android"), (["--runtime", "godot"], "godot")):
                    listed = subprocess.run(cmd + args, capture_output=True, text=True)
                    self.assertEqual(0, listed.returncode, listed.stdout + listed.stderr)
                    rows = json.loads(listed.stdout)
                    self.assertEqual(runtime, rows[0]["runtime"])
                    self.assertEqual(["ok"], [c["method"] for c in rows[0]["cases"]])
                manifest.write_text(json.dumps({"version": 1, "tests": [
                    {"name": "Manual", "command": ["echo", "DONE"], "marker": "DONE"}
                ]}))
                invalid = subprocess.run(cmd + ["--runtime", "godot"], capture_output=True, text=True)
                self.assertEqual(1, invalid.returncode)
                self.assertIn("requires a discovery target", invalid.stderr)
