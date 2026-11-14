# Kumikae AS3

ActionScript 3 as a first-class scripting language for Godot 4.

You write AS3 classes that extend Godot's own classes. The same source runs in the editor, on desktop, on iOS
and Android, and on Nintendo Switch through the Kumikae build.

> **Status: early.** Today the compiler checks an AS3 program against Godot's whole API and reports syntax,
> name and type errors, on the command line and to an editor through its language server. A first
> part of the language runs on the command line (`kas3c run`), and in headless Godot an AS3 script moves a
> body, does arithmetic on Godot's values and listens to signals. Nothing runs in the Godot editor yet. The [roadmap](docs/roadmap.md) shows what comes next
> and how you can tell when each stage is done.

## What you will write

This script doesn't run yet, but `kas3c check` already accepts it and reports a mistake in it.

```actionscript
package game {
    import godot.*;

    public class Player extends CharacterBody2D {
        [Export] public var maxSpeed:Number = 300;
        [Signal] public var died:Signal;

        override public function _ready():void {
            (getNode("Hitbox") as Area2D).bodyEntered.add(onBodyEntered);
        }

        override public function _physicsProcess(delta:Number):void {
            velocity.x = Input.getAxis("left", "right") * maxSpeed;
            if (isOnFloor() && Input.isActionJustPressed("jump")) {
                velocity.y = -400;
            }
            moveAndSlide();
        }

        private function onBodyEntered(body:Node2D):void {
            if (body.isInGroup("enemies")) {
                died.emit();
                queueFree();
            }
        }
    }
}
```

- The class extends a Godot class, so you attach the script to a node as you attach a GDScript or C# script.
- `[Export]` shows the field in the inspector. Metadata tags are already AS3 syntax.
- **Godot's API reads as AS3.** Methods are lowerCamel (`moveAndSlide`), Godot's properties are AS3
  properties (`velocity`, `visible`), and a signal is a property you connect to (`bodyEntered.connect`).
  The [naming design](docs/superpowers/specs/2026-11-12-godot-naming-design.md) has every rule.
- `velocity.x = ...` changes the body's velocity. The compiler writes the changed vector back.

## Why AS3

| Goal | What it means |
|---|---|
| First-class in Godot | Scripts extend Godot classes, attach to nodes, export properties and use signals. |
| One codebase | The same source runs on desktop, iOS, Android and Nintendo Switch, with no per-platform code. |
| Fast | Release builds are native code. The target is ahead of or level with Godot C#, with steadier frames. |
| Easy to use | Write a script, press play. No build step while you edit. |
| Easy to debug | Breakpoints and stepping from JetBrains IDEs, in the editor and on a device. |

## What works today

| Capability | Status |
|---|---|
| Parse AS3 and report syntax errors (`kas3c parse`) | **Works** |
| Resolve names: packages, imports, classes, scopes (`kas3c check`) | **Works** |
| Give every expression a type and resolve `a.b` (`kas3c check --dump-types`) | **Works** |
| Report type errors (assignments, arguments, returns, overrides, interfaces, constants) | **Works** |
| Godot's whole API under AS3 names (`moveAndSlide()`, `timer.timeout.add(f)`, `Key.ESCAPE`) | **Works** |
| Arithmetic on Godot's math types (`position + velocity * delta` is a `Vector2`; `position + node` is an error) | **Works** |
| Language server (`kas3c lsp`): errors as you type, go to definition, find references, hover, outline | **Works** in VS Code on macOS; [Rider not yet tried, and not on Windows](editors/README.md) |
| Run an AS3 program on the command line (`kas3c run`): classes, inheritance, numbers, strings, loops | **Works** for that part of the language; the rest is refused by name |
| An AS3 script attached to a Godot node (`CharacterBody2D`), run by headless Godot 4.7.2 | **Works** for properties, method calls, operators on Godot's values (`position + velocity * delta`, `position.x = 5`), signals (`timer.timeout.add(onTimeout)`), singletons (`Input.isActionPressed("jump")`), constants (`Vector2.ZERO`) and static methods. `Int64` and global functions are next |
| Run a script in the Godot editor | Planned (stage 2d) |
| Export a release build as native code | Planned (stage 3) |
| iOS, Android, Nintendo Switch | Planned (stages 4 and 5) |
| Completion, rename, Godot's documentation in hover | Planned |
| Debugger, JetBrains plugin | Planned (stage 6) |

## Try the compiler

Before you begin, install CMake 3.22 or later, a C++17 compiler, and Python 3 (for the tests).

1. Build the compiler:

   ```bash
   cmake -S compiler -B compiler/build
   cmake --build compiler/build -j
   ```

2. Run the tests:

   ```bash
   ctest --test-dir compiler/build --output-on-failure
   ```

3. Parse a file and print its syntax tree:

   ```bash
   compiler/build/kas3c parse --dump-ast compiler/tests/corpus/Shapes.as
   ```

4. Check a program that uses Godot's API, and print what each name refers to:

   ```bash
   compiler/build/kas3c check --dump-names compiler/tests/corpus/RayQueries.as
   ```

Errors go to standard error as `<file>:<line>:<column>: error: <message>`. The exit status is 0 when there
are no errors, 1 when a file has errors, and 2 for bad usage or an unreadable file.

For what the compiler accepts and checks, and its limits, see the [compiler README](compiler/README.md).
To see the errors in an editor as you type, see [editors](editors/README.md).

## How it works

![How an .as file becomes a running game](docs/diagrams/pipeline.svg)

1. **The front end** reads your `.as` files, resolves names, and checks types. It reports every error with a
   file, line and column.
2. **The typed IR** (intermediate representation) is the single definition of what your program means. Both
   ways of running the program read it and nothing else.
3. **The interpreter** runs the IR directly. The editor and debug builds use it, so you need no C++ toolchain
   while you work, and you get hot reload and the debugger.
4. **The C++ generator** turns the IR into C++, and the platform's own C++ compiler turns that into native
   code. Release builds use it. There is no just-in-time compiler on any platform, because iOS and consoles
   forbid one.

A build is entirely one kind or the other:

| | Debug build | Release build |
|---|---|---|
| Runs your code with | The interpreter | Native code |
| You need | Nothing extra | A C++ toolchain for the target, as C# exports need the .NET SDK |
| Desktop and Android | A Godot extension that stock Godot 4.7 loads | The same, plus your game's own library |
| iOS and Nintendo Switch | Linked into the application | Linked into the application |

Both kinds must behave identically. One suite of AS3 programs runs on both, and a difference between them is
a failing test.

### Where the speed comes from

![The three kinds of call](docs/diagrams/calls.svg)

1. **Godot calls your script** (`_physicsProcess`, signal handlers). Godot calls by name with boxed
   arguments, as it does for C# and GDScript. The runtime looks the name up once and caches it.
2. **Your script calls Godot** (`moveAndSlide()`). The call goes through Godot's typed direct-call path,
   with no boxed values.
3. **Your script calls your own code** (`enemy.hit()`). In a release build this is a direct C++ call that
   never touches the engine.

### Measured so far

Before building the compiler, we wrote by hand the C++ that the compiler will generate, and measured it
against Godot C# on desktop. Lower is better. The figures are median frame times in microseconds.

| Benchmark | AS3-shaped C++ | C# | GDScript |
|---|---|---|---|
| Move many nodes | 615 | 686 | 1,405 |
| A script on every node | 1,002 | 1,682 | 2,253 |
| Signal traffic | 510 | 1,062 | 1,799 |
| Physics ray queries | 1,339 | 1,354 | 1,513 |
| N-body simulation | 60 | 63 | 9,753 |
| Allocation-heavy code | 48 | 74 | 6,397 |

- The hand-written code was ahead of C# on seven of nine benchmarks (one of them only with a compiler flag
  that turns loop unrolling off) and level on two.
- Its slow frames were steadier. On the two benchmarks that create garbage, C#'s slowest frames were 5 to 12
  times its median.
- These figures come from hand-written code, not from the compiler. Stage 3 measures real compiled AS3.

For the full tables and what was not measured, see the
[stage 0b results](docs/results/2026-11-stage-0b-desktop.md).

## Roadmap

![Roadmap](docs/diagrams/roadmap.svg)

For what each stage delivers and what is being built now, see the [roadmap](docs/roadmap.md).

## Repository layout

| Path | Contents |
|---|---|
| `compiler/` | The front end as a library (`kas3front`), the `kas3c` command-line tool, and their tests. |
| `compiler/godot/` | The generator that writes Godot's API as AS3 declarations, and its output, `godot.as`. |
| `compiler/lsp/` | The language server behind `kas3c lsp`. |
| `compiler/ir/`, `compiler/vm/` | The typed IR and the interpreter that runs it. |
| `compiler/godot_ext/` | The Godot extension: AS3 as a Godot scripting language. |
| `editors/` | How to connect an editor, and the VS Code client. |
| `docs/roadmap.md` | The plan: stages, what each delivers, and current progress. |
| `docs/backlog.md` | What reviews put off: every deferred item, owed until fixed. |
| `docs/diagrams/` | The diagrams on this page, and the script that generates them. |
| `docs/superpowers/specs/` | The design, and the performance redesign that amends it. |
| `docs/superpowers/plans/` | The step-by-step plan for each stage. |
| `docs/results/` | Benchmark results, with the raw data. |

The throwaway speed spike is kept on the `spike/stage-0b` branch.

## Design documents

- [Design](docs/superpowers/specs/2026-11-11-kumikae-as3-design.md): goals, shape, language scope and stages.
- [Performance redesign](docs/superpowers/specs/2026-11-11-performance-redesign.md): the rules that generated
  code follows, and the owner's decision on the speed goal. Where the two documents disagree, this one wins.
- [Language server](docs/superpowers/specs/2026-11-12-language-server-design.md): the plan for editor
  support. Its first slice is built.
- [Godot naming](docs/superpowers/specs/2026-11-12-godot-naming-design.md): how Godot's API is named in AS3.
  Approved.
- [Language additions](docs/superpowers/specs/2026-11-12-language-additions.md): candidates for going beyond
  AS3. Parked: no new syntax until scripts run.

## Licence

This repository is private for now and has no licence file.
