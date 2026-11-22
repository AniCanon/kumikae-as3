# Coin Dash

A 30-second rooftop score attack written in **ActionScript 3** for Godot 4.7. Run the lower deck for silver (+1), or cut across the drone’s patrol for gold (+3). A laser hit sends you back to the dock and costs three seconds. A well-timed dash takes you through the beam. The game saves your highest completed score on this device.

![A courier collecting silver and gold on the rooftops of Port Meridian](../../docs/assets/coin-dash.gif)

## Play

| Action | Keyboard | Controller | Touch or mouse |
|---|---|---|---|
| Start or play again | Space or Enter | Bottom face button | Press the menu button |
| Steer | WASD or arrows | Left stick | Hold or drag on the deck |
| Dash | Space | Bottom face button | Press **Dash** |
| Restart immediately | R | Top face button | |

Open this folder as a Godot project and press **Play** once the ActionScript 3 extension is built. From a source checkout, build it once using a [godot-cpp](https://github.com/godotengine/godot-cpp) checkout for Godot 4.7. Run these commands from the repository root, replacing the path on the first line.

```sh
cmake -S compiler -B compiler/build-godot -DKAS3_GODOT_CPP=/path/to/godot-cpp
cmake --build compiler/build-godot --target kas3_godot
godot --headless --editor --quit-after 300 --path examples/coin-dash
godot --path examples/coin-dash
```

## Inside the game

The source root is `src/`. Each public ActionScript 3 class lives in the directory named by its package. The same convention applies under `tests/`. [Flash and AIR class-file conventions](https://airsdk.dev/docs/development/appendixes/how-to-use-actionscript-examples)

| Under `dev.kumikae.coindash` | What to change here |
|---|---|
| [config](src/dev/kumikae/coindash/config) | `RoundRules` owns round length, pickup values, hit penalty and dash timing. `InputAction` holds the action names. |
| [gameplay](src/dev/kumikae/coindash/gameplay) | `Round` is pure ActionScript 3 simulation. `Rooftop` connects typed `[Export]` actors, `[Signal]`s, input and pickup routes. |
| [entities](src/dev/kumikae/coindash/entities) | `Player` steers and dashes. `Coin` emits `collected`. `Scanner` owns its patrol, beam and hit test. Each actor draws itself with cached `Palette` colors. |
| [presentation](src/dev/kumikae/coindash/presentation) | `Hud` (`CanvasLayer`, dirty-checked scoreboard, menus and `[Signal]` buttons), `SoundBank` (typed audio triggers) and `Palette` (shared colors). |
| [storage](src/dev/kumikae/coindash/storage/Ledger.as) | Loading and saving the best score. |

Scenes and theme are split cleanly: `main.tscn` instances [scenes/Hud.tscn](scenes/Hud.tscn) and [scenes/SoundBank.tscn](scenes/SoundBank.tscn), styled by [assets/game-theme.tres](assets/game-theme.tres), with every dependency wired through typed `[Export]` node references and compile-time `[Signal]`s — zero `getNode()` lookups or stringly-typed `.call()` bridges.

The scene-and-actor split follows the play state and player in the Flash game *Mode*. Actor-specific tuning stays with the actor, so the scanner’s beam drawing and collision check use one length. [Mode play state](https://github.com/AdamAtomic/Mode/blob/master/src/PlayState.as) · [Mode player](https://github.com/AdamAtomic/Mode/blob/master/src/Player.as)

To try hot reload, change the scarf shape in `Player._draw` and save during a run. The score and clock carry on as you edit. [How hot reload works](../../docs/hot-reload.md)

## Run the gameplay tests

```sh
python3 testing/runner.py examples/coin-dash/kas3-tests.json --compiler compiler/build/kas3c --require-pass
```

The ActionScript 3 tests cover the buzzer, restart, dash immunity, hit penalty, the scanner’s patrol reset, HUD signal wiring and silver and gold pickups through Godot physics. The [Claude Code plugin](../../editors/claude/README.md) also packages this project as its standalone starter.
