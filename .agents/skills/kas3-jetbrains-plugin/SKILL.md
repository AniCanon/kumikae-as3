---
name: kas3-jetbrains-plugin
description: Use when changing anything under editors/jetbrains/ in kumikae-as3 (the JetBrains IDE plugin: tool windows, the iOS Simulator panel, its local service, run configurations, settings) — the fixed stack (Kotlin, Compose Swing, IntelliJ Platform Gradle Plugin, ktlint), where state, I/O and views live, how to test without an IDE, and the UI probe.
---

# Changing the JetBrains plugin

Read `AGENTS.md`, then `editors/jetbrains/README.md`, `ARCHITECTURE.md` and `CONTRIBUTING.md`. They are the
authority; this is what gets broken most.

## The stack is fixed

- Gradle through the wrapper (`./gradlew`), Kotlin with the Compose compiler plugin, the IntelliJ Platform
  Gradle Plugin, ktlint. The versions are in `build.gradle.kts`: change one only when asked to.
- **UI is Compose Swing** (`org.jetbrains.compose.swing:swing-ui`), mounted through `composePanel` in
  `ui/ComposeHost.kt`. Not hand-built Swing panels, not the UI DSL, not Jewel, not Skia Compose.
- Native Swing controls only where the IDE's own behaviour is needed (a painted picture that takes the
  mouse, a console, a table model, a file browser), mounted through `Native` / `NativeScrollPane`.
- No second dependency-injection container, no Android `ViewModel`, no coroutines scope of your own where
  `BackgroundTasks` already does the job.
- A dependency that is not already in `build.gradle.kts` is a question for a maintainer, not a line to add.

## Where things live

| Thing | Lives in | Never in |
|---|---|---|
| State the screen shows | A window controller, as `mutableStateOf` | A Swing component, a view |
| Requests, processes, files, JSON | A repository behind a typed interface, injected | A controller, a view, a composable |
| Threads | `BackgroundTasks`, injected; results delivered on the EDT | `Executors` inside a view |
| Choosing concrete services | The composition root (`*Composition`) and the tool-window factory | A default argument of a view or controller |
| Layout, text, enabled/disabled | Composables that take values and callbacks | A controller |
| The wire protocol | `protocol.md` and the core module; the command-line client depends on it | Changed without changing the client and its tests |

A controller never constructs or touches a Swing component. A view never starts work: it forwards the
user's action to the controller.

## Lifetimes

- Every worker, composition and session belongs to the window's `Disposable` and is disposed with it, on
  the EDT.
- After disposal nothing is delivered: a late result is dropped.
- A result for something that is no longer current (another device, an older generation) is dropped.
- A second action while one runs is refused with a status the user sees, not queued in silence.
- A hidden window does not poll.

## Compose over Swing

- Hoist state shared with a controller to the controller; keep small view state (a tab, an expansion) in
  the composition.
- No I/O and no visible mutation in a composable body. An effect only when the work belongs to the
  composition and needs cleanup.
- `remember` caches a value. It is not where a retained Swing control is attached: use `SwingNode`'s
  factory and update.
- Reusable composables take a `SwingModifier` as their first optional parameter and pass it to their root.
- Key a native node by the component's identity.

## Tests, without an IDE

1. Controller tests with `ManualBackgroundTasks`: run the work, deliver later, dispose in between. No
   sleeps, no real threads.
2. Repository tests against a fake transport.
3. Core tests for the protocol, geometry, processes and the session.
4. The UI probe (`./gradlew runIde -PuiProbe`): mounts the real views with fake repositories, renders them
   in light and dark, presses the real controls, and writes PNGs and `result.txt` under
   `build/reports/ui/`. Run it after any change to a view or a lifetime, and look at the pictures.

Before saying done, with the output read:

```sh
cd editors/jetbrains
./gradlew check buildPlugin
python3 -m unittest discover -s tests -v
```

## Do not

- Start a person's real IDE or change its settings: only the sandbox the Gradle plugin starts.
- Erase, create, shut down or install onto a Simulator that you did not create. Never kill a process you
  did not start; stop the ones you did.
- Load or download idb, an IDE, a JDK or any tool without a maintainer's yes.
- Claim the plugin works in an IDE because it compiles. Say what was seen, by whom, and what was not.
- Copy anything about a games console, or any personal identifier, into this repository.
