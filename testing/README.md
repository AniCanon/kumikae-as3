# Testing

Kumikae Testing lets you write tests for your game in ActionScript 3. It has three parts:

- **A small AS3 library,** in [`as3/app/anicanon/kumikae/testing/`](as3/app/anicanon/kumikae/testing) (`TestCase`, `Suite`, `Assert`, `AssertionError`) and [`godot/app/anicanon/kumikae/testing/`](godot/app/anicanon/kumikae/testing) (`TestScene`, `EngineTestRunner`).
- **A command-line runner,** `runner.py`, which runs the tests a project lists and streams each case live.
- **Rider integration,** in the [plugin](../editors/jetbrains/README.md#tests), which shows results in the
  IDE's Tests window.

Tests run on the interpreter, with `kas3c run` or inside headless Godot. Nothing is downloaded.

Mark public no-argument instance methods with `[Test]` and point a manifest target at the fixture directory:

```json
{"version":1,"tests":[{"name":"Models","discover":"tests"}]}
```

Add `"runtime": "godot"` to run discovered fixtures inside a single headless Godot process so Godot types
(`Vector2`, `Node`, `PackedScene`), scenes, and frame stepping work directly. Use `"ios-simulator"` or
`"android"` to export and stream the same fixtures from a booted iOS Simulator or ARM64 Android emulator:

```json
{"version":1,"tests":[{"name":"Game tests","discover":"tests","runtime":"godot"}]}
```

The runner uses the compiler's syntax tree and creates a fresh fixture for each test. Case IDs use the
qualified class name. Set `suitePackage` on a target to omit a common package prefix from its displayed IDs.
Rider uses the same IDs for the gutter, test results, reruns and source navigation.

### Metadata and lifecycle hooks

| Metadata | Where | Meaning |
|---|---|---|
| `[Test]` | Public instance method | Runs as a test case. Accepts optional `timeout=<ms>`, `expects="<ErrorClass>"`, and `allowEngineErrors=true`. |
| `[Before]`, `[After]` | Public instance method | Runs before and after each test on the same fixture instance. |
| `[BeforeClass]`, `[AfterClass]` | Public static method | Runs once before and after all tests in the fixture class. |
| `[Ignore]` / `[Ignore("reason")]` | Class or `[Test]` method | Skips the test(s) and reports them as ignored. |

### Godot engine tests (`TestScene`)

Inside a `"runtime": "godot"` target, tests run synchronously by default and can touch Godot APIs directly.
To mount scenes or step frames across `_process` / `_physicsProcess`, use `TestScene`:

```actionscript
[Test]
public function playerMoves():void {
    var player:Mover = TestScene.instantiate("res://scenes/Mover.tscn") as Mover;
    TestScene.afterFrames(2, function():void {
        Assert.assertTrue(player.position.x > 0);
    }).afterPhysicsFrames(2, function():void {
        Assert.assertTrue(player.physicsTicks >= 2);
    }).until(function():Boolean {
        return player.position.x >= 40;
    }, 3000, function():void {
        Assert.assertTrue(player.position.x >= 40);
    });
}
```

- `TestScene.root`, `TestScene.add(node)`, and `TestScene.instantiate(path)` mount nodes under a per-test
  sandbox that is freed after the test finishes.
- `afterFrames(n, fn)`, `afterPhysicsFrames(n, fn)`, `afterSeconds(s, fn)`, `until(pred, timeoutMs, fn)`,
  and `waitForSignal(signal, timeoutMs, fn)` chain steps on the engine loop before `[After]` and sandbox
  cleanup run.
- Any Godot `ERROR:` or `SCRIPT ERROR:` logged while a test is running fails that test unless it is marked
  `[Test(allowEngineErrors=true)]`.
- Set `"headless": false` on a Godot target when a test needs a window/GPU, or pass `--native` to `runner.py`
  to run against compiled native code instead of the interpreter.

## Write a test case

A test case is a class that extends `TestCase` and overrides `run`. The examples on this page test this
class from a game, in `src/Score.as`:

```actionscript
package game {
    public class Score {
        public var points:int;

        public function Score(points:int = 0) {
            this.points = points;
        }

        public function add(amount:int):void {
            points += amount;
        }
    }
}
```

The test case, in `tests/ScoreCase.as`:

```actionscript
package game.tests {
    import game.Score;
    import app.anicanon.kumikae.testing.Assert;
    import app.anicanon.kumikae.testing.TestCase;

    public class ScoreCase extends TestCase {
        private var initial:int;
        private var score:Score;

        public function ScoreCase(name:String, initial:int) {
            super(name);
            this.initial = initial;
        }

        override public function setUp():void {
            score = new Score(initial);
        }

        override public function run():void {
            score.add(10);
            Assert.assertEquals(initial + 10, score.points, "Adding points raises the score");
            Assert.assertTrue(score.points >= 0, "A score is never negative");
        }

        override public function tearDown():void {
            score = null;
        }
    }
}
```

- **Each case runs `setUp`, `run`, then `tearDown`.** If `setUp` fails, `run` is skipped. `tearDown` runs
  either way.
- **Constructor arguments make parameter cases.** The same class, constructed with different values, gives
  several cases with their own names and results.
- **A case's name is one line** and doesn't contain `::`.

| Assertion | Fails when |
|---|---|
| `Assert.assertTrue(value, message)` | `value` is false |
| `Assert.assertFalse(value, message)` | `value` is true |
| `Assert.assertEquals(expected, actual, message)` | The two differ (`!==`); emits comparison diff values |
| `Assert.assertNotEquals(unexpected, actual, message)` | The two are strictly equal (`===`) |
| `Assert.assertNull(value, message)` | `value !== null` |
| `Assert.assertNotNull(value, message)` | `value` is `null` or `undefined` |
| `Assert.assertApprox(expected, actual, tolerance, message)` | Difference exceeds `tolerance` (or either is `NaN`) |
| `Assert.assertThrows(block, expectedErrorName, message)` | `block()` doesn't throw, or throws a different error class |
| `Assert.fail(message)` | Always |

## Collect cases into a suite

A suite is built in the constructor of an entry class. `kas3c run` constructs that class, which runs the
suite. This one is `tests/ScoringSuite.as`:

```actionscript
package game.tests {
    import app.anicanon.kumikae.testing.Suite;

    public class ScoringSuite {
        public function ScoringSuite() {
            var suite:Suite = new Suite("Scoring");
            suite.add(new ScoreCase("Starts empty", 0));
            suite.add(new ScoreCase("Existing score", 10));
            if (!suite.run()) {
                throw new Error("Scoring tests failed");
            }
            trace("PASS scoring suite");
        }
    }
}
```

A manual suite carries on after a failure, so one run reports every failing case. Cases added to a `Suite`
run in the order they were registered.

To run the suite directly, pass the entry file first, then the rest of the program and the library:

```sh
kas3c run tests/ScoringSuite.as tests/ScoreCase.as src/Score.as <repository>/testing/as3/app/anicanon/kumikae/testing/*.as
```

## List the tests of a project

Put a manifest named `kas3-tests.json` beside `project.godot`:

```json
{
  "version": 1,
  "tests": [{
    "name": "Scoring",
    "source": "tests/ScoringSuite.as",
    "entryClass": "game.tests.ScoringSuite",
    "events": true,
    "command": [
      "{compiler}", "run", "tests/ScoringSuite.as", "{sources}",
      "{library}/app/anicanon/kumikae/testing/Assert.as",
      "{library}/app/anicanon/kumikae/testing/Suite.as",
      "{library}/app/anicanon/kumikae/testing/TestCase.as"
    ],
    "sources": ["src/**/*.as", "tests/**/*.as"],
    "marker": "PASS scoring suite",
    "timeout": 120
  }]
}
```

| Field | Meaning |
|---|---|
| `name` | The test's name. Unique, one line, with no `::` |
| `discover` | Optional. Directory of fixture classes whose `[Test]` methods are discovered automatically |
| `runtime` | Optional (for `discover` targets). `"cli"` (default, `kas3c run`), `"godot"`, `"ios-simulator"`, or `"android"` |
| `headless` | Optional (for `"runtime": "godot"`). `true` by default; set `false` when a test needs a window/GPU |
| `preset` | Optional (for mobile runtimes). Export preset name when more than one exists |
| `suitePackage` | Optional. Package prefix omitted from displayed case IDs for a `discover` target |
| `command` | The program to run, as an array of arguments (for manual targets). It's run without a shell |
| `sources` | Patterns for the project's `.as` files, relative to the manifest. They replace `{sources}` in the command |
| `exclude` | Optional. Patterns for files to leave out of `sources` |
| `marker` | A line the program must print when it has finished. Without it, the test fails |
| `timeout` | Optional. Seconds before the test is stopped. The default is 120 |
| `events`, `entryClass` | For a manual suite: report each case separately, and the class that runs the suite |
| `source` | The file the test's result links to |

In a command, `{compiler}` is `kas3c`, `{godot}` is Godot, `{project}` is the manifest's folder, and
`{library}` is this folder's `as3` directory.

A test without `events` and `entryClass` is one result: a plain script, or a Godot scene run with
`{godot}`. It passes when the process exits with status 0 and prints its marker.

Keep tests in the project's `tests` folder. The Godot extension leaves that folder out of the game, so a
mistake in a test can't stop the game. See
[Using AS3 in Godot](../docs/godot.md#tests-in-a-project).

## Run the tests

```sh
python3 testing/runner.py /path/to/project/kas3-tests.json --compiler /path/to/kas3c
```

The last line of output is a count, such as `AS3 tests: 1 passed, 0 failed`. The exit status is 0 only
when every test passed. The count includes each case in a suite and each plain script or scene test.
A target that fails to compile or start contributes one failed result with its diagnostic.

To run one case, name it as `<test>::<suite>::<case>`:

```sh
python3 testing/runner.py /path/to/project/kas3-tests.json --compiler /path/to/kas3c \
    --case 'Scoring::Scoring::Starts empty'
```

`--list` prints the manifest's tests. `--godot` names the Godot program, which defaults to `godot`. `--native`
runs `"runtime": "godot"` targets with `KAS3_NATIVE_PLAY=1` instead of the interpreter. `--device`,
`--preset`, and `--android-sdk` select the target simulator/emulator and Android SDK for mobile runtimes.

A test fails when its process exits with a nonzero status, prints a compiler or engine error, doesn't
print its marker, runs past its timeout, or doesn't finish its suite. Asking for a case that doesn't exist
fails too. Stopping the runner stops the processes it started.

## Run tests in Rider

Choose the **Kumikae AS3 > Tests** run configuration and select the manifest, or click the gutter icon
beside a `[Test]` method or fixture class. See [the plugin's README](../editors/jetbrains/README.md#tests).

For manual `Suite` targets, map the suite's cases to methods in the manifest to get gutter icons:

```json
"cases": [
  {"source": "tests/CourseTest.as", "suite": "CourseTest", "name": "gapJump", "method": "gapJump"},
  {"source": "tests/CourseTest.as", "suite": "CourseTest", "name": "magnetCollects", "method": "magnetCollects"}
]
```

`suite` and `name` must match what the suite reports. `method` names a method in `source`. The icon beside
the method runs that case alone, and the icon beside the class runs every case mapped to the file.

## What isn't there

- **Metadata.** Discovery supports public no-argument instance methods marked with `[Test]`. Use the bundled library for setup and teardown.
- **No parallel runs.**
- **No asynchronous cases.** A test that needs frames to pass runs as a Godot scene, as one result.

## How results are reported

A suite prints one record for each event, in a format that needs no escaping in AS3:

```text
@kas3-test-v1 testStarted 12 0
Starts empty

```

The first line has the event, the length of the name, and the length of the message, both in UTF-16 code
units. The name and the message follow, each on its own line. The runner checks the order, the names and
the lengths before it reports anything, so a program's ordinary output can't inject a test result.

## Test the library itself

```sh
python3 -m unittest discover -s testing/tests -v
compiler/build/kas3c run testing/tests/fixtures/LibraryTest.as testing/tests/fixtures/ProbeCase.as testing/as3/app/anicanon/kumikae/testing/*.as
```

The second command runs a fixture that records a failing case on purpose and checks that the suite carries
on. Its last line is `PASS framework lifecycle`.
