# Roadmap

This page says what Kumikae AS3 does today, what is being built, and what comes later. It changes when the
state of the project changes.

![Roadmap](diagrams/roadmap.svg)

## Goals

| Goal | What it means |
|---|---|
| First-class in Godot | Scripts extend Godot classes, attach to nodes, export properties and use signals. |
| One codebase | The same source runs on desktop, mobile and consoles, with no per-platform code. |
| As fast as C#, or faster | Release builds are native code, measured against Godot C# on the same benchmarks. |
| Easy to use | Write a script and press Play. Save, and the running game changes. |
| Easy to debug | Errors with a file and a line everywhere, and breakpoints from the IDE. |

## Built

### The compiler

- **Parser, name resolution and type checking** for ActionScript 3, checked against about 190,000 lines of
  open-source AS3.
- **Godot's whole API as typed AS3:** 1,036 classes, with operators on `Vector2` and the other math types,
  Godot's 64-bit integers as `Int64`, and opt-in binary32 arithmetic with `float` and `Vector.<float>`.
- **Every error has a file, a line and a column.**

### The language at run time

- **Typed callbacks** such as `(int)=>int` check scalar signatures and carry arguments and results in their declared register kinds.

- **Classes, interfaces, accessors, closures, rest parameters,** `try`/`catch`/`finally`, labeled `break`
  and `continue`.
- **`Array`, `Vector.<T>`, `Dictionary`, `Object` literals,** untyped values (`*`), `for in` and `for each`.
- **The standard library:** `String`, `Number`, `Math`, `JSON`, `Date`, `RegExp`, and the top-level
  functions.
- **Opt-in `byte` and `Vector.<byte>`.** Byte vectors store one byte per element and copy directly to Godot's `PackedByteArray`.
- **AS3's runtime errors,** with stack traces that name the `.as` file and line.

### Inside Godot

- **[Coin Dash](../examples/coin-dash/README.md)** is a playable ActionScript 3 example under `dev.kumikae.coindash` with modular scenes, a shared theme, typed `[Export]` actor and UI wiring, compile-time `[Signal]`s, physics pickups, a moving hazard, a dash and a saved best score.

- **Scripts on nodes:** properties, methods, signals, singletons, constants, static methods and global
  functions, with what Godot returns checked against its declared type.
- **`[Export]` and `[Signal]`,** including `[Export]` fields typed as your own `Node` and `Resource` script classes.
- **Hot reload** that keeps state and updates live objects in place. See [Hot reload](hot-reload.md).
- **Memory:** reference counting, with a cycle collector that runs in short steps each frame.
- **iOS:** the interpreter runs in the Simulator and on a device. Rider can sign, install and run a game on a paired iPhone or iPad.
- **Android:** the interpreter runs on an ARM64 emulator through Rider.
- **Web (WebAssembly):** `libkas3_godot.wasm` builds with Emscripten (`-Oz` + `wasm-opt -Oz`) as a slim
  runtime-only GDExtension (~628 KiB raw / ~194 KiB Brotli) that loads the precompiled `res://.kas3/program.kas3`
  binary IR packed automatically during Godot's Web export (plus an optional `--compiler` build with
  in-browser `kas3_web_reload` hot reload, and `compiler/tools/build_web_template.sh` for trimmed Godot Web
  templates). Claude Code starters include a Web export preset when the native kit supplies the Web runtime.
  Rider and Claude Code can build and preview the game in a browser and run gameplay fixtures in headless
  Chromium.

### Native code

- **`kas3c cpp` writes a program as C++,** and the Godot extension loads the compiled library.
- **Native iOS and Android exports.** Release exports build the AS3 scripts into the app (static objects
  linked into Xcode for iOS and the Simulator; `libkas3_program.so` compiled with the Android NDK for
  `arm64-v8a`). Debug exports default to the interpreter. The Simulator runs the native integration suite,
  and Android APK export modes are verified on macOS. Runs on iOS and Android hardware are next.
- **Every test program gives the same output** on the interpreter and as native code.
- **Level with or ahead of Godot C#** on all ten macOS frame benchmarks. See [Performance](performance.md).
- **Native code when you press Play,** on by default on macOS. The extension builds the program library
  by itself, caches it, and falls back to the interpreter when there's no C++ toolchain. A script stuck in a
  loop is stopped as on the interpreter, and an edit moves the running game onto the interpreter with its
  objects updated in place.

### Editors and tools

- **Godot Script editor:** syntax highlighting, contextual completion and saving for ActionScript files
  on desktop.
- **Language server:** errors as you type, completion, signature help, go to definition, find references,
  hover and outline. Completion includes Godot reference summaries, AS3 library and keyword descriptions,
  and ASDoc from the project. Selected API entries include compile-checked AS3 examples. A change to a
  million-line project is checked in milliseconds.
- **Rider plugin:** language support, a project wizard, `project.godot` and scene editors, run configurations
  for desktop, Web, iOS and Android, automatic Godot preset setup for new run targets, per-game companion targets in Build & Release, a Devices panel, and a live test runner that opens failed assertions,
  comparison diffs, compiler diagnostics and Godot script errors at their source.
- **Claude Code plugin** with AS3 language support, a playable starter, debug exports and gameplay tests on desktop, Web, iOS Simulator and Android emulator. [Setup](../editors/claude/README.md).
- **VS Code client** for the language server on VS Code 1.82 and newer.
- **A testing library** written in AS3, with CLI and headless Godot engine runners (`TestScene`).

## Next

1. **Run signed native builds on iOS and Android hardware, and extend native export to Windows and Linux.**
2. **Hot reload for native code.**
3. **Godot's larger values inline:** `Vector3`, `Color` and the rest in vectors, fields and parameters.
4. **Threads.**
5. **A restart action in the IDE,** to go with hot reload.
6. **Debugger:** breakpoints and stepping from Rider, in the editor and on a device.
7. **Windows and Linux.** Neither has been built or run.
8. **Consoles.** Console support lives in a separate repository, because console SDKs are confidential.

## Later

These are designed separately. Nothing above delivers them, and the design keeps each possible.

- `async` and `await`, with work off the main thread.
- Language additions from Apache Royale, such as arrow functions, `?.` and `??`.
- The Flash API (`flash.display.Sprite`, `MovieClip`) as a library on top of Godot nodes.
- Loading `.swf` files.
- Native ahead-of-time C++ WebAssembly program libraries for Web export (the interpreter Web export works today).

## Known limits

- **Only macOS is tested.** Godot 4.7 on Apple silicon is the one configuration the test suite runs on.
- **A cycle through a Godot `Array` or `Dictionary` isn't collected.** The collector follows AS3 objects,
  Godot's reference-counted objects and listeners.
- **Godot's string-format operator (`"%s" % value`) isn't available.** Use AS3 string concatenation.
- **A method of Godot's isn't a value.** `timer.timeout.add(queueFree)` is refused. Pass a method of your
  own that calls it.
- **A Godot class isn't a class value.** `var kind:Class = Node2D` is refused.
- **A Godot value or object held in a `*` or an `Object` has no members by name,** and no `for in`. Store it
  in a variable of its type first.
- **A script class with the same name as a Godot class is ambiguous** when it's in the default package. Put
  it in a package.
- **The 60 Godot methods that take raw pointers aren't exposed.**
- **`super` can't reach a virtual method implemented by another C++ extension.** Godot's extension
  interface has no such call.
- **The first editor scan of a new project can crash as Godot exits,** if Godot is told to quit at once
  (`--import`). The fault is in Godot's documentation thread. Later runs are clean.
- **The Rider plugin has been validated by automated editing checks,** and needs wider use by people.

## Design decisions

| Topic | Decision |
|---|---|
| Naming | Godot's API uses AS3 conventions: lowerCamel methods, properties instead of accessors, signals as properties. `_ready` keeps its underscore. Five Godot names that clash with AS3's get a `Godot` prefix, such as `GodotObject`. |
| Where AS3 doesn't decide | C#'s behavior in Godot is the reference. |
| No JIT | Native code is always compiled ahead of time, because iOS and consoles forbid generating code at run time. |
| Integers | A Godot integer is an `Int64` unless Godot says it's narrower. It converts to `int` or `Number` with a range check, never silently. |
| Freed Godot objects | A freed object reads as `null`. Using it is an AS3 error with a line number, never a crash. |
| Values held in place | A `Vector.<Vector2>` or `Vector.<Vector2i>` holds the values themselves and can't hold `null`, as in C#. |
| Static fields and null calls | Both follow Flash: a class's static fields are set on its first use, and a call on `null` evaluates its arguments before raising the error. |
| Signals shared by the whole game | On a singleton's signal, `removeAll()` removes only the calling script's listeners. |

## Terms

| Term | Meaning |
|---|---|
| Front end | The part of the compiler that reads source code and checks it. |
| IR | Intermediate representation: the compiler's own form of the program, between source and running code. |
| Back end | A way of running the IR. This project has two: the interpreter and the C++ backend. |
| Program library | Your scripts compiled to native code, as a library the Godot extension loads. |
| JIT | Just-in-time compiler: one that produces machine code while the game runs. This project never uses one. |
