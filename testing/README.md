# Testing

Kumikae Testing lets you write tests for your game in ActionScript 3. It has three parts:

- **A small AS3 library,** in [`as3/app/anicanon/kumikae/testing/`](as3/app/anicanon/kumikae/testing): `TestCase`, `Suite` and
  `Assert`.
- **A command-line runner,** `runner.py`, which runs the tests a project lists and reports each case.
- **Rider integration,** in the [plugin](../editors/jetbrains/README.md#tests), which shows results in the
  IDE's Tests window.

Tests run on the interpreter, with `kas3c run` or inside Godot. Nothing is downloaded.

Mark public no-argument instance methods with `[Test]` and point a manifest target at the fixture directory:

```json
{"version":1,"tests":[{"name":"Models","discover":"tests"}]}
```

The runner uses the compiler’s syntax tree and creates a fresh fixture for each test. Case IDs use the
qualified class name. Set `suitePackage` on a target to omit a common package prefix from its displayed IDs.
Rider uses the same IDs for the gutter, test results, reruns and source navigation.

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
| `Assert.assertEquals(expected, actual, message)` | The two differ |
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

A suite carries on after a failure, so one run reports every failing case. Registration is explicit:
adding a method or a `[Test]` tag doesn't register a case. Cases run one after another.

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
| `command` | The program to run, as an array of arguments. It's run without a shell |
| `sources` | Patterns for the project's `.as` files, relative to the manifest. They replace `{sources}` in the command |
| `exclude` | Optional. Patterns for files to leave out of `sources` |
| `marker` | A line the program must print when it has finished. Without it, the test fails |
| `timeout` | Optional. Seconds before the test is stopped. The default is 120 |
| `events`, `entryClass` | For a suite: report each case separately, and the class that runs the suite |
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
when every test passed.

To run one case, name it as `<test>::<suite>::<case>`:

```sh
python3 testing/runner.py /path/to/project/kas3-tests.json --compiler /path/to/kas3c \
    --case 'Scoring::Scoring::Starts empty'
```

`--list` prints the manifest's tests. `--godot` names the Godot program, which defaults to `godot`.

A test fails when its process exits with a nonzero status, prints a compiler or engine error, doesn't
print its marker, runs past its timeout, or doesn't finish its suite. Asking for a case that doesn't exist
fails too. Stopping the runner stops the processes it started.

## Run tests in Rider

Choose the **AS3 Tests** run configuration and select the manifest. See
[the plugin's README](../editors/jetbrains/README.md#tests).

To get a Run icon beside a test method, map the suite's cases to methods in the manifest:

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
