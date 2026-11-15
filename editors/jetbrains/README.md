# Kumikae AS3 for JetBrains IDEs

The first piece is an iOS Simulator panel. The owner has tried it in Rider and
reported that the snapshot preview was slow. The live-picture implementation and
its measurements are described in the results below.

| Checked | By | Result |
|---|---|---|
| Compilation, style check and plugin zip | Automated build, 2026-11-15 | `./gradlew check buildPlugin` succeeds. |
| Service tests (`core`) | Automated build | 67 pass; one opt-in live probe skips in ordinary checks and passed separately. |
| Panel tests (repository, controller, service adapter) | Automated build | 115 pass; one opt-in retention probe skips in ordinary checks and passed separately. |
| Plugin verifier against the installed Rider | `./gradlew verifyPlugin` | Compatible with RD-262.10968.170. |
| Native Swift tests | `swift test` | 21 pass. |
| Native lifecycle recovery | Owned disposable Simulator, 2026-11-15 | Shutdown, reboot and read-only device switching passed; startup input-service readiness now preserves native retry. |
| Standalone frame retention | Production repository/controller/screen, synthetic frames | 7,800 distinct paints; full-GC heap 48.023–48.097 MiB, one retained stream image; zero images and 35.097 MiB hidden. Whole IDE/Compose not measured. |
| Agent client tests | `python3 -m unittest` | 7 pass. |
| Service and agent client against a booted Simulator | Run by hand through `simulator.py` | Devices, select, launch of `org.kumikae.as3test`, a 2622x1206 PNG screenshot, console log, and the test report "14 passed, 0 failed". |
| The panel's own repository against a booted Simulator | One throwaway test, read-only | Select, a decoded 1206x2622 JPEG picture in 403 ms, an empty console page and status. |
| Panel layout and buttons | UI probe in a sandbox Rider, with a synthetic service | Light and dark pictures under `build/reports/ui/`; Refresh, Boot and Launch pressed through the real buttons. |
| The panel in a Rider window, by a person | Owner | Snapshot version was slow; live version still needs the owner's acceptance. |
| Native panel against a booted Simulator | Automated visible sandbox probe | 63.45 distinct frames painted/s at 1206×2622; observation-to-paint p50 21.48 ms, p95 32.13 ms; press-to-input barrier acknowledgment 3.95–18.70 ms. These are not optical presentation or application-response timings. |
| Native ten-minute visible panel | Automated sandbox probe | Mean 58.92 frames/s, minute windows 46.39–64.23; helper RSS 41.78–53.94 MiB. IDE heap samples were bounded but ended higher; absence of IDE memory growth is not established. |
| Earlier idb ten-minute stream and input | Production service in a small harness | Mean 26.42 frames/s consumed; retained heap 5.655 MiB at one minute and 5.706 MiB at ten minutes. Owned children exited. This was not a ten-minute Rider heap measurement. |
| idb input against a booted Simulator | `simulator.py` and the sandbox panel probe | Density 3; Home, taps and Settings swipes observed. Earlier text, Lock and Rotate commands were accepted, without a separate visual confirmation. |

The designs (`docs/superpowers/specs/2026-11-14-jetbrains-ios-simulator-design.md` and
`2026-11-14-simulator-native-design.md`, kept out of the repository)
compare screen and input options. The [results](../../docs/results/2026-11-jetbrains-simulator.md)
give the detail behind this table. [ARCHITECTURE.md](ARCHITECTURE.md) describes the
parts and [CONTRIBUTING.md](CONTRIBUTING.md) the Kotlin and Compose rules.

## What is implemented

- A **Simulators** tab lists every simulator with a button to boot it or shut it down; each
  running one has its own tab, and closing the tab shuts it down. Zoom, a console drawer and a
  drawn device body (with the camera housing on the models that have one) are in the device's tab.
- A macOS-only **iOS Simulator** tool window. Select a device, boot it, view its
  screen, install an explicit `.app`, launch a bundle, and read console output.
- Full-resolution native pictures after the owner's private-input switch. A
  service-owned Swift helper reads the Simulator framebuffer into one mapped slot.
  Copies happen off the UI thread; only the newest picture survives. Static device
  decoration is cached at display density, with a 16 MiB allocation limit. If the
  helper cannot start, the panel tries installed idb MJPEG, then public screenshots
  and names the route in its status line. With input off, the panel retains JPEG
  snapshots, capped at two requests per second. Hidden windows and the Simulators list stop pictures.
  Failed live streams report a retry with bounded backoff. A guest input service
  still starting after reboot keeps the native route available for that retry.
- One action at a time. A button pressed while another action runs is refused
  with a message, never queued. A picture that arrives after the device changed,
  or after a boot, launch or rotation, is thrown away. A device picked while an
  action runs is refused too; the list returns to the service's device.
- An agent command-line client that talks to the same service as the panel.
  It saves PNG screenshots, reads logs and the test report, and offers input.
- Native input tracks press, movement and release live; pending moves coalesce.
  Taps, printable ASCII text, Home and Lock use the same persistent helper. Native
  rotation returns an explicit unsupported error. The idb fallback retains its
  release-time swipe and rotation behavior. Both routes require the owner's switch.
- Tests for protocol validation, geometry, device parsing, command construction,
  log limits, process timeouts, session cleanup and HTTP authentication, and for
  the panel's state: stale pictures, refused actions, failures, the console
  cursor, input permission, hidden windows and late results after closing.

The bundled helper needs no idb at runtime. It loads private Apple interfaces only
after the existing opt-in. It accepts Apple-silicon iPhone devices when the selected Xcode passes the
runtime capability checks; a build deny list can reject a known incompatibility
with its reason. Only Xcode 27.0 build 27A266a has been measured. Missing or
changed capabilities and unsupported devices fall back. The plugin
installs idb only when you explicitly choose Install idb… and agree to its dialog.

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
Rider's own runtime is a JDK 25. Building the native helper also needs installed
Swift 6; `buildSimulatorBridge` runs `swift build -c release` and bundles the
executable and its resource bundle in `native/`. Without installed Swift on macOS
that task logs a skip. It downloads nothing. Run `swift test` in
`native/SimulatorBridge` for the helper tests.

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
pictures after any change to the layout. The default probe never talks to a
Simulator and is never part of the plugin zip.
An explicitly enabled `KUMIKAE_SIMULATOR_LIVE_PROBE=1` run uses the already booted
demo device's Home and Settings, real Swing pointer phases and native input. Adding
`KUMIKAE_SIMULATOR_ENDURANCE_PROBE=1` extends that run by ten minutes, sampling CPU,
heap and RSS. The live run requires the native route, checks that hiding the panel
releases its helper, and reports measurements without asserting performance targets.

## Isolated native checks

No probe starts as part of an ordinary test run. With the release helper already
built, `KUMIKAE_NATIVE_LIFECYCLE_PROBE=1 ./gradlew --offline :core:test --tests '*NativeLifecycleProbe'`
creates one disposable iPhone using an installed runtime. It checks external
shutdown, reboot, session shutdown/boot and selection recovery. It captures frames
from an already booted Simulator for the selection check, sends it no input, and
deletes only the device it created, including after failures. A pre-existing probe
name is refused. `ProbeDeviceOwnerTest` covers lost creation replies and cleanup
errors with fake commands.

`KUMIKAE_FRAME_RETENTION_PROBE=1 ./gradlew --offline :test --tests '*FrameRetentionProbe'`
paints 7,800 fresh 1206×2622 images through the production repository, controller
and native screen in a standalone test JVM. It shows no window and uses no
Simulator. Live heap histograms force full collections; samples include the GC's
recorded after-collection heap, weak image counts and verified distinct paints.
It covers these objects, not a mounted Compose view or the whole IDE.

`KUMIKAE_SIMULATOR_MEMORY_PROBE=1 ./gradlew --offline runIde -PuiProbe` is a separate
ten-minute IDE probe with synthetic frames and a mounted, undisplayed production
view. It writes `memory.csv` and live histograms under `build/reports/ui/`, including
hidden/disposed samples. **This probe was prepared but not run in the follow-up:**
the owner's sandbox IDE was in use. Do not run it beside another sandbox session.

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

The existing **⋮ > Switch input on…** confirmation permits private Apple interfaces
for pictures and input and remembers the choice. The native helper is tried first;
installed idb is the fallback. With the switch off, pictures use public `simctl`.
Missing frameworks, symbols, unsupported Xcode builds or startup failures select
the next route and display a reason. Input already submitted is never replayed.

The helper's private surface and Xcode guard are documented in
[native/SimulatorBridge/README.md](native/SimulatorBridge/README.md). It owns no
Simulator window and needs no Screen Recording permission. Hiding the panel stops
its frame subscription and helper. Reopening creates a fresh connection.

The optional **Install idb…** dialog explains and asks before running
`tools/install-idb.sh`. Existing idb is found in
`~/.local/bin`, Homebrew's folder or `PATH`. The fallback owns its companion, input
shell and video child and leaves other tools' companions alone.

The standalone service has no panel to ask. Set its explicit opt-in and helper path:

```sh
export KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT=1
export KUMIKAE_SIMULATOR_NATIVE=/absolute/path/to/simulator-bridge
export KUMIKAE_SIMULATOR_IDB=/absolute/path/to/idb   # optional fallback override
```

Text is printable ASCII using US keyboard usages. Native drags follow the pointer
live. There is no multi-touch, audio or Unicode paste. Lock is implemented but has
not been visually verified on the shared device; native rotation is unsupported.

## Layout of the code

| Path | Job |
|---|---|
| `native/SimulatorBridge/` | Swift library, thin executable, shared frame layout and native tests. |
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
