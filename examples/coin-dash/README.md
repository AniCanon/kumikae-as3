# Coin Dash

A small game written in ActionScript 3 for Godot 4.7: move the disc with the arrow keys and collect the
coin. Three scripts, no art: everything on screen is drawn by the scripts.

| File | What it is |
|---|---|
| `Player.as` | A `CharacterBody2D`: reads the arrow keys, moves, and draws itself. |
| `Coin.as` | An `Area2D`: listens for the player touching it, counts, and moves somewhere else. |
| `Hud.as` | A `Label`: shows the count. |
| `main.tscn` | The scene: the three nodes and their collision shapes. |

## Run it

Build the extension once (from the repository's root), then let Godot read the project once:

```sh
cmake -S compiler -B compiler/build-godot -DKAS3_GODOT_CPP=<a godot-cpp checkout for Godot 4.7>
cmake --build compiler/build-godot -j
godot --headless --path examples/coin-dash --import
godot --path examples/coin-dash
```

`bin` is a link to where that build puts the library.

## Change it while it runs

Leave the game running and edit a script: the speed in `Player.as`, a colour or a size in a `_draw`,
or what `onTouched` does in `Coin.as`, and save. The game takes the new code within half a second and
keeps its state: the score and where everything is. If the edit has an error, Godot prints it and the
game carries on with the code it had.

What a field starts as (`private var age:Number = 0`) is given when an object is made, so changing it
does not reach an object that is already on screen: change the code that uses it, or restart.
