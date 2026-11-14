# Kumikae AS3 for JetBrains IDEs

The first Simulator panel is written, but **has not compiled or run in Rider yet**.
The offline build stops on a missing dependency before Kotlin compilation. No
plugin zip was produced. The Python agent client has passed its seven tests.

The [design](../../docs/superpowers/specs/2026-11-14-jetbrains-ios-simulator-design.md)
compares screen and input options. The [results](../../docs/results/2026-11-jetbrains-simulator.md)
separate what was run from what is only implemented.

## What is implemented

- A macOS-only **iOS Simulator** tool window. Select a device, boot it, view its
  screen, install an explicit `.app`, launch a bundle, and read console output.
- JPEG snapshots, capped at two requests per second. One capture is in flight at
  a time. Capture alone took about half a second on this Mac. This is a slow
  preview, not smooth video. Hidden panels stop requesting pictures.
- An agent command-line client that talks to the same service as the panel.
  It saves PNG screenshots, reads logs and the test report, and offers input.
- Optional idb input: click to tap, drag and release to swipe, a text field, home,
  lock and rotation. **It is off by default and has not been tested with idb.**
- Tests for protocol validation, geometry, device parsing, command construction,
  log limits, process timeouts, session cleanup and HTTP authentication.

Without idb, use Apple's Device Hub window for input. No private framework is
loaded by this plugin. Enabling idb introduces its private Apple dependencies.
There is no automatic download or installation path.

This is the beginning of the one plugin planned in roadmap stage 6. It does not
yet connect `kas3c lsp`, set AS3 breakpoints or step through scripts. Keep using
the [LSP4IJ setup](../README.md#rider-and-other-jetbrains-ides). The Simulator panel
is absent on Windows and Linux. The descriptor targets platform build 262 and
later; other IDEs and operating systems have not been tested.

## Build without downloads

Use Java 21 and an installed Gradle 9.8.1. This project uses Kotlin 2.4.10 and the
IntelliJ Platform Gradle Plugin 2.20.0. It compiles against an installed IDE, with
Rider at `/Applications/Rider.app` as the default. It does not request an IDE SDK
download.

From this directory:

```sh
./gradle-offline :core:test buildPlugin
python3 -m unittest discover -s tests -v
```

`gradle-offline` finds the cached Gradle 9.8.1 distribution. You can instead set
`GRADLE_BIN` to a full path to an installed Gradle executable. The script always
passes `--offline`. Its writable Gradle home is `.gradle-home/` here. It reads the
existing dependency cache under `~/.gradle/caches` without modifying it. Override
that cache root with `GRADLE_RO_DEP_CACHE` if needed.

The known missing artifact on this Mac is:

```text
com.google.errorprone:error_prone_annotations:2.41.0
```

Gson 2.13.2 requires it. Its JAR and dependency metadata must be made available
through an approved dependency download before this build can proceed. The
Gradle and Kotlin plugins, Gson, JUnit 4.13.2, Hamcrest 1.3 and Kotlin standard
library were present. The offline dependency report names no other missing item
on the core test runtime path. Further build stages have not run and may expose
other requirements. No dependency was substituted or fetched to bypass this.

A successful `buildPlugin` puts a zip in `build/distributions/`. Until then,
compilation, the Kotlin tests, packaging and IDE compatibility remain unverified.

## Run a sandbox IDE

Once the build dependencies are available:

```sh
./gradle-offline runIde -PidePath=/Applications/Rider.app
```

This starts the installed Rider with separate settings and plugin files under
`build/idea-sandbox/`. Open this worktree or the Godot test project. Open **View >
Tool Windows > iOS Simulator**. Use another installed compatible IDE by changing
`idePath`. That path is a local installation, not a product version to download.

Choose an available iOS device. Boot does not erase or create one. Type an absolute
path to a Simulator `.app` into **App**, then press **Install**. The default bundle
is `org.kumikae.as3test`; its app is `kas3test.app`. **Launch restarts that app.**
Do not install a device-only export. The custom Apple-silicon Simulator template
is separate work; this plugin does not build it.

For a check without the Godot app, select the booted device and launch
`com.apple.Preferences`. The underlying Xcode commands were tested with Settings.
The panel itself has not been seen in an IDE.

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
container; that path remains to be checked with the actual Simulator export.

## Use the same session from an agent

Copy the **Agent endpoint file** path from the panel. It holds a local port and a
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
./gradle-offline :core:installDist
core/build/install/kumikae-simulator/bin/kumikae-simulator --state-dir "$PWD/build/agent"
```

It prints an endpoint file path. Keep that process running and point the client
at that file. Ctrl+C closes it. All session files go under the supplied directory.
The exact wire format is in [protocol.md](protocol.md).

## Optional input: an owner decision

idb and its matching macOS companion are not installed here. Neither is bundled.
They use Apple's private input services. Older versions may not work with Xcode
27; current idb source describes a different input transport for that Xcode.
No version is claimed compatible until tested on this Mac. Choosing and installing
that pair needs the owner's agreement to the dependency and private-interface risk.

After that decision and installation, start the IDE or standalone service with:

```sh
export KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT=1
export KUMIKAE_SIMULATOR_IDB=/absolute/path/to/idb
export KUMIKAE_SIMULATOR_INPUT_DEVICE='<id from devices>'
export KUMIKAE_SIMULATOR_SCALE=3
```

`3` is only an example. Set the selected Simulator display's actual pixels per
point, using idb's device description. The scale is bound to that exact device.
Other devices stay in viewer mode. Configure idb and its companion locally first;
the adapter invokes its CLI and does not manage installation or companion cleanup.
The service does not silently enable input just because `idb` is on PATH.

The adapter uses the documented CLI argument shapes, including `ui rotate` from
current idb source. Text is printable ASCII only. Drags are one swipe after mouse
release. No multi-touch, audio, Unicode paste or live finger tracking is provided.

## Layout of the code

| Path | Job |
|---|---|
| `core/` | Kotlin service, Xcode commands, optional input adapter, protocol and tests. No IDE imports. |
| `src/main/kotlin/` | Tool window and painting. Calls the service through HTTP. |
| `src/main/resources/META-INF/plugin.xml` | One plugin id, with a macOS tool-window condition in its factory. |
| `simulator.py` | Python standard-library agent client. Builds no Simulator commands. |
| `tests/` | Client tests with a fake local service. No IDE or Simulator needed. |

Android is out of scope. The protocol and input interface leave room for another
backend without duplicating the panel or client.
