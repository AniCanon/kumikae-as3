# Plugin structure

The plugin separates language support, project/run tools and the iOS Simulator panel. Compiler
semantics stay in `kas3c`; editor interaction stays in the IDE. AS3 debugging remains planned.

## Language support

`As3LspSupportProvider` starts one project-wide native LSP client using the located compiler.
Completion, signature help, diagnostics and navigation come from that server. Standard LSP edits and
snippets go through the platform lookup pipeline; the plugin does not override Tab globally.
Rider's public notification-handler hook wraps diagnostic version validation and native cache insertion
in one read action. Rider sends incremental changes from `beforeDocumentChange`; without this boundary,
a reply can cache old offsets under the new document version while the write is still in progress.
The short, synchronous cache publication is not cancellable/retried because it has side effects.
It checks project disposal inside the read action and adds no worker, queue or diagnostic cache.

AS3 language-block templates use that same client pipeline. Their ordinary source text and tab-stop
spans originate in compiler queries; no template catalogue or snippet parser is duplicated in Kotlin.

`As3ParserDefinition` creates token-only PSI, sufficient for native editor integration. `As3Lexer`
recognizes code, punctuation and non-code text, retaining lexical state at token boundaries for
incremental highlighting. `As3EditorHighlighterProvider` combines a structural highlighter with the
existing TextMate highlighter. Its iterator intersects their intervals: AS3 supplies token kinds and
TextMate supplies color attributes. Both receive the same document changes and belong to the editor;
no document text or per-edit token history is copied into a separate cache.

`As3BraceMatcher` and `As3QuoteHandler` use those structural tokens. Quotes use the whole literal's
boundaries even when TextMate splits it into escape/color segments. `As3LineIndentProvider` computes
local indentation from existing tokens and project settings. `As3EnterHandler` applies that provider
after native Enter because token-only PSI has no full formatter. None of these handlers reads files,
starts processes or waits for the server.

The sandbox `UiProbeLanguage` exercises native typing actions. `UiProbeCompletion` exercises the
native lookup against the bundled server in a disposable project. The compiler tests separately cover
scope/type semantics, wire capabilities, Unicode ranges and cache invalidation. See the
[results](../../docs/results/2026-11-completion.md) for observed coverage and remaining limitations.
`UiProbeFocus` scopes the platform's window-deactivation test control to those background sandbox runs
and restores it on exit. It prevents desktop focus changes from dismissing the test's live lookup;
the production plugin contains no focus override.

## Simulator composition

```text
SimulatorToolWindowFactory   entry point: resolves IDE paths, builds the window
  SimulatorComposition       chooses the concrete services
    LocalSimulatorService    starts the shared session; commands over HTTP, binary pictures directly
    ApiSimulatorRepository   typed commands, binary pictures and decoded frames
    BackgroundWorker (x4)    actions, polling, stream startup, continuous touch
  SimulatorWindowController  all requests and all state
  SimulatorToolWindow        mounts the view, owns the two native controls and visibility-scoped timers
    SimulatorView            Compose: a tab for the list and one per running device
    DeviceListPage           every simulator, each with Boot or Shut down
    DevicePage               tool bar, framed device with zoom, floating bar, console drawer
    SimulatorScreen          native: paints the picture, mouse to gesture
```

## The service and the two clients

`core/` is a plain Kotlin module with no IDE imports. It wraps `xcrun simctl` in a
local HTTP service protected by a token, described in [protocol.md](protocol.md).
It stays a separate Gradle module because it is also the standalone
`kumikae-simulator` program that an agent can run without an IDE.

The tool window sends commands through `ApiClient`, the same authenticated HTTP
interface that `simulator.py` uses. Binary screenshots and live pictures use the
same `LocalSession` directly, avoiding base64, JSON and loopback copies for images.
There is still one selection, one geometry authority and one console. The agent
protocol is unchanged; no extra unauthenticated image endpoint is exposed.

`FallbackInput` chooses the bundled native helper, installed idb, then public screenshots.
`NativeInput` owns a helper and a private temporary frame slot. `NativeBridgeConnection`
validates readiness, calibrates clocks and serializes small control messages.
`NativeFrameReader` maps the shared slot and copies a new sequence into a matching
`BufferedImage` under a shared file lock. `NativeFrameFeed` does that work off the EDT
and exposes one replaceable latest frame. Closing signals the worker; it reaps the
helper and removes the mapping. No image crosses JSON and no image copy runs on the EDT.

The Swift library has device, frame source, frame sink and input protocols. Its
`AppleSimulator` actor alone knows private Apple names, with separate extensions
for the input connection and messages. It runtime-loads CoreSimulator and checks
Objective-C signatures and private XPC export presence. Runtime capabilities
are the Xcode compatibility gate; an empty-by-default build deny list records
reasons for any known incompatibilities. A failed empty startup barrier is
retryable after reboot, without replaying input. Capture polls the IOSurface seed
on an 8 ms budget including copying and publication, and only copies changes.
Its pixel buffer is reused; the JVM allocates its own image outside the slot lock
and revalidates the header before copying. DTUHID sends down/move/up and waits for
a separate guest-service barrier reply. Rotation uses a separate guest orientation service;
its reply precedes feed closure. Oriented pixels and inverse touch coordinates
share one transform. External orientation changes revoke the feed. The actor is
single-use; reconnection creates another helper. Stdin EOF cancels active work and
releases input and device references.
See [the native package](native/SimulatorBridge/README.md) for the complete private
surface and [protocol.md](protocol.md) for the shared frame layout.

`IdbDevices` is the fallback. It owns a private companion socket and a persistent `idb shell` for the
selected device. Input waits for the shell's acknowledgment, never replays a failed
command, and rebuilds a failed connection on a later request. `OwnedStreamProcesses`
reaps only its own children; session shutdown waits for forced cleanup if needed.
A JVM shutdown hook also waits for those children if the IDE exits while project
disposal is still closing the session. Normal close unregisters the hook.
`LiveFrames` reads dynamic MJPEG at scale 0.33 and quality 0.6 on one worker and
decodes on another. Both handoffs are single replaceable slots, not queues. Equal
JPEGs are discarded before decoding. The parser understands segment lengths,
escaped entropy bytes, truncated streams and a maximum encoded frame size.

## Repository

`SimulatorRepository` is the typed boundary: devices, status, select, boot,
install, launch, screenshot, console page and input, in terms of
`SimulatorDevice`, `SimulatorStatus`, `SimulatorFrame`, `ConsolePage` and
`SimulatorInput`. Requests may block; reading a live frame slot never blocks or
decodes. `ApiSimulatorRepository` is the only panel class that reads or writes
protocol JSON. It decodes fallback snapshots on a worker; the core copies native frames or decodes
fallback live JPEGs. Nothing above the repository sees JSON, HTTP or a process.

## Controller

`SimulatorWindowController` owns every request and every piece of state the
window shows, as Compose observable state. It never constructs or touches a
Swing component, so its tests need no IDE and no window.

- **Independent workers.** Actions and polling each use a `BackgroundTasks`;
  opening a stream uses a third, and continuous touch a fourth. Live frame copying
  and decoding belong to the service. `TouchController` preserves press/release
  order and replaces unsent moves with the newest point.
  `PictureStreamController`, owned by the window controller, manages the stream
  lifetime and 500 ms to 8 s capped retry backoff. A stopped stream clears the
  picture and reports its retry. Input does not stop or wait for live video.
- **One action at a time.** A second action while one runs is refused, and the
  status line says so. Nothing is queued: a tap that runs seconds late is worse
  than a tap that visibly did not happen.
- **Stale pictures.** Selecting a device, booting, launching, rotating and any
  failure advance a picture epoch. A poll remembers the epoch it started under
  and its status and picture are dropped if the epoch has moved. A picture is
  also dropped when it is for a device other than the selected one. This epoch
  is the controller's own; the `generation` inside a frame is the service's
  geometry counter, which input must quote back.
- **Selection.** The device list and `selected` always agree, even when a
  selection is refused because an action is running: an action must never name
  a device the user cannot see. The service's own choice comes back with the
  running action's result or the next poll, and the service refuses any request
  for a device it has not selected. A poll's status repeats the device as it
  was when selected, so a poll follows only its identity; the replies to select
  and boot, and Refresh, carry the newer device state.
- **Console.** The log cursor advances with every page, independent of the
  epoch, so no output is lost or shown twice. A page that lost history is
  marked. The text is capped at 128 Ki characters.
- **Action failures** show their message, clear the picture and are written to the
  console once. They are never retried automatically: a lost reply does not
  prove that a boot or launch did nothing.
- **Visibility.** A hidden window does not poll or stream. The list tab stops
  pictures too. Late stream openings are closed, and late frames are discarded.
  The service keeps collecting console output for an agent.
- **Closing.** Disposal disposes all workers and closes the repository. A
  result that is delivered afterwards changes nothing. `LocalSimulatorService`
  hands the slow part of closing to its own thread, because closing waits for a
  running command and a boot can take two minutes. A session that finishes
  starting after the window closed is closed at once.

## View

`SimulatorToolWindow` mounts `SimulatorView` through `ui/ComposeHost.kt`, which
ties the composition to the controller's disposal on the UI thread. Compose owns
the layout and whether buttons are enabled. What the person set in the panel itself
(the app to launch, whether the console shows) is `SimulatorPanelState`. Dialogs,
menus and the clipboard are behind `SimulatorPrompts`, injected, so that the view
opens nothing itself and the probe can answer for a person.
Two controls stay native and are mounted through `Native` and
`NativeScrollPane`: `SimulatorScreen`, which paints the fitted picture and turns
mouse presses into gestures, and the console text area. The view pushes the
picture and the console text into them when the state changes.

The view schedules metadata polling at 500 ms and checks the newest decoded frame
slot every 8 ms while showing. An empty slot changes no state and causes no repaint.
The controller owns picture state. Normal frame replacement repaints only
`DeviceFrame.screen`; geometry and layout changes repaint the whole native control.
`DeviceBodyCache` retains one decoration image at the current display density;
geometry or density changes replace it, so live frames do not rasterize the static bezel.
The cache is capped at 16 MiB; larger zoomed views paint decoration directly.
Clearing a screen also releases its last-presented frame used by paint instrumentation.
A settled native screen still has cheap seed polls, but no pixel copy or repaint.
The idb fallback asks its source for changes dynamically.

## Compose Swing

Compose Swing UI is pinned to 0.1.0-alpha06 from JetBrains' Compose Maven
repository; the Compose compiler matches the Kotlin compiler. It is Compose
Runtime driving Swing components, with no separate renderer. Because the
library is an alpha, the plugin limits its platform range to the tested IDEA and Rider endpoints;
builds between them are not individually verified. `ui/ComposeUi.kt` holds the few shared composables (`IconButton`,
`TitleButton`, `Pill`, `StatusLine`, `Native`, `NativeScrollPane`) and
`ui/Controls.kt` the small painted controls behind them. `DeviceFrame` works out
where the device and its screen sit in the view and draws the body around the
picture; a pointer position is measured against the screen it reports.

## The service under load

Boot, shutdown and install can take two minutes. They run outside the session's lock, one at a
time (a second is refused with `BUSY`), so status, the list, the log and pictures still answer.
Screenshot capture and input acknowledgment also run outside the geometry lock.
Input and stream startup are admitted under that lock; competing input and selection
are refused until they complete. Status remains available while a helper starts. Stream publication continues during swipes. Native and
scaled geometry share a generation, and stream failures invalidate that generation.
Reading the list replaces what the session knows of the selected device, and the panel reads it
every sixth poll: a device started or stopped elsewhere shows within about three seconds. A
device that is not running is not asked for its picture.

## Validation

- `core` tests cover the protocol, geometry, device parsing, commands, the log
  buffer, process timeouts, session cleanup and HTTP authentication.
- Repository tests run `ApiSimulatorRepository` against a fake JSON transport.
- Controller tests use `ManualBackgroundTasks`, a scheduler that separates
  running work from delivering its result, to reproduce stale pictures, late
  delivery after disposal and overlapping actions without sleeping.
- `LocalSimulatorServiceTest` starts the real local service in a temporary
  directory. It needs no Simulator.
- `./gradlew verifyPlugin` checks the built plugin against the installed IDE.
- The UI probe (`./gradlew runIde -PuiProbe`) is a second plugin that exists only
  in the sandbox. It mounts the production view with a synthetic repository,
  presses the real buttons, and renders the light and dark themes to pictures.
  It proves layout and wiring, not behaviour against a real Simulator.
- A run against a real Simulator goes through the standalone service and
  `simulator.py`; see the [results](../../docs/results/2026-11-jetbrains-simulator.md).

See [CONTRIBUTING.md](CONTRIBUTING.md) for the Kotlin and Compose conventions.

Native Unicode input uses shared string validation, copies unchanged UTF-8 through an
injected guest pasteboard, then sends the native paste shortcut. A failed copy sends
no shortcut. The command runner owns separate pipe workers for each child and joins
them when that child ends; blocking pipe work never occupies the shared common pool.

Language-server launch discovery and declaration refresh belong to `As3ProjectLanguageService`.
Its disposable alarm drives one bounded refresh controller per project, instead of a sleeping worker
for every opened file. Disposal cancels scheduled work; callbacks delivered after disposal do nothing.
The missing-compiler warning is also project-owned. `DeclarationRefreshTest` checks scheduling,
success, retry bounds and disposal with injected callbacks and no real threads.

The local lexer carries expression/control/brace context in its restart state. Deep stacks use a
prefix replay sentinel instead of keeping old document states. The UI probe checks auto-import and
override additional edits through native completion acceptance and one Undo. Its plain-text-only mode
(`KUMIKAE_COMPLETION_CONTROL_PROBE=1`) opens the same temporary projects without an AS3 session, to
separate platform lifecycle warnings from completion activity.

## Language-client adapters

The base descriptor contains local typing, syntax and project tools. Optional descriptors load Rider's
native integration only in Rider, and LSP4IJ's server, parameter help and outline only in IDEA. The
IDEA condition is the outer dependency and LSP4IJ is nested inside it. Rider therefore has no LSP4IJ
class-loader dependency, even when that plugin is installed. This keeps protocol classes and editor
handlers isolated. No native LSP class is loaded on the IDEA path.

`As3ServerLaunch` owns compiler launch arguments and initialization options. Both clients use the same
project language service for compiler discovery and bounded declaration refresh. LSP4IJ owns its
connection provider and process shutdown; there is no additional worker or response cache.
`As3Lsp4ijCompletion` uses LSP4IJ's public completion feature extension to refresh suggestions after
prefix changes. Its lookup data stores the request-time caret and server item. A public lookup listener
selects Enter's insert range or Tab's replacement range before insertion and copies positions before the
client adjusts them. The lookup owns that data and listener; there is no project or global item cache.
All snippet insertion, additional edits, cancellation and Undo remain in the client. Initialization
advertises the diagnostic-version filtering that LSP4IJ already implements, so the server can tag its
publications and the client can reject mismatched versions.

An IDEA project without enabled LSP4IJ receives one actionable notification when an AS3 file opens.
That listener imports no optional client classes, so missing dependencies cannot break local typing.
