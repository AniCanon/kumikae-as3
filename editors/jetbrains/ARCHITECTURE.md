# Plugin structure

The plugin has one feature so far: the iOS Simulator tool window. It is split so
that each part can be tested without the others, and so that a later feature
(the language server client, the debugger) can reuse the same shapes.

```text
SimulatorToolWindowFactory   entry point: resolves IDE paths, builds the window
  SimulatorComposition       chooses the concrete services
    LocalSimulatorService    starts the local service, speaks its JSON over HTTP
    ApiSimulatorRepository   JSON <-> typed values, nothing else
    BackgroundWorker (x2)    one for actions, one for polling
  SimulatorWindowController  all requests and all state
  SimulatorToolWindow        mounts the view, owns the two native controls, the timer
    SimulatorView            Compose: layout, form drafts, button availability
    SimulatorScreen          native: paints the picture, mouse to gesture
```

## The service and the two clients

`core/` is a plain Kotlin module with no IDE imports. It wraps `xcrun simctl` in a
local HTTP service protected by a token, described in [protocol.md](protocol.md).
It stays a separate Gradle module because it is also the standalone
`kumikae-simulator` program that an agent can run without an IDE.

The tool window does not call that code directly. `LocalSimulatorService` starts a
session and then talks to it through `ApiClient`, the same HTTP interface that
`simulator.py` uses. The panel and an agent therefore share one selected device
and one console, and the wire protocol has exactly one implementation. The
protocol must not change without changing `protocol.md`, the Python client and
its tests together.

## Repository

`SimulatorRepository` is the typed boundary: devices, status, select, boot,
install, launch, screenshot, console page and input, in terms of
`SimulatorDevice`, `SimulatorStatus`, `SimulatorFrame`, `ConsolePage` and
`SimulatorInput`. Every call may block. `ApiSimulatorRepository` is the only
class that reads or writes protocol JSON. It also decodes the picture, so that
work happens on a worker thread. Nothing above it sees JSON, HTTP or a process.

## Controller

`SimulatorWindowController` owns every request and every piece of state the
window shows, as Compose observable state. It never constructs or touches a
Swing component, so its tests need no IDE and no window.

- **Two workers.** Actions (select, boot, install, launch, input, refresh) use one
  `BackgroundTasks`; polling uses another. A capture takes about as long as the
  polling interval, so with a single worker nearly every click would arrive
  while a poll was running.
- **One action at a time.** A second action while one runs is refused, and the
  status line says so. Nothing is queued: a tap that runs seconds late is worse
  than a tap that visibly did not happen.
- **Stale pictures.** Selecting a device, booting, launching, rotating and any
  failure advance a picture epoch. A poll remembers the epoch it started under
  and its status and picture are dropped if the epoch has moved. A picture is
  also dropped when it is for a device other than the selected one. This epoch
  is the controller's own; the `generation` inside a frame is the service's
  geometry counter, which input must quote back.
- **Console.** The log cursor advances with every page, independent of the
  epoch, so no output is lost or shown twice. A page that lost history is
  marked. The text is capped at 128 Ki characters.
- **Failures** show their message, clear the picture and are written to the
  console once. They are never retried automatically: a lost reply does not
  prove that a boot or launch did nothing.
- **Visibility.** A hidden window does not poll. The service keeps running and
  keeps collecting console output for an agent.
- **Closing.** Disposal disposes both workers and closes the repository. A
  result that is delivered afterwards changes nothing. `LocalSimulatorService`
  hands the slow part of closing to its own thread, because closing waits for a
  running command and a boot can take two minutes. A session that finishes
  starting after the window closed is closed at once.

## View

`SimulatorToolWindow` mounts `SimulatorView` through `ui/ComposeHost.kt`, which
ties the composition to the controller's disposal on the UI thread. Compose owns
the layout, the draft values of the form fields and whether buttons are enabled.
Two controls stay native and are mounted through `Native` and
`NativeScrollPane`: `SimulatorScreen`, which paints the fitted picture and turns
mouse presses into gestures, and the console text area. The view pushes the
picture and the console text into them when the state changes.

The view schedules polling with a timer that runs only while the component is
showing, and tells the controller when it is shown or hidden.

## Compose Swing

Compose Swing UI is pinned to 0.1.0-alpha06 from JetBrains' Compose Maven
repository; the Compose compiler matches the Kotlin compiler. It is Compose
Runtime driving Swing components, with no separate renderer. Because the
library is an alpha, the plugin accepts only the platform build it has been
tried on. `ui/ComposeUi.kt` holds the few shared composables: `Column`,
`FormRow`, `Note`, `ReadOnlyField`, `Native` and `NativeScrollPane`.

## Validation

- `core` tests cover the protocol, geometry, device parsing, commands, the log
  buffer, process timeouts, session cleanup and HTTP authentication.
- Repository tests run `ApiSimulatorRepository` against a fake JSON transport.
- Controller tests use `ManualBackgroundTasks`, a scheduler that separates
  running work from delivering its result, to reproduce stale pictures, late
  delivery after disposal and overlapping actions without sleeping.
- `LocalSimulatorServiceTest` starts the real local service in a temporary
  directory. It needs no Simulator.
- The UI probe (`./gradlew runIde -PuiProbe`) is a second plugin that exists only
  in the sandbox. It mounts the production view with a synthetic repository,
  presses the real buttons, and renders the light and dark themes to pictures.
  It proves layout and wiring, not behaviour against a real Simulator.
- A run against a real Simulator goes through the standalone service and
  `simulator.py`; see the [results](../../docs/results/2026-11-jetbrains-simulator.md).

See [CONTRIBUTING.md](CONTRIBUTING.md) for the Kotlin and Compose conventions.
