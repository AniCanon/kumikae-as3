# Contributing to the plugin

This page has the conventions for the plugin's Kotlin and Compose code, and the checks to run before you
open a pull request. The repository's [CONTRIBUTING.md](../../CONTRIBUTING.md) applies too. For the
structure of the code, read [ARCHITECTURE.md](ARCHITECTURE.md).

## The stack

The stack is fixed. Don't add a language, a framework or a dependency without opening an issue first.

| What | Uses |
|---|---|
| Build | Gradle through the wrapper (`./gradlew`), and the IntelliJ Platform Gradle Plugin |
| Language | Kotlin, on a JDK 25, with the Compose compiler plugin |
| UI | Compose Swing UI, mounted through `composePanel` in `ui/ComposeHost.kt` |
| Style | ktlint for Kotlin, and Xcode's swift-format for the Swift helper |
| Tests | JUnit, on the JUnit Platform |
| The Simulator helper | Swift 6, with no package dependencies |

The versions are in `build.gradle.kts`. The build compiles against an installed Rider and never downloads
an IDE or a JDK.

## Checks

From this folder, with `JAVA_HOME` set to a JDK 25:

```sh
./gradlew check buildPlugin
python3 -m unittest discover -s tests -v
```

- `check` runs the style check and the tests of both modules. On macOS with Swift installed, it also lints
  the Swift helper.
- `buildPlugin` refuses to build while the style check fails.
- `./gradlew ktlintFormat` fixes most style findings. `./gradlew formatSimulatorBridge` formats the Swift.
- `./gradlew verifyPlugin` runs JetBrains' plugin verifier against the installed Rider and IntelliJ IDEA.
  `RIDER_HOME` and `IDEA_HOME` say where they are.

Read the test count after you change the build. A missing test engine gives a green build that ran
nothing.

## Kotlin

Follow [JetBrains' Kotlin coding conventions](https://kotlinlang.org/docs/coding-conventions.html). The
`.editorconfig` selects IntelliJ's Kotlin style, 120-column lines and trailing commas.

- Prefer explicit imports, immutable local values, and named arguments where a call would be ambiguous.
- A `@Composable` function that emits UI is named in PascalCase. Other functions are camelCase.
- Keep files small, with one purpose each. Aim for under 300 lines.
- Write no banner comments. A comment says why, not what.
- Don't add a blanket suppression or a baseline to get a style finding past the check.

## Tests first

Write the test, watch it fail for the reason you expect, then make it pass.

| A change to | Is tested in |
|---|---|
| What the Simulator window shows or does | `SimulatorWindowControllerTest`, with `ManualBackgroundTasks` deciding when work runs and when its result arrives |
| How protocol JSON maps to types | `ApiSimulatorRepositoryTest` |
| Anything in `core/` | `core`'s own tests |
| The wire protocol | [protocol.md](protocol.md), the service, both clients and the Python client's tests, changed together |

## Where things live

| Thing | Lives in | Never in |
|---|---|---|
| State the screen shows | A window controller, as observable state | A Swing component or a view |
| Requests, processes, files, JSON | A repository behind a typed interface, injected | A controller, a view or a composable |
| Threads | `BackgroundTasks`, injected, with results delivered on the UI thread | An executor inside a view |
| The choice of concrete services | The composition root (`*Composition`) and the tool-window factory | A default argument of a view or a controller |
| Layout, text, enabled and disabled | Composables that take values and callbacks | A controller |

Resolve IDE services and paths at the plugin's entry points, then pass them to ordinary controllers and
views. IDE light services are the one exception: JetBrains
[doesn't support constructor injection](https://plugins.jetbrains.com/docs/intellij/plugin-services.html#constructor)
of other services, so resolve those when a method needs them. Don't add a second dependency-injection
container, and don't look up services inside composables.

## Lifetimes

- Every worker, composition and session belongs to its window's `Disposable` and is disposed with it, on
  the UI thread.
- After disposal, nothing is delivered. A late result is dropped.
- A result for something that's no longer current, such as another device, is dropped.
- A second action while one runs is refused with a status the user sees. It's never queued in silence.
- A hidden window doesn't poll.

## Compose over Swing

The plugin applies Google's guidance on
[where to hoist state](https://developer.android.com/develop/ui/compose/state-hoisting),
[thinking in Compose](https://developer.android.com/develop/ui/compose/mental-model) and
[side effects](https://developer.android.com/develop/ui/compose/side-effects) to Compose Swing and the
IDE's lifecycle.

- **Hoist shared state to the controller.** Keep a draft form value at the nearest composition that uses
  it. Text, selections and whether a button is enabled belong in observable state, not in retained Swing
  controls.
- **Keep composable bodies free of I/O** and of changes visible outside the composition. Use an effect only
  when the work belongs to the composition, such as pushing a new picture into the native screen.
- **`remember` caches a value.** It isn't an effect, and it isn't where a retained Swing control is
  attached to a new parent. Use `SwingNode`'s factory and update phases for that.
- **Key a retained native node by the component's identity,** as `Native` does, so that replacing the
  control replaces the node.
- **A reusable composable takes a `SwingModifier` as its first optional parameter** and passes it to its
  root.
- **Use native Swing controls only where the IDE's own behavior is needed,** such as a painted picture
  that takes the mouse, or a console. Mount them through `Native` and `NativeScrollPane`.
- **Don't** mark mutable Swing components `@Stable` or `@Immutable`, add `derivedStateOf` without a
  measured need, or bring Android's `ViewModel` or Activity APIs into the plugin.

## Checks in a sandbox IDE

These start an IDE with separate settings under `.intellijPlatform/sandbox/`. They never touch your own
IDE's settings. Run one at a time.

```sh
./gradlew runIde
```

This starts the installed Rider with the plugin loaded, for trying a change by hand.

### The UI probe

```sh
./gradlew runIde -PuiProbe
```

The UI probe is a second, test-only plugin that exists only in the sandbox. It's never part of the plugin
zip. Without showing a window, it:

- Mounts the real Simulator panel over a synthetic service, in the light theme and the dark theme, and
  presses Refresh, Boot, Launch and Install through the real buttons.
- Types in a real editor: pairing, indentation, completion with Enter and Tab, snippets, automatic
  imports, override stubs, undo, signature help, hover, navigation and outline.
- Writes pictures and `result.txt` to `build/reports/ui/`. The task fails unless the result is `passed`.

Run it after any change to a view, a lifetime or a language client, and look at the pictures. Text that's
cut off, controls that overlap, and unreadable colors are defects.

The probe proves layout and wiring. It doesn't prove behavior against a real Simulator.

### Both IDEs, with and without LSP4IJ

To check which AS3 adapter the plugin descriptor registers, set `KUMIKAE_LSP_DESCRIPTOR_PROBE=1`
for the four sandbox tasks below. Each sandbox checks whether the AS3 parameter-help handler is present.

After you change a language client, run all four combinations, one after another:

```sh
./gradlew --offline check buildPlugin verifyPlugin
./gradlew --offline runIdeaWithoutLsp4ij -PuiProbe
./gradlew --offline runIdea -PuiProbe
./gradlew --offline runIde -PuiProbe
./gradlew --offline runRiderWithoutLsp4ij -PuiProbe
```

| Task | Checks | Reports in |
|---|---|---|
| `runIdea` | IntelliJ IDEA with LSP4IJ | `build/reports/ui-idea` |
| `runIdeaWithoutLsp4ij` | The install notification, and typing help without a server | `build/reports/ui-runIdeaWithoutLsp4ij` |
| `runIde` | Rider's own client, with LSP4IJ installed and unused | `build/reports/ui` |
| `runRiderWithoutLsp4ij` | Rider's own client | `build/reports/ui-runRiderWithoutLsp4ij` |

Save a run's reports before the next run overwrites them.

Run `KUMIKAE_RIDER_STARTUP_PROBE=1 ./gradlew --offline runIde -PuiProbe` to check Rider’s startup
adapters and close and reopen a sandbox project with the Problems window visible.

### Opt-in probes

Each of these is switched on by an environment variable and never runs in an ordinary test run.

| Variable | With | What it does |
|---|---|---|
| `KUMIKAE_TEST_ACTIONS_PROBE=1` | `runIde -PuiProbe` | Checks only the AS3 test integration: gutter actions, the result tree, and navigation to a failing line |
| `KUMIKAE_COMPLETION_CONTROL_PROBE=1` | `runIde -PuiProbe` | Opens the same projects as plain text, with no AS3 session, to tell a platform warning from one the plugin causes |
| `KUMIKAE_COMPLETION_RETENTION_PROBE=1` | `runIde -PuiProbe` | Opens and closes ten projects over ten minutes and counts closed projects that are still held |
| `KUMIKAE_SIMULATOR_LIVE_PROBE=1` | `runIde -PuiProbe` | Drives a Simulator that is already booted, through the real panel and the native helper |
| `KUMIKAE_IOS_TARGET_PROBE_ONLY=1` | `runIde -PuiProbe` | Checks physical and simulator selection and the signed app field in both themes |
| `KUMIKAE_DEVICE_SETTINGS_PROBE=1` | `runIde -PuiProbe` | Exercises the device catalog and project settings in light and dark themes |
| `KUMIKAE_SIMULATOR_MEMORY_PROBE=1` | `runIde -PuiProbe` | Ten minutes of synthetic frames through a mounted panel, with memory samples |
| `KUMIKAE_FRAME_RETENTION_PROBE=1` | `:test --tests '*FrameRetentionProbe'` | Paints synthetic frames through the panel's own classes in a plain JVM, and counts retained images |
| `KUMIKAE_NATIVE_LIFECYCLE_PROBE=1` | `:core:test --tests '*NativeLifecycleProbe'` | Creates one disposable Simulator and checks shutdown, reboot and reselection. It deletes only the device it created |

To check the iOS Run action end to end, make a private copy of `compiler/tests/godot_project` as
`game/` in a temporary folder. Enable `debug/file_logging/enable_file_logging` in that copy and write
the UDID of a new disposable simulator to `owned-device` in the temporary folder. For
`./gradlew runIde -PuiProbe`, set `KUMIKAE_GAME_RUN_PROBE` to that folder and
`KUMIKAE_IOS_SCENE_MODE=interpreter`. To exercise native Debug, add `actionscript/native_code=1`
under the staged iOS preset’s options and use `KUMIKAE_IOS_SCENE_MODE=native`.
The probe adds the target, chooses that simulator in Rider, presses Run, and checks the launched game's
log for the requested execution mode and the AS3 scene results.

For a paired physical test device, give the app a disposable bundle identifier and put the device’s
CoreDevice identifier in `owned-device`. Set `KUMIKAE_GAME_RUN_PROBE` and `KUMIKAE_IOS_PHYSICAL_PROBE=1`. Start with the
test app uninstalled so the report comes from this run. The probe builds and signs the game, selects
the phone in Rider and reads the AS3 scene results from its app container. Remove the app after the run.

The helper's own probes are in [its README](native/SimulatorBridge/README.md#probes).

To log the messages between the IDE and the language server, run
`./gradlew runIde -PlspTrace=<existing folder>`. The logs contain source text. Keep them out of the
repository.

## Don't

- Start a person's real IDE or change its settings. Use only the sandbox that Gradle starts.
- Erase, create, shut down or install onto a Simulator that the test didn't create.
- Claim that the plugin works in an IDE because it compiles. Say what was run, and what wasn't.
