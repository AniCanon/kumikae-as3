# AS3 tests

Kumikae Testing is a small first-party AS3 library, a Python command-line runner,
and a Rider adapter. It uses the existing interpreter and Godot; it downloads
nothing. Keep assertions and fixtures in AS3, process/protocol handling in Python,
and IDE presentation in the JetBrains plugin.

## Write a case

```actionscript
package game.tests {
    import kumikae.testing.Assert;
    import kumikae.testing.TestCase;

    public class ScoreCase extends TestCase {
        private var initial:int;

        public function ScoreCase(label:String, initial:int) {
            super(label);
            this.initial = initial;
        }

        override public function setUp():void {
            // Construct a fresh fixture here.
        }

        override public function run():void {
            Assert.assertEquals(initial + 1, initial + 1, "Score increases");
            Assert.assertTrue(initial >= 0, "Initial score is nonnegative");
        }

        override public function tearDown():void {
            // Release fixture resources here, including after a failure.
        }
    }
}
```

Register named cases in an entry class's constructor:

```actionscript
var suite:Suite = new Suite("Scoring");
suite.add(new ScoreCase("Starts empty", 0));
suite.add(new ScoreCase("Existing score", 10));
if (!suite.run()) {
    throw new Error("Scoring tests failed");
}
trace("PASS scoring suite");
```

Each case runs `setUp`, `run`, then `tearDown`. A setup failure skips `run`;
teardown still runs. The suite continues after failures, including thrown values
with an empty message. Constructor arguments provide typed parameter cases with
individual names and results. Cases are sequential; a Godot integration harness
owns its frame-based lifecycle and process timeout.

Names must be nonempty single lines without `::`, which separates stable selection
identities. The adapter rejects unsupported names with an error. Failure details
may contain arbitrary punctuation and newlines. Each source file defines one class.

The conventions take cues from [FlexUnit assertions](https://flex.apache.org/flexunit/asdoc/flexunit/org/flexunit/Assert.html)
and [fixture lifecycle](https://flex.apache.org/flexunit/tutorial/flexunit/Unit-6.html),
[Kotlin's named tests and assertions](https://kotlinlang.org/docs/jvm-test-using-junit.html),
[Swift Testing's named parameter cases](https://developer.apple.com/documentation/testing/parameterizedtesting),
and [MSTest's setup and cleanup](https://learn.microsoft.com/en-us/dotnet/core/testing/unit-testing-mstest-writing-tests-lifecycle).
This is not a FlexUnit compatibility layer. `[Test]`/`[Before]` discovery, function
callbacks, automatic expression capture and parallel execution are not implemented.
Explicit `TestCase` subclasses use ordinary AS3 virtual dispatch and provide a
portable, deliberate registration API without relying on metadata discovery.

## Register project targets

Put `kas3-tests.json` beside `project.godot`:

```json
{
  "version": 1,
  "tests": [{
    "name": "Scoring",
    "source": "tests/ScoringSuite.as",
    "entryClass": "game.tests.ScoringSuite",
    "events": true,
    "command": ["{compiler}", "run", "tests/ScoringSuite.as", "{sources}"],
    "sources": ["src/**/*.as", "tests/**/*.as"],
    "exclude": ["src/presentation/**/*.as"],
    "marker": "PASS scoring suite",
    "timeout": 120
  }]
}
```

The project must include the library's `as3/kumikae/testing/*.as` sources in its
test source list. The sample's `tools/sync_test_framework.py` copies that version
locally for Godot imports and can verify it with `--check`. Exclude tests from
production exports. `{library}` expands to this runner's library directory when
an external source path is preferable. `{compiler}`, `{godot}` and `{project}`
expand to existing programs and the manifest directory. Commands are argument
arrays, executed without a shell. Only registered commands execute.

Legacy assertion scripts and Godot scenes can omit `events`/`entryClass`. They
appear as one test each and must print an exact completion-marker line. The
runner rejects nonzero exits, engine/compiler errors, missing completion markers,
timeouts, malformed events and incomplete suites. A missing requested case fails
even when other selected cases pass. Cancellation terminates the owned process
group, including children that outlive their parent.

```sh
python3 testing/runner.py /path/to/project/kas3-tests.json --compiler /path/to/kas3c
python3 testing/runner.py /path/to/project/kas3-tests.json --case 'Scoring::Scoring::Starts empty'
```

Single-case execution generates a temporary AS3 entry that sets `Suite.only` and
constructs the registered entry class. Original source files are never rewritten.
The temporary directory is removed on exit. CLI entry constructors must finish the
suite and report its result. Godot integration targets are filtered at scene level.

## Rider

Choose **AS3 Tests** in Run Configurations and select the manifest. The sample
ships that configuration. A gutter Run action appears on a registered source's
class. The native Tests tree shows individual framework cases, details, source
navigation, and **Rerun Failed Tests**. Suite/case identity is retained in the name.
The adapter uses the IDE's bundled test-runner module, not an external dependency.

## Protocol and verification

Version 1 events use length-framed text so labels and multiline failure messages
need no escaping in AS3. A record starts with
`@kas3-test-v1 EVENT NAME_LENGTH MESSAGE_LENGTH`, a newline, the name, a newline,
the message, and a newline. Lengths count UTF-16 code units, matching AS3 String.length. The adapter validates
ordering, names, size limits and complete lifecycles before emitting escaped
TeamCity service messages. Raw program output cannot inject test events into Rider.

Run `python3 -m unittest discover -s testing/tests -v`. With a built compiler, also
run `kas3c run testing/tests/fixtures/LibraryTest.as testing/tests/fixtures/ProbeCase.as testing/as3/kumikae/testing/*.as`.
The fixture intentionally records a failed case and verifies continuation and
teardown; its final marker is `PASS framework lifecycle`. The JetBrains sandbox
probe separately verifies that the native tree shows a pass and a failure.
