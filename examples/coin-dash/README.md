# Coin Dash

A small game written in ActionScript 3 for Godot 4.7. Move the disc with the arrow keys and collect the
coin. The game has three scripts and no art: the scripts draw everything on screen.

| File | What it is |
|---|---|
| `Player.as` | A `CharacterBody2D` that reads the arrow keys, moves, and draws itself |
| `Coin.as` | An `Area2D` that listens for the player touching it, counts, and moves somewhere else |
| `Hud.as` | A `Label` that shows the count |
| `main.tscn` | The scene: the three nodes and their collision shapes |
| `kas3.gdextension` | Tells Godot where the AS3 extension is |

## Run it

You need Godot 4.7 as `godot` on your `PATH`, and a
[godot-cpp](https://github.com/godotengine/godot-cpp) checkout for Godot 4.7.

1. From the repository's root, build the extension:

   ```sh
   cmake -S compiler -B compiler/build-godot -DKAS3_GODOT_CPP=<path to godot-cpp>
   cmake --build compiler/build-godot --target kas3_godot
   ```

   The `bin` folder here is a link to where that build puts the library.

2. Let Godot's editor scan the project once, so that it registers the extension:

   ```sh
   godot --headless --editor --quit-after 300 --path examples/coin-dash
   ```

3. Run the game:

   ```sh
   godot --path examples/coin-dash
   ```

## Change it while it runs

Leave the game running, edit a script, and save. For example:

- Change the speed, `320`, in `Player.as`.
- Change a color or a size in a `_draw` method.
- Change what `onTouched` does in `Coin.as`.

The game takes the new code within half a second and keeps its state: the score, and where everything is.
If your edit has an error, Godot prints it and the game carries on with the code it had.

One thing doesn't change on a live object: a field's first value, such as `private var age:Number = 0`.
Objects of a class that extends a Godot class keep the values they have. Change the code that uses the
field, or restart the game. [Hot reload](../../docs/hot-reload.md) explains why.

## Verify a pickup

The `Gameplay::CoinPickupTest::pickupUpdatesHud` test places the player on the coin and waits for the
HUD to display the new score. The [Claude Code plugin](../../editors/claude/README.md) packages this
example as its standalone starter and can run this test on desktop and both mobile simulators.
