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
                    + "[BeforeClass] public static function setup():void {}"
                    + "[AfterClass] public static function teardown():void {}"
                    + "[Test] public function score():void {} } }")
            manifest = root / "kas3-tests.json"
            manifest.write_text(json.dumps({"version": 1, "tests": [{"name": "Model", "discover": "tests"}]}))
            result = subprocess.run([sys.executable, "-I", str(runner), str(manifest), "--compiler", os.environ["KAS3C"]],
                                    capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            for package in ["first", "second"]:
                self.assertIn("testSuiteStarted name='" + package + ".Score'", result.stdout)
                self.assertIn("tests/" + package + "/Score.as?method=score", result.stdout)

    @unittest.skipUnless(os.environ.get("KAS3C"), "Requires the compiler")
    def test_hooks_ignore_expects_and_comparison_diffs_run_end_to_end(self):
        runner = Path(__file__).resolve().parents[1] / "runner.py"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "tests").mkdir()
            (root / "tests/LifecycleTest.as").write_text('''package qa {
                import app.anicanon.kumikae.testing.Assert;
                public class LifecycleTest {
                    public static var log:Array = [];
                    private var value:int = 0;

                    [BeforeClass] public static function setupAll():void { log.push("beforeClass"); }
                    [AfterClass] public static function teardownAll():void { log.push("afterClass"); }
                    [Before] public function setup():void { value = 41; log.push("before"); }
                    [After] public function teardown():void { log.push("after"); }

                    [Test] public function runsWithHooks():void {
                        trace("hello from runsWithHooks");
                        Assert.assertEquals(42, value + 1);
                        Assert.assertFalse(false);
                        Assert.assertNotEquals(0, value);
                        Assert.assertNull(null);
                        Assert.assertNotNull(this);
                        Assert.assertApprox(1.5, 1.50001, 0.001);
                        Assert.assertThrows(function():void { throw new RangeError("bad"); }, "RangeError");
                    }

                    [Test(expects="RangeError")] public function expectsSpecificError():void {
                        throw new RangeError("out of bounds");
                    }

                    [Test] [Ignore("not ready")] public function skippedCase():void {
                        Assert.fail("should not run");
                    }

                    [Test] public function comparisonDiff():void {
                        Assert.assertEquals("expected-val", "actual-val", "Mismatch");
                    }
                }
            }''')
            (root / "tests/IgnoredFixture.as").write_text('''package qa {
                [Ignore("whole fixture skipped")]
                public class IgnoredFixture {
                    [Test] public function skippedByClass():void {}
                }
            }''')
            manifest = root / "kas3-tests.json"
            manifest.write_text(json.dumps({"version": 1, "tests": [{"name": "Unit", "discover": "tests"}]}))
            result = subprocess.run(
                [sys.executable, "-I", str(runner), str(manifest), "--compiler", os.environ["KAS3C"]],
                capture_output=True, text=True,
            )
            self.assertEqual(1, result.returncode, result.stdout + result.stderr)
            self.assertIn("testCount count='5'", result.stdout)
            self.assertIn("testStdOut name='runsWithHooks' out='hello from runsWithHooks|n'", result.stdout)
            self.assertIn("testFinished name='runsWithHooks' duration='", result.stdout)
            self.assertIn("testIgnored name='skippedCase' message='not ready'", result.stdout)
            self.assertIn("testIgnored name='skippedByClass' message='whole fixture skipped'", result.stdout)
            self.assertIn("type='comparisonFailure'", result.stdout)
            self.assertIn("expected='expected-val' actual='actual-val'", result.stdout)
            self.assertIn("AS3 tests: 2 passed, 1 failed, 2 ignored", result.stdout)

    @unittest.skipUnless(os.environ.get("KAS3C"), "Requires the compiler")
    def test_discovery_accepts_override_and_rejects_invalid_test_methods(self):
        runner = Path(__file__).resolve().parents[1] / "runner.py"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "tests").mkdir()
            (root / "tests/Base.as").write_text("package qa { public class Base { public function check():void {} } }")
            (root / "tests/Sub.as").write_text("package qa { public class Sub extends Base { [Test] override public function check():void {} } }")
            manifest = root / "kas3-tests.json"
            manifest.write_text(json.dumps({"version": 1, "tests": [{"name": "Model", "discover": "tests"}]}))
            listed = subprocess.run([sys.executable, "-I", str(runner), str(manifest), "--compiler", os.environ["KAS3C"], "--list"],
                                    capture_output=True, text=True)
            self.assertEqual(0, listed.returncode, listed.stdout + listed.stderr)
            self.assertEqual(["check"], [row["method"] for row in json.loads(listed.stdout)[0]["cases"]])
            for snippet in (
                "package qa { public class Bad { [Test] public function takesArg(x:int):void {} } }",
                "package qa { public class Bad { [Test] public static function staticMethod():void {} } }",
                "package qa { public class Bad { [Test] public function get value():int { return 1; } } }",
            ):
                with self.subTest(snippet=snippet):
                    (root / "tests/Bad.as").write_text(snippet)
                    bad = subprocess.run([sys.executable, "-I", str(runner), str(manifest), "--compiler", os.environ["KAS3C"], "--list"],
                                         capture_output=True, text=True)
                    self.assertNotEqual(0, bad.returncode)
                    (root / "tests/Bad.as").unlink()
