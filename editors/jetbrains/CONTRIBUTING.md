# Kotlin and Compose conventions

Follow [JetBrains' Kotlin coding conventions](https://kotlinlang.org/docs/coding-conventions.html).
The `.editorconfig` here selects IntelliJ's Kotlin style, four-space indentation,
120-column lines and trailing commas. Prefer explicit imports, immutable local
values, named arguments for ambiguous calls and related declarations grouped by
responsibility. UI-emitting `@Composable` functions use PascalCase; other
functions use camelCase.

Run `./gradlew ktlintFormat` to format, and `./gradlew check` to run the style
check and the tests. `buildPlugin` also requires the style check. It covers
production Kotlin, tests and Gradle Kotlin scripts, in both modules. Do not add
blanket suppressions or a baseline to get a new finding past it.

Keep files small and single-purpose: aim for under 300 lines. No banner
comments. A comment says why, not what.

## Tests first

Write the test, watch it fail for the reason you expect, then make it pass.
State changes belong in `SimulatorWindowControllerTest`, with
`ManualBackgroundTasks` deciding when work runs and when its result arrives.
JSON mapping belongs in `ApiSimulatorRepositoryTest`. Anything in `core` is
tested there with JUnit 4; both modules run on the JUnit Platform, so either
style of test is picked up. Read the test count after a build change: a missing
engine gives a green build that ran nothing.

The wire protocol is shared with `simulator.py`. Keep its Python client tests
green, and change `protocol.md`, the service and both clients together.

## Dependency injection

Resolve IDE services and paths at plugin entry points, then pass dependencies to
ordinary controllers and views. `SimulatorToolWindowFactory` is the entry point;
`SimulatorComposition` chooses the concrete services. Do not create a live
repository in a default argument of a view or controller. Views receive a
controller and forward user actions; controllers own requests and state
changes. Put file, process and IDE-service access behind injected interfaces.
Workers belong to the window that disposes them.

IDE light services are a platform exception: JetBrains [does not support
constructor injection of other services](https://plugins.jetbrains.com/docs/intellij/plugin-services.html#constructor).
Resolve those when a method needs them. Do not introduce a second DI container
or look up services inside composables.

## Compose Runtime over Swing

Apply Google's [state ownership](https://developer.android.com/develop/ui/compose/state-hoisting),
[composition model](https://developer.android.com/develop/ui/compose/mental-model) and
[side-effect guidance](https://developer.android.com/develop/ui/compose/side-effects)
to Compose Swing and the IDE's lifecycle:

- Keep a draft form value at the nearest composition that uses it. State that
  the controller shares with the view lives in the controller as observable
  state; pass values and callbacks to composables. Text, selections and button
  availability belong in observable state, not in retained Swing controls.
- Keep composable bodies free of I/O and of changes visible outside the
  composition. Requests stay in the controller. Use an effect only when the
  work belongs to the composition, such as pushing a new picture into the
  native screen.
- `remember` may cache a value; it is not an effect and not a safe place to
  attach a retained Swing control to a new parent. Use `SwingNode`'s factory
  and update phases for that. `NativeScrollPane` sets its viewport there.
- Key retained native nodes by component identity, as `Native` does, so that
  replacing the control replaces the node. Apply changing text and sizes
  through `SwingNode.update`, not only in the factory.
- A reusable composable takes a default `SwingModifier` as its first optional
  parameter and forwards it to its root. Do not apply a caller's modifier to
  several children.
- Dispose compositions through their owning disposable on the UI thread;
  `composePanel` does this.
- Do not mark mutable Swing components `@Stable` or `@Immutable`, add
  `derivedStateOf` without a measured need, or bring Android `ViewModel` or
  Activity APIs into this plugin.

Run `./gradlew runIde -PuiProbe` after any change to presentation or lifecycle,
and look at the pictures in `build/reports/ui/`. Text that is cut off,
controls that overlap and unreadable colours are defects.

For other languages and documentation, follow the repository's own guides.

The sandbox probe also uses the installed AS3 editor handlers for pairing and indentation, then opens
a temporary project to exercise native LSP lookup and Tab/Enter acceptance. `language.txt`,
`typing-times.txt`, `completion-*.txt` and `completion-heap.txt` accompany the light/dark PNGs. Timings
include IDE scheduling; retained heap samples include the whole IDE and are not a leak proof. The
probe plugin is excluded from the distributable ZIP.

The native lookup probe supplies an activation event to its sandbox frame when launched in the
background; it does not steal desktop focus or test OS activation. It trusts only its generated
fixture path and restores the sandbox's trust settings when finished.

For a lifecycle control, run `KUMIKAE_COMPLETION_CONTROL_PROBE=1 ./gradlew --offline runIde -PuiProbe`.
It opens plain-text files, confirms each project closes and is disposed, and records the same heap
samples. Save the reports before running another probe: both use `build/reports/ui/`. The control and
AS3 probes both reproduce the pinned Rider's `HighlightingPanel` disposal warning. The paired
ten-minute runs retain all ten disposed projects; see the
[results](../../docs/results/2026-11-completion-followup.md#ide-lifecycle-investigation). The AS3 probe also verifies generated imports and override
signatures are accepted and undone atomically.

Add `KUMIKAE_COMPLETION_RETENTION_PROBE=1` for ten projects across ten minutes, with live-GC histograms
and weak closed-project counts. Combine it with the control flag for the same plain-text workload.
The test plugin cancels the pinned Rider's evaluation feedback survey and IDEA's first-run UI onboarding
dialog, which otherwise block project opening. It matches those two exact classes and uses Cancel;
license dialogs and production IDE settings are untouched. Callback failures fail the probe.


## Language-client compatibility checks

After changing a client adapter, run the following sequentially with installed JDK 25, Rider and IDEA.
The build resolves the declared LSP4IJ 0.21.0 dependency; use `--offline` after caching it. IDE binaries
come from `RIDER_HOME` and `IDEA_HOME` (or the application defaults in the build), never a download task.

```sh
./gradlew --offline check buildPlugin verifyPlugin
./gradlew --offline runIdeaWithoutLsp4ij -PuiProbe
./gradlew --offline runIdea -PuiProbe
./gradlew --offline runIde -PuiProbe
./gradlew --offline runRiderWithoutLsp4ij -PuiProbe
```

`runIdea` uses LSP4IJ; `runIde` checks Rider's native client with LSP4IJ installed but no competing AS3
server or handlers. The two `WithoutLsp4ij` tasks disable that plugin in separate sandboxes. IDEA then
checks the install notification and local editing; Rider still runs the complete semantic probe.
Reports are under `build/reports/ui-idea`, `ui`, `ui-runIdeaWithoutLsp4ij` and
`ui-runRiderWithoutLsp4ij`, respectively. Preserve them before another run.

Each semantic run opens and disposes three projects. It checks Enter/Tab, continued typing and
Backspace, snippet navigation, prefix refresh, generated edits and Undo, parameter help, go to
declaration and outline. The probe waits for each client's indexing gate before invoking the first
completion; IDEA checks LSP4IJ's readiness in the same UI-thread turn as invocation. Parameter help
waits for the live template to finish and activates the sandbox before displaying its hint. No fixed
delay stands in for readiness. It also mounts the panel and writes light/dark images for inspection.
