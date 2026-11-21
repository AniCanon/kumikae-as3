# Documentation

| Page | Read it to learn |
|---|---|
| [Using AS3 in Godot](godot.md) | How to set up a project, and how Godot's API, signals and values read in AS3 |
| [The AS3 standard library](standard-library.md) | How to choose a collection, work with numbers and text, and read and write JSON |
| [The AS3 language](language.md) | Which AS3 the compiler accepts and runs, what it adds, and where it differs from Flash |
| [How it works](how-it-works.md) | How a script goes from an `.as` file to a running game |
| [Performance](performance.md) | The numbers against Godot C#, and the techniques behind them |
| [Hot reload](hot-reload.md) | What happens when you save a script while the game runs |
| [Roadmap](roadmap.md) | What is built, what is in progress, what comes next, and the known limits |
| [C++ guidelines](cpp-guidelines.md) | How the compiler's C++ is written |

Each part of the code has its own README:

| Part | README |
|---|---|
| The compiler, the interpreter and the Godot extension | [compiler/](../compiler/README.md) |
| Editor support | [editors/](../editors/README.md) |
| Testing in AS3 | [testing/](../testing/README.md) |
| An example game | [examples/coin-dash/](../examples/coin-dash/README.md) |

## Writing documentation

- **Say what is true now.** History belongs in git. A page changes in the same pull request as the code it
  describes.
- **Write for someone new to the project.** Use plain words, short sentences and the second person.
- **Don't claim more than was run.** A number comes with how it was measured. Something not tested is
  listed as not tested.
- **Draw diagrams with [`diagrams/build.py`](diagrams/build.py).** Edit the script and run it; don't edit an
  `.svg` by hand.
- **Leave out anything about a console's SDK, tools or results,** and any personal or device identifier.
