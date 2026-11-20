# Kumikae AS3 for Claude Code

Build and run an ActionScript 3 Godot game on an Apple silicon Mac. Claude uses the bundled
`kas3c lsp` for diagnostics, definitions and references in `.as` files. The game tools use local
executables and work with Rider closed.

## Install

With a packaged marketplace on disk, run these commands in Claude Code.

```text
/plugin marketplace add /absolute/path/to/kumikae-marketplace
/plugin install kumikae@anicanon
/reload-plugins
```

For a single session, run `claude --plugin-dir /absolute/path/to/kumikae-marketplace/plugins/kumikae`
from the game folder. Select the packaged folder, which contains both `.lsp.json` and `kit/kas3c`.

Use Python 3.9 or newer and Godot 4.7 on your PATH, or set `KAS3_GODOT` to its executable.
Exports require matching Godot export templates. For iOS install Xcode with an iOS Simulator runtime.
For Android install the SDK, Build Tools and an ARM64 emulator. Boot the destination before running
or testing on it. Set `ANDROID_HOME` for a nonstandard SDK location.

## Develop a game

| Claude command | Result |
|---|---|
| `/kumikae:new MyGame` | Create a playable Coin Dash starter with export presets, the game runtime and a gameplay test. Open the new folder in Claude Code. |
| `/kumikae:doctor` | Create a temporary starter and run its physics and HUD test through the local Godot runtime. |
| `/kumikae:build desktop` | Export a debug game and print its artifact path. Mobile targets are `ios-simulator` and `android`. |
| `/kumikae:run desktop` | Play the desktop project in Godot. On mobile, build, install and launch on the selected destination. |
| `/kumikae:test all --case Gameplay::CoinPickupTest::pickupUpdatesHud` | Run the same discovered AS3 case in desktop Godot, iOS Simulator and Android emulator. Report each result and exit nonzero if any target failed or was unavailable. |

Use these commands in an existing Kumikae project with its desktop runtime, export presets and
`kas3-tests.json`. For multiple destinations, pass `--ios-device UDID` or
`--android-device emulator-SERIAL`. Build chooses the preset by platform. Pass `--preset NAME`
when a platform has more than one preset. Run tests for each target separately when preset names differ.
Each build prints a fresh output folder under `.godot/kumikae-builds/`. Delete older output folders when you no longer need the builds. Desktop Play follows the project's Godot settings.
Simulator Play watches the host source directory. Android Play rebuilds and launches the APK.

Each skill invokes the same executable. To reproduce a result in a terminal, run
`/absolute/path/to/plugins/kumikae/bin/kumikae test all --project /path/to/game`.
The LSP server starts automatically for AS3 files. Open the folder containing `project.godot`.

## Work on several games

Open each game folder in its own Claude session. New games receive distinct mobile application
identifiers, including games created with the same folder name. Moving a game preserves its identifier.
If you copy an existing game to create a second app, set its identifiers in the export presets.
Each phone screen shows one foreground app at a time. The plugin queues tests and launch requests
that share a destination. Use separate simulators or emulators for simultaneous playtests.

## Assemble the marketplace

From the repository root, use the native directory from a matching Kumikae release build. The current
Rider distribution zip contains this directory. Unpack the archive locally and run:

```sh
python3 -I editors/claude/package.py /path/to/kumikae-as3/native /path/to/kumikae-marketplace
claude plugin validate --strict /path/to/kumikae-marketplace
claude plugin validate --strict /path/to/kumikae-marketplace/plugins/kumikae
```

The destination must be new. Packaging copies the compiler, runtimes and Simulator template present
in the native kit, and the test runner, export helpers and starter from this checkout. Use the same
release for the native binaries and source. The resulting marketplace can be moved or installed on
another supported Mac. LSP and desktop development need only the macOS portion of the kit.
When the kit contains `web/libkas3_godot.wasm`, new games include the library and a **Web** preset
for exporting from Godot. A desktop-only kit omits that preset.
