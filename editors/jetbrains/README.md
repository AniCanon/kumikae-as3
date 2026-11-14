# Kumikae AS3 for JetBrains IDEs

The first piece is an iOS Simulator panel. The owner has tried it in Rider and
reported that the snapshot preview was slow. The live-picture implementation and
its measurements are described in the results below.

| Checked | By | Result |
|---|---|---|
| Compilation, style check and plugin zip | Automated build, 2026-11-14 | `./gradlew check buildPlugin` succeeds. |
| Service tests (`core`) | Automated build | 46 pass. |
| Panel tests (repository, controller, service adapter) | Automated build | 75 pass. |
| Plugin verifier against the installed Rider | `./gradlew verifyPlugin` | Compatible with RD-262.10968.170. |
| Agent client tests | `python3 -m unittest` | 7 pass. |
| Service and agent client against a booted Simulator | Run by hand through `simulator.py` | Devices, select, launch of `org.kumikae.as3test`, a 2622x1206 PNG screenshot, console log, and the test report "14 passed, 0 failed". |
| The panel's own repository against a booted Simulator | One throwaway test, read-only | Select, a decoded 1206x2622 JPEG picture in 403 ms, an empty console page and status. |
| Panel layout and buttons | UI probe in a sandbox Rider, with a synthetic service | Light and dark pictures under `build/reports/ui/`; Refresh, Boot and Launch pressed through the real buttons. |
| The panel in a Rider window, by a person | Owner | Snapshot version was slow; live version still needs the owner's acceptance. |
| Live panel against a booted Simulator | Automated visible sandbox probe | 25.30 painted frames/s; warm mouse release to input acknowledgment 88–104 ms; no new paints while still or hidden. Cold first-touch handoff remains about 489 ms. |
| Ten-minute stream and input | Production service in a small harness | Mean 26.42 frames/s consumed; retained heap 5.655 MiB at one minute and 5.706 MiB at ten minutes. Owned children exited. This was not a ten-minute Rider heap measurement. |
| idb input against a booted Simulator | `simulator.py` and the sandbox panel probe | Density 3; Home, taps and Settings swipes observed. Earlier text, Lock and Rotate commands were accepted, without a separate visual confirmation. |

The design (`docs/superpowers/specs/2026-11-14-jetbrains-ios-simulator-design.md`)
compares screen and input options. The [results](../../docs/results/2026-11-jetbrains-simulator.md)
give the detail behind this table. [ARCHITECTURE.md](ARCHITECTURE.md) describes the
parts and [CONTRIBUTING.md](CONTRIBUTING.md) the Kotlin and Compose rules.

## What is implemented

- A **Simulators** tab lists every simulator with a button to boot it or shut it down; each
  running one has its own tab, and closing the tab shuts it down. Zoom, a console drawer and a
  drawn device body (with the camera housing on the models that have one) are in the device's tab.
- A macOS-only **iOS Simulator** tool window. Select a device, boot it, view its
  screen, install an explicit `.app`, launch a bundle, and read console output.
- Live MJPEG after the owner's idb switch, using binary frames and retaining only
  the newest picture. A private companion and persistent input shell belong to
  the session. With input off, the panel retains JPEG snapshots, capped at two
  requests per second. Hidden windows and the Simulators list stop pictures.
  Failed live streams report a retry with bounded backoff.
- One action at a time. A button pressed while another action runs is refused
  with a message, never queued. A picture that arrives after the device changed,
  or after a boot, launch or rotation, is thrown away. A device picked while an
  action runs is refused too; the list returns to the service's device.
- An agent command-line client that talks to the same service as the panel.
  It saves PNG screenshots, reads logs and the test report, and offers input.
- Optional idb input and live video: click to tap, drag and release to swipe,
  type text, Home, Lock and Rotate. Both are off until the owner's existing switch
  is enabled. Input keeps one idb connection warm instead of starting Python per event.
- Tests for protocol validation, geometry, device parsing, command construction,
  log limits, process timeouts, session cleanup and HTTP authentication, and for
  the panel's state: stale pictures, refused actions, failures, the console
  cursor, input permission, hidden windows and late results after closing.

Without idb, use Apple's Device Hub window for input. No private framework is
loaded by this plugin itself. Switching input on runs idb for input and live video, which use those interfaces.
The plugin installs idb only when you choose Install idb… and agree to its dialog.

This is the beginning of the one plugin planned in roadmap stage 6. It does not
yet connect `kas3c lsp`, set AS3 breakpoints or step through scripts. Keep using
the [LSP4IJ setup](../README.md#rider-and-other-jetbrains-ides). The Simulator panel
is absent on Windows and Linux. The descriptor accepts only platform build
262.10968.170; other IDEs, builds and operating systems have not been tested.

## Build

The build needs JDK 25 and an installed Rider 2026.2 (build 262.10968.170). It uses
the Gradle wrapper (Gradle 9.8.1), Kotlin 2.4.10 with the Compose compiler plugin,
the IntelliJ Platform Gradle Plugin 2.20.0, ktlint 1.8.0 and Compose Swing UI
0.1.0-alpha06. It compiles against the installed IDE and never downloads one.
Rider's own runtime is a JDK 25, so the plugin needs nothing extra at run time.

From this directory, with `JAVA_HOME` set to a JDK 25:

```sh
./gradlew check buildPlugin
python3 -m unittest discover -s tests -v
```

`check` runs the style check and the Kotlin tests of both modules. `buildPlugin`
refuses to build while the style check fails, and puts a zip in
`build/distributions/`. `./gradlew ktlintFormat` fixes most style findings.
`./gradlew verifyPlugin` runs JetBrains' plugin verifier against the installed Rider.

Rider is found through the `RIDER_HOME` environment variable, or at
`/Applications/Rider.app` when it is not set. The wrapper uses your normal Gradle
home. On a machine that has not built this before, it downloads Gradle 9.8.1 and
the dependencies named above; add `--offline` once they are cached. No JDK is
downloaded: without a JDK 25 the build stops.

## Run a sandbox IDE

```sh
./gradlew runIde
```

This starts the installed Rider with separate settings and plugin files under
`.intellijPlatform/sandbox/`. It does not touch your own Rider settings. Open this
worktree or the Godot test project, then **View > Tool Windows > iOS Simulator**.

## Check the panel without a person

```sh
./gradlew runIde -PuiProbe
```

This adds a second, test-only plugin to the sandbox. It mounts the real panel
with a synthetic service, in the light and the dark theme, without showing a
window. It presses Refresh, Boot, Launch and Install through the real buttons,
checks what the panel then displays, and writes pictures and `result.txt` to
`build/reports/ui/`. The task fails unless the result is `passed`. Look at the
pictures after any change to the layout. The probe never talks to a Simulator
and is never part of the plugin zip.

## Use the panel

Choose an available iOS device. Boot does not erase or create one. Type an absolute
path to a Simulator `.app` into **App**, then press **Install**. The default bundle
is `org.kumikae.as3test`; its app is `kas3test.app`. **Launch restarts that app.**
Do not install a device-only export. The custom Apple-silicon Simulator template
is separate work; this plugin does not build it.

For a check without the Godot app, select the booted device and launch
`com.apple.Preferences`. The underlying Xcode commands were tested with Settings.
The panel itself has been drawn by the UI probe, not yet used by a person.

The console belongs to the session. Changing device, relaunching, closing the
project or stopping the standalone service closes its console process. Xcode
forwards signals to the app, so this may stop the app launched by the session.
It leaves the Simulator booted. Apps started elsewhere are not attached or stopped.
Hiding the panel leaves the service available to an agent and keeps collecting logs.

Logs are chunks of text with cursors, kept in 256 Ki characters of memory. The
panel retains 128 Ki characters. Older output is discarded with a flag. No growing
log files are created. These are the launcher's console pipes, not all iOS unified
logs. App streams may be combined by `simctl`. An empty console is not a passing
test. The `report` command reads `Documents/as3test.txt` from the app's data
container. That path was read from the actual Simulator export: 14 passed, 0 failed.

## Use the same session from an agent

Copy the endpoint file's path with **⋮ > Copy agent endpoint path** in the panel. It holds a local port and a
secret token. Keep it private. The client refuses a descriptor readable by other
users. The server listens only on `127.0.0.1`. There is one selected device per
session, shared by the IDE and every connected agent.

```sh
SESSION=/absolute/path/shown/in/the/panel/session.json
python3 simulator.py --session "$SESSION" devices
python3 simulator.py --session "$SESSION" select --device '<id from devices>'
python3 simulator.py --session "$SESSION" boot --device '<id from devices>'
python3 simulator.py --session "$SESSION" screenshot --device '<id from devices>' --out build/screen.png
python3 simulator.py --session "$SESSION" tap --frame build/screen.png.json 120 300
python3 simulator.py --session "$SESSION" swipe --frame build/screen.png.json 120 500 120 200 --duration-ms 400
python3 simulator.py --session "$SESSION" text --device '<id from devices>' 'hello'
python3 simulator.py --session "$SESSION" button --device '<id from devices>' home
python3 simulator.py --session "$SESSION" rotate --device '<id from devices>' landscape_left
python3 simulator.py --session "$SESSION" launch --device '<id from devices>'
python3 simulator.py --session "$SESSION" log --cursor 0
python3 simulator.py --session "$SESSION" report --device '<id from devices>'
```

Use `--help` after any command. Save a new screenshot after rotation. Tap and swipe
coordinates are pixels in that screenshot, not points or coordinates on the Mac.
The saved `.json` carries the device and geometry generation. A generation is a
number that changes when the service loses or changes its known screen geometry.
Stale input is refused. Failed commands return a nonzero exit code. Do not retry
input blindly after a timeout: it may already have reached the device.

To run the same service without an IDE:

```sh
./gradlew :core:installDist
core/build/install/kumikae-simulator/bin/kumikae-simulator --state-dir "$PWD/build/agent"
```

The launcher needs a Java 25 on `JAVA_HOME` or the `PATH`, because the service is
compiled for the same Java as the plugin. It prints an endpoint file path. Keep that process running and point the client
at that file. Ctrl+C closes it. All session files go under the supplied directory.
The exact wire format is in [protocol.md](protocol.md).

## Input: the owner's switch

Live pictures, taps, swipes, typing, Home, Lock and Rotate use idb, a tool from Meta that drives the
Simulator through Apple's private input services. Apple can change those without notice. Nothing
here installs it or turns it on by itself:

1. In the panel, choose **⋮ > Switch input on…** and agree. The choice is remembered.
2. If idb is not on this Mac, the menu then offers **Install idb…**. It says what will be installed
   and from where, asks, runs `tools/install-idb.sh` (its companion from Meta's Homebrew tap, its
   client in its own Python environment), shows what the script printed in the console, and
   switches input on. The same script can be run by hand. It needs Homebrew.

The service finds idb in `~/.local/bin`, Homebrew's folder or along `PATH`, and reads each device's
pixels per point from idb's description. It starts a private companion, input shell and video
child, and stops them on session closure. It leaves companions started by other tools alone. A device idb cannot describe stays in viewer mode and says why.

The standalone service has no panel to ask, so the owner's switch is an environment variable:

```sh
export KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT=1
export KUMIKAE_SIMULATOR_IDB=/absolute/path/to/idb   # only if idb is somewhere unusual
```

Text is printable ASCII only. A drag is one swipe, sent when the mouse is released. There is no
multi-touch, audio, Unicode paste or live finger tracking.

## Layout of the code

| Path | Job |
|---|---|
| `core/` | Kotlin service, Xcode commands, optional input adapter, protocol and tests. No IDE imports. Also the standalone service. |
| `src/main/kotlin/.../panel/` | The tool window: repository, controller, composed view, the painted screen. |
| `src/main/kotlin/.../ui/` | The Compose host and the small shared composables. |
| `src/main/resources/META-INF/plugin.xml` | One plugin id; the tool window is offered on macOS only. |
| `src/test/kotlin/` | Panel tests, the manual scheduler, and the UI probe. |
| `src/uiProbe/plugin.xml` | Descriptor of the test-only probe plugin. |
| `simulator.py` | Python standard-library agent client. Builds no Simulator commands. |
| `tests/` | Client tests with a fake local service. No IDE or Simulator needed. |

Android is out of scope. The protocol and input interface leave room for another
backend without duplicating the panel or client.

## Names and compatibility: the owner's to change

These were decided when the plugin moved to the same build and structure as the
owner's other JetBrains plugin. They are recorded here so they are easy to revisit.

- Plugin id and package root: `app.anicanon.kumikae.as3`. The service's package is
  `app.anicanon.kumikae.as3.simulator`. Vendor: AniCanon. Name: Kumikae AS3.
- The plugin depends on `com.intellij.modules.platform` only, not on a Rider
  module. It is meant for every JetBrains IDE, Rider first.
- It installs only into platform build 262.10968.170, the installed Rider. The
  Compose Swing library is an alpha and is only known to work there. Widening
  the range means trying the panel in each other IDE or build first.
- The Simulator tool window is offered on macOS only.
- The service and the plugin both target Java 25.
- The default bundle in the panel stays `org.kumikae.as3test`, the test app's
  identifier. It is not the plugin's identity.
