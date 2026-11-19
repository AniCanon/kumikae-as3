"""End-to-end headless Godot engine test runner verification."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
RUNNER = REPO / "testing" / "runner.py"
COMPILER = os.environ.get("KAS3C", str(REPO / "compiler/build/kas3c"))
GODOT = os.environ.get("KAS3_GODOT") or shutil.which("godot") or ""
EXTENSION_BIN = REPO / "compiler/tests/godot_project/bin"


def _has_godot_env():
    return (
        bool(GODOT)
        and Path(COMPILER).is_file()
        and any(EXTENSION_BIN.glob("libkas3_godot.*"))
    )


@unittest.skipUnless(_has_godot_env(), "Requires built kas3c, libkas3_godot, and godot on PATH")
class GodotEngineTest(unittest.TestCase):
    def _create_project(self, root: Path):
        (root / "bin").symlink_to(EXTENSION_BIN)
        (root / ".godot").mkdir()
        (root / ".godot/extension_list.cfg").write_text("res://kas3.gdextension\n")
        (root / "kas3.gdextension").write_text(
            '[configuration]\n'
            'entry_symbol = "kas3_godot_init"\n'
            'compatibility_minimum = "4.7"\n'
            'reloadable = false\n\n'
            '[libraries]\n'
            'macos.debug = "res://bin/libkas3_godot.dylib"\n'
            'macos.release = "res://bin/libkas3_godot.dylib"\n'
            'linux.debug.x86_64 = "res://bin/libkas3_godot.so"\n'
            'linux.release.x86_64 = "res://bin/libkas3_godot.so"\n'
        )
        (root / "project.godot").write_text(
            'config_version=5\n\n'
            '[application]\n'
            'config/name="EngineTestProject"\n'
            'config/features=PackedStringArray("4.7")\n'
        )
        (root / "src").mkdir()
        (root / "src/Mover.as").write_text(
            'package game {\n'
            '    import godot.Node2D;\n'
            '    import godot.Vector2;\n'
            '    public class Mover extends Node2D {\n'
            '        public var processTicks:int = 0;\n'
            '        public var physicsTicks:int = 0;\n'
            '        override public function _process(delta:Number):void {\n'
            '            processTicks++;\n'
            '            position = new Vector2(position.x + 10, position.y);\n'
            '        }\n'
            '        override public function _physicsProcess(delta:Number):void {\n'
            '            physicsTicks++;\n'
            '        }\n'
            '    }\n'
            '}\n'
        )
        (root / "scenes").mkdir()
        (root / "scenes/Mover.tscn").write_text(
            '[gd_scene load_steps=2 format=3]\n\n'
            '[ext_resource type="Script" path="res://src/Mover.as" id="1"]\n\n'
            '[node name="Mover" type="Node2D"]\n'
            'script = ExtResource("1")\n'
        )
        (root / "tests").mkdir()
        (root / "tests/SceneIntegrationTest.as").write_text(
            'package qa {\n'
            '    import app.anicanon.kumikae.testing.Assert;\n'
            '    import app.anicanon.kumikae.testing.TestScene;\n'
            '    import game.Mover;\n'
            '    import godot.pushError;\n'
            '    import godot.Vector2;\n\n'
            '    public class SceneIntegrationTest {\n'
            '        public static var setupCount:int = 0;\n'
            '        private var beforeRan:Boolean = false;\n\n'
            '        [BeforeClass] public static function initAll():void {\n'
            '            setupCount++;\n'
            '        }\n\n'
            '        [Before] public function setUp():void {\n'
            '            beforeRan = true;\n'
            '        }\n\n'
            '        [Test] public function vectorMathRunsInGodot():void {\n'
            '            Assert.assertTrue(beforeRan);\n'
            '            Assert.assertEquals(1, setupCount);\n'
            '            var sum:Vector2 = new Vector2(2, 3) + new Vector2(4, 5);\n'
            '            Assert.assertEquals(6, sum.x);\n'
            '            Assert.assertEquals(8, sum.y);\n'
            '        }\n\n'
            '        [Test] public function instantiatesSceneAndStepsFrames():void {\n'
            '            trace("mounting Mover.tscn");\n'
            '            var mover:Mover = TestScene.instantiate("res://scenes/Mover.tscn") as Mover;\n'
            '            Assert.assertNotNull(mover);\n'
            '            TestScene.afterFrames(2, function():void {\n'
            '                Assert.assertTrue(mover.processTicks >= 2, "Expected process ticks");\n'
            '            }).afterPhysicsFrames(2, function():void {\n'
            '                Assert.assertTrue(mover.physicsTicks >= 2, "Expected physics ticks");\n'
            '            }).until(function():Boolean {\n'
            '                return mover.position.x >= 40;\n'
            '            }, 3000, function():void {\n'
            '                Assert.assertTrue(mover.position.x >= 40);\n'
            '            });\n'
            '        }\n\n'
            '        [Test(allowEngineErrors=true)] public function allowsExpectedEngineError():void {\n'
            '            pushError("intentional engine error");\n'
            '        }\n\n'
            '        [Test] public function failsOnUnexpectedEngineError():void {\n'
            '            pushError("unexpected engine failure");\n'
            '        }\n\n'
            '        [Test] [Ignore("pending feature")] public function skippedEngineTest():void {\n'
            '            Assert.fail("should never run");\n'
            '        }\n'
            '    }\n'
            '}\n'
        )
        manifest = root / "kas3-tests.json"
        manifest.write_text(json.dumps({
            "version": 1,
            "tests": [{"name": "Engine", "discover": "tests", "runtime": "godot"}],
        }))
        return manifest

    def test_godot_engine_target_runs_scenes_frames_errors_and_leaves_project_clean(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = self._create_project(root)
            before_files = sorted(p.relative_to(root).as_posix() for p in root.rglob("*") if ".godot" not in p.parts)
            result = subprocess.run(
                [sys.executable, "-I", str(RUNNER), str(manifest), "--compiler", COMPILER, "--godot", GODOT],
                capture_output=True, text=True, timeout=60,
            )
            after_files = sorted(p.relative_to(root).as_posix() for p in root.rglob("*") if ".godot" not in p.parts)
            self.assertEqual(before_files, after_files, "Godot test run must not write files into the project")
            self.assertEqual(1, result.returncode, result.stdout + result.stderr)
            self.assertIn("testCount count='5'", result.stdout)
            self.assertIn("testSuiteStarted name='qa.SceneIntegrationTest'", result.stdout)
            self.assertIn("testStdOut name='instantiatesSceneAndStepsFrames' out='mounting Mover.tscn|n'", result.stdout)
            self.assertIn("testFinished name='vectorMathRunsInGodot' duration='", result.stdout)
            self.assertIn("testFinished name='instantiatesSceneAndStepsFrames' duration='", result.stdout)
            self.assertIn("testFinished name='allowsExpectedEngineError' duration='", result.stdout)
            self.assertIn("testStdErr name='failsOnUnexpectedEngineError'", result.stdout)
            self.assertIn("testFailed name='failsOnUnexpectedEngineError'", result.stdout)
            self.assertIn("testIgnored name='skippedEngineTest' message='pending feature'", result.stdout)
            self.assertIn("AS3 tests: 3 passed, 1 failed, 1 ignored", result.stdout)

            # Single-case filter runs only the requested engine test and exits 0.
            single = subprocess.run(
                [sys.executable, "-I", str(RUNNER), str(manifest), "--compiler", COMPILER, "--godot", GODOT,
                 "--case", "Engine::qa.SceneIntegrationTest::instantiatesSceneAndStepsFrames"],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(0, single.returncode, single.stdout + single.stderr)
            self.assertIn("AS3 tests: 1 passed, 0 failed", single.stdout)
