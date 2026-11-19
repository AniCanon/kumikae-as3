# Kumikae AS3 for Rider and IntelliJ IDEA

A plugin for writing Godot games in ActionScript 3. It adds language support, a project wizard, run
configurations for desktop, iOS and Android, embedded device displays, a test runner, and editors
for `project.godot` and scenes.

This page is for someone using the plugin. To change it, read [ARCHITECTURE.md](ARCHITECTURE.md) and
[CONTRIBUTING.md](CONTRIBUTING.md).

## Requirements

| | |
|---|---|
| IDE | Rider 2026.2, or IntelliJ IDEA 2026.1. The plugin accepts builds 261.22158.277 through 262.10968.170, and has been checked on those two |
| IntelliJ IDEA only | The LSP4IJ plugin, version 0.21.0. Rider uses its own language client |
| Operating system | macOS. The Simulator panel exists only on macOS. Windows and Linux are untested |
| To run a game | Godot 4.7 |
| For the Simulator | Xcode |

There is no AS3 debugger yet.

## Build the plugin

You need a JDK 25, and Rider 2026.2 installed. The build compiles against the installed IDE and never
downloads one.

Build the compiler first, so that the plugin bundles `kas3c`. Build the Godot extension too if you want the
project wizard to set up new projects with it. See [the compiler's README](../../compiler/README.md#build-and-test).

From this folder, with `JAVA_HOME` set to the JDK 25:

```sh
./gradlew buildPlugin
```

The zip is written to `build/distributions/`.

| What the build needs | Where it looks |
|---|---|
| Rider | `RIDER_HOME`, or `/Applications/Rider.app` |
| Gradle and the libraries | Downloaded by the Gradle wrapper on the first build. Add `--offline` after that |
| Swift 6, for the Simulator helper | The installed Xcode. Without it, the helper is skipped and the build says so |

## Install the plugin

1. In the IDE, open **Settings > Plugins**.
2. Click the gear icon and choose **Install Plugin from Disk**.
3. Select the zip file from `build/distributions/`.
4. Restart the IDE. Installing, updating and uninstalling all need a restart.

In IntelliJ IDEA, install **LSP4IJ** from **Settings > Plugins** as well. If it's missing, the plugin shows
a notification when you open an AS3 file. Typing help works without it, and completion and errors don't.

### How the plugin finds the compiler

The plugin uses the first `kas3c` it finds, in this order:

1. A build in the project or in a folder above it: `compiler/build/kas3c` or `build/kas3c`.
2. The one bundled with the plugin.
3. The `PATH`, then `/opt/homebrew/bin` and `/usr/local/bin`, then `~/.local/bin`.

Rebuilding the compiler elsewhere doesn't update the copy inside an installed plugin. Rebuild and
reinstall the plugin to update it.

## Language support

Errors as you type, completion, signature help, go to definition, find references, hover and outline come
from the compiler's language server. Bracket pairing and indentation are the plugin's own. See
[Completion and typing help](../completion.md).

## Create a project

Choose **File > New Project > AS3 Godot Game**. The wizard creates a small game that runs: a
`project.godot`, a scene, two scripts, and the extension's files.

- **Package name.** The app's identifier, such as `com.studio.game`. Left empty, it's made from the
  project's name.
- **Where the game should run.** Desktop is always included. Choosing iOS adds an iOS export preset and an
  Simulator run configuration. Android adds its own export preset. Companion plugins can contribute additional targets.

## Run the game

Opening a project that has a `project.godot` and at least one `.as` file adds a run configuration named
**Game**, of the type **Kumikae AS3 → Game**. Press Run. The game starts in Godot, and what it prints appears in
the Run window.

| Field | Left empty |
|---|---|
| Godot | Found on the `PATH`, in `/opt/homebrew/bin` or `/usr/local/bin`, or in `/Applications` |
| Project folder | This project |
| Scene | The project's main scene |

Edit a script while the game runs and save. The game takes the new code and keeps its state. See
[Hot reload](../../docs/hot-reload.md).

## Run on iOS or Android

Open **Run → Add Game Target…** and choose a Godot export preset. In **Game Devices → Simulators**,
start a simulator or Android virtual device. Select the destination in the run toolbar and press **Run**.
Rider reads the application identifier from the preset, builds the game, installs it and launches it.
iOS requires Xcode and a Simulator runtime. Android requires the Android SDK, matching Godot export
templates and an ARM64 destination. The macOS distribution bundles the AS3 runtimes and Simulator template.

## The iOS Simulator panel

Open **View → Tool Windows → Game Devices** and select an iOS simulator. The iOS tab displays its
screen, input controls and console. Android devices open in the Android tab. For stopped Android
devices, **Graphics for next emulator start** offers **Device default**, **Host GPU** and **Software**.
This applies when Rider next starts an emulator and leaves the AVD configuration intact.
See [embedded device performance](../../docs/embedded-device-performance.md) for frame rates, latency
and how to repeat the measurements.

### Live picture and input

At first the panel shows snapshots, at most two a second, taken with Xcode's public `simctl` tool.

To see a live picture and to tap, drag and type in the panel, choose **Switch input on for live picture**
from the menu and confirm. The panel then uses a bundled helper that reads the Simulator's screen and sends
input through private Apple interfaces. The choice is remembered.

- The helper works with iPhone and iPad simulators on a Mac with Apple silicon.
- It checks that the installed Xcode has the interfaces it needs, and reports an error if not. Only Xcode
  27.0 has been tested.
- Taps, drags, Home, Lock and rotation work. Typing sends ASCII as key presses. Other text goes through the
  simulator's pasteboard, and iOS may ask you to allow the paste.
- There is no multi-touch and no audio.
- Hiding the panel stops the helper.

### The console

The console shows what the launched app prints. It keeps the most recent 128,000 characters or so and marks
where older output was dropped. It isn't the whole iOS system log.

Changing device, launching again, or closing the project stops the console's process, which can stop the
app it launched. The Simulator stays booted.

## Tests

Click the gutter beside a method to run that test, or beside a class to run its fixture.
Use **Run Game Tests** on the Tests folder for the default target. Context runs use the local test
runner even while a mobile destination is selected. Double-click a failed result to open the
assertion. To keep a named plan, create a **Kumikae AS3 → Tests** configuration, give it a name
and select its groups. The same manifest drives the command-line runner and CI.

New games keep model code in `src/<package>`, engine scripts in its `view` package, and fixtures in
`tests/<package>`. **New → ActionScript** creates files, classes, interfaces, Godot node scripts,
test cases and test suites. Use **New → ActionScript Package** for a namespace. An empty `[Suite]`
collects its directory's fixtures in Rider. Give the suite typed fixture fields to select a subset.

[Testing](../../testing/README.md) describes the library, the manifest and the command-line runner.

## Project settings and scenes

**`project.godot`.** Open the file to use the **Settings** editor beside its source. It edits the
application's name, main scene and icon, the viewport size, stretch mode and orientation, and the
identifier and icon of an existing iOS or Android export preset. Settings and comments it doesn't know are
kept. **Apply** changes the file in one undoable step.

Click the icon or splash in **General** to open the artwork gallery. Search by filename or folder,
select a thumbnail and inspect its preview before choosing **Use Icon** or **Use Splash**.
**Browse…** also accepts local images. **Import and Use** copies the selected file into the game's
`assets/` folder, giving it a unique name when needed. **Apply** saves the assignment.
The splash preview uses the project's launch window size, background, filtering and boot stretch mode.

Choose **Add Game Target** for a new preset, or **Export** or **Export All** to build existing targets. **Properties** shows additional Godot settings entered in Source.

**Scenes.** Open a `.tscn` file to use the **Scene** editor. Its outline shows each node's type and the AS3
script attached to it. You can open a script, and attach or detach one. Attaching accepts a script that
directly extends the node's type. Edit inherited and instanced scenes in Godot.

**From a script to its scenes.** A gutter icon on an AS3 class, and **Find Attached Scene Nodes** in the
editor's menu, show the scene nodes that use the script.

**The Game view.** The project view has a **Game** pane that shows empty folder chains under `src` and
`tests` as package names, such as `com.studio.game`. Its menu has **Compact AS3 Packages**, **Show AS3 Members** and **Show Generated Files**.
Double-click an AS3 file to open it. Click its disclosure arrow to show or hide its declarations.

## Use the Simulator from the command line

`simulator.py` is a client for the same local service the panel uses. A script or a coding agent can use it
to drive the Simulator while you watch in the panel. It needs only Python's standard library.

1. In the panel's menu, choose **Copy agent endpoint path**. The file holds a local port and a secret
   token. Keep it private.
2. Pass that path to the client:

   ```sh
   SESSION=/path/shown/in/the/panel/session.json
   python3 simulator.py --session "$SESSION" devices
   python3 simulator.py --session "$SESSION" select --device '<id from devices>'
   python3 simulator.py --session "$SESSION" boot --device '<id from devices>'
   python3 simulator.py --session "$SESSION" screenshot --device '<id from devices>' --out build/screen.png
   python3 simulator.py --session "$SESSION" tap --frame build/screen.png.json 120 300
   python3 simulator.py --session "$SESSION" launch --device '<id from devices>'
   python3 simulator.py --session "$SESSION" log --cursor 0
   ```

The other commands are `status`, `shutdown`, `install`, `terminate`, `report`, `text`, `button`, `rotate`
and `swipe`. Add `--help` after any command.

- **Coordinates are pixels in a saved screenshot,** not points and not positions on your Mac. Save a new
  screenshot after a rotation.
- **Stale input is refused.** The `.json` saved beside a screenshot records the screen's geometry. If the
  geometry has changed since, the tap fails.
- **Don't retry input blindly after a timeout.** It may have reached the device.
- **Input needs the same switch as the panel.**

The service listens only on `127.0.0.1`. There is one selected device for each session, shared by the
panel and every client. [protocol.md](protocol.md) has the wire format.

### Run the service without an IDE

```sh
./gradlew :core:installDist
core/build/install/kumikae-simulator/bin/kumikae-simulator --state-dir "$PWD/build/agent"
```

The service needs Java 25. It prints the path of its endpoint file. Keep it running, and point the client
at that file.

The service has no panel to ask about input, so opt in with the environment:

```sh
export KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT=1
export KUMIKAE_SIMULATOR_NATIVE=/path/to/simulator-bridge
```

## Limits

- No debugger.
- No rename refactoring, for names or for packages.
- The Simulator panel has no multi-touch and no audio.
- The plugin has been checked by automated editing tests in Rider and IntelliJ IDEA. It needs wider use by
  people.
- Android Studio isn't supported.
