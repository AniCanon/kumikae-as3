# The iOS Simulator in JetBrains IDEs

Date: 2026-11-14. First implementation: `editors/jetbrains/`.

## Decision

Build the macOS panel now, using Xcode's `simctl` for pictures, device selection,
boot, install and launch. Use one local service for both the panel and an agent.
Start with a snapshot view. It is useful for checking screens and logs. It is not
a smooth game preview or an AS3 debugger.

**Input needs an owner decision.** Implement an optional adapter for Meta's idb,
but leave it off. It uses private Apple interfaces. It needs an explicitly supplied
executable, an installed companion, and an explicit acknowledgement of that risk.
Do not download, bundle or start installing it. Until the owner chooses it and it
passes a real test on Xcode 27, the panel is a viewer with launch and logs. Use
Apple's Simulator window for input. A claim that the whole interactive panel works
would be premature.

The alternative is a native helper using macOS Accessibility and mouse events.
Those APIs are public, but driving a second app needs permission, window geometry,
focus handling and protection against touching the wrong window. It also brings
the Simulator window in front of the code. Do not hide those costs behind a tap
button. Leave that alternative for a separate, visible choice.

## Evidence from this Mac

Read the project README, editor guide, roadmap stage 6, C++ guidelines, iOS result,
`build_ios.sh` and `ios_test.tscn`. No compiler or template work belongs here.

Commands run on 2026-11-14:

- `xcodebuild -version`: Xcode 27.0, build 27A266a.
- `simctl list -j devices available`: one booted iPhone 17 Pro on iOS 26.4.
  The iOS 27.0 device list was empty. Do not store its identifier or local name.
- `simctl help`, `help io`, `help launch` and `help bootstatus`.
  No tap, swipe, text injection, home or rotation command is listed.
- Ten sequential screenshots to a real file, of the home screen. PNG median
  630.9 ms, range 575.7–731.3 ms, about 2.9 MB. JPEG median 524.05 ms, range
  502.2–861.7 ms, about 462 KB. These are process start-to-exit times, not measured
  touch-to-display delay. The JPEG was opened and the home screen was visible.
- This Xcode advertises `screenshot -` as stdout, but wrote a file named `-` and
  returned zero bytes. Use a temporary file and remove it after reading.
- `recordVideo --force --codec=h264 /dev/stdout`, with a pipe attached, returned
  no video and AVFoundation error -11823. Without `--force` it refused the existing
  destination. This experiment rules out that pipe on this machine; it does not
  prove that every URL or future Xcode behaves the same.
- Rider 2026.2.3.1 is installed. Gradle 9.4.1 and 9.8.1 distributions, Kotlin
  2.4.10, IntelliJ Platform Gradle Plugin 2.20.0, Gson and JUnit exist in caches.
  Presence in a cache is not yet proof that a full offline build resolves.
- `idb` was not on PATH. Computer Use refused access to Device Hub. No input
  backend or IDE panel has been visually tested at design time.

The shell sandbox cannot reach CoreSimulator. The screenshot tests used an
approved unsandboxed process. No dependency was downloaded.

## Getting the screen

| Option | Support and cost | Frame rate and delay | Xcode update risk |
|---|---|---|---|
| `simctl io screenshot` loop | Xcode's documented command. No extra software or Screen Recording grant. Repeated process start, full image encode and decode. | Measured capture above. Request at most 2 fps, with one request in flight. Expect about 1–2 fps here. A change can wait one capture interval plus capture and UI work. End-to-end delay is not measured. | Command and output details can change. The stdout discrepancy already needs a workaround. |
| `simctl io recordVideo` to a pipe | Documented movie recording, not a documented frame stream. Needs decoding, framing and buffering in the client. | No working pipe observed here. Do not promise a frame rate or low latency. A finalized movie is not a live panel. | AVFoundation/container behaviour and output support can change. |
| Capture the Simulator host window | ScreenCaptureKit is a public macOS API. Needs a native helper and Screen Recording permission. Must locate and crop the right window, including its bezel and controls. | Apple's sample configures 60 fps. This is a possible target, not our measurement. Encoding, transport, decoding and Swing painting add delay. | Less framework risk, but window layout and host app identity change. Current Apple docs call the host Device Hub. |
| CoreSimulator / SimulatorKit directly | Private interfaces. Can access display surfaces and send input without a visible host window. Needs native code and reverse-engineered contracts. | Could use display updates instead of polling. No local measurement and no promise of 60 fps. | High. Class names, message layouts and services change. No Apple compatibility contract. |
| idb / FBSimulatorControl | Third-party software with private Apple dependencies. Client and companion must match. Adds installation, licence review, version pinning and maintenance. | Offers video and direct input. Not installed or measured here. | Its own documentation describes the Xcode 27 move from Indigo to DTUHID. A random older release is not enough. |

Sources read: Apple's [ScreenCaptureKit sample](https://developer.apple.com/documentation/screencapturekit/capturing-screen-content-in-macos),
Meta's [FBSimulatorControl explanation](https://github.com/meta/idb/blob/main/FBSimulatorControl/README.md),
and Apple's [Device Hub interaction guide](https://developer.apple.com/documentation/xcode/interacting-with-your-app-in-the-ios-or-ipados-simulator).
The local `simctl` help is the authority for the installed Xcode commands.

Use JPEG for the live view, PNG for an agent's saved screenshot. The view keeps
aspect ratio and adds empty margins. Pointer coordinates in those margins are
ignored. No screenshots are written to the project permanently. Stop polling when
the panel is hidden. Show capture time and errors, and clear a failed or changed
device's image so an old picture does not look live.

## Input

The optional adapter invokes `idb ui tap`, `ui swipe`, `ui text`, `ui button HOME`,
`ui button LOCK`, and `ui rotate` with an explicit device identifier on every call.
Use an argument list, never a shell command string. Text is one argument, including
spaces and punctuation. Restrict the first version to printable ASCII: idb's text
path is keyboard input, not a general Unicode paste service.

Read the current CLI's [touch command](https://raw.githubusercontent.com/facebook/idb/main/idb/cli/commands/tap.py),
[input commands](https://raw.githubusercontent.com/facebook/idb/main/idb/cli/commands/hid.py),
and [types](https://raw.githubusercontent.com/facebook/idb/main/idb/common/types.py).
The adapter contract is checked by command-building tests. Actual input remains
unverified until a compatible idb client and companion are available.

The local protocol uses screenshot pixels, top left as origin. Each gesture names
the selected device and the dimensions of the picture it used. Reject stale
geometry, out-of-bounds coordinates and non-finite numbers. idb takes points, not
pixels. Require an explicit pixels-per-point scale when enabling it; do not infer
scale from an iPhone model name. After rotation discard the old geometry and take
a new screenshot. Only a single main display is in scope. No multi-touch or live
finger tracking: a drag is sent as one swipe when the mouse is released.

XCUITest is Apple's supported way to synthesize test input. It needs a test runner
and its lifecycle, rather than being a general `simctl tap` command. It is a future
alternative for repeatable app tests. macOS Accessibility can also operate the
host window and its menus, with the focus and permission costs described above.
`simctl pbcopy` only sets the clipboard; it does not type into a focused field.
Screen power is not the same operation as pressing the lock button.

## Devices, app and logs

List available iOS devices from `simctl list -j devices available`. Display name,
runtime and state. The user selects one; do not use the ambiguous `booted` alias.
Refresh the list on demand. Boot with `simctl bootstatus <id> -b`, with a bounded
wait. Do not create, erase, rename or shut down devices automatically.

Install an explicit absolute `.app` path with `simctl install`. Default bundle id:
`org.kumikae.as3test`. The expected app is `kas3test.app`, built from the Godot iOS
export. `build_ios.sh` builds the extension slices, not that app. The custom Godot
template is being built elsewhere. Do not build it here or claim this app runs.

Launch with `simctl launch --terminate-running-process --stdout=<file>
--stderr=<file> <id> <bundle>`. The explicit launch action restarts that app. Read
those two files through a bounded log buffer. Report which stream each chunk came
from and return a cursor, so an agent can ask for only new output. Stop collecting
when changing device, relaunching or closing the session. Bound both retained
memory and file growth; end collection visibly if a limit is reached.

Console output is not every iOS log. Unified logging is a different source and is
not in the first version. A quiet console is not proof of success. The earlier
phone test needed `user://as3test.txt`. Provide a separate report operation that
reads only `Documents/as3test.txt` in the selected app's data container, found by
`simctl get_app_container`. Verify that location when the real app is available.

## One local interface

Kotlin core code owns a session: selected device, image geometry, input adapter,
launch and log collector. It has no IntelliJ imports. A small HTTP service binds
only `127.0.0.1`, on an assigned port. It requires a random bearer token. An endpoint
file with owner-only permissions holds the port and token. Do not log or commit
it. The IDE publishes its endpoint path in the panel. A standalone entry point
can run the same service without an IDE, for tests and agents.

The panel and the Python standard-library CLI both call this service. Neither
builds Simulator commands. JSON requests carry `version: 1`, `id`, `method` and
`params`; replies echo `id` and contain either `result` or a named `error`.
Document exact fields and examples beside the CLI. Methods: `devices`, `select`,
`status`, `boot`, `screenshot`, `tap`, `swipe`, `text`, `button`, `rotate`, `install`,
`launch`, `log`, and `report`. Configuration of private input is local startup
configuration, not something an unauthenticated remote caller can switch on.

Serialize session operations, including agent and mouse gestures. Use timeouts,
bounded request sizes and bounded queues. No arbitrary shell execution or general
file-reading method. Reject unknown methods, fields, wrong versions and bad types
before starting a process. Errors must distinguish bad requests, missing input
support, process failures and closed sessions. A timeout must stop its process.
Closing the project stops the server, polling and log readers, and removes the
endpoint and temporary files. It leaves the Simulator and launched app running.

## Plugin fit and scope

One plugin id for Kumikae AS3, using Kotlin and the IntelliJ Platform Gradle Plugin.
Depend only on platform modules, not Rider-only classes. A tool-window factory
checks macOS before creating content. Windows and Linux show no Simulator panel
and start no service. This code does not replace the current LSP4IJ setup.
The later language-server client and debug adapter belong in this same plugin,
beside this feature. A debug adapter connects the IDE to AS3 breakpoints and
stepping; pictures, input and process logs do not provide that capability.

First version: one selected device, snapshot view, optional idb input, boot,
explicit install and launch, bounded console log, fixed test report, agent CLI,
and tests for parsing, protocol, coordinates, command building and lifecycle.
Build offline against installed Rider where possible. No automatic downloads.

Not in this version: smooth video, audio forwarding, AS3 breakpoints, stepping,
hot reload, export builds, signing, physical iPhones, multiple displays, multi-touch,
remote access, Android, or automatic idb installation. Keep input behind an
interface and the protocol independent of IntelliJ; an Android backend can be
added later without copying the panel or the agent interface.

## Checks and remaining decisions

Write tests first and observe a failure before adding each testable behaviour.
Use fake processes for protocol and command tests. Exercise real list, capture,
boot and built-in app launch/log on this Mac if access permits. Try an offline
Gradle test and plugin build. Record exact results in the implementation README.
An IDE screenshot is needed before claiming the panel was seen in Rider.

The owner can change: snapshot speed versus native streaming work; whether to
accept idb's private interfaces; the idb version and distribution after a real
compatibility test; using Accessibility or XCUITest instead; the default app and
the scope of log collection. No private-interface adoption or download is implied
by this document. Full embedded interaction remains conditional on that choice.
