# Roadmap

This page is the plan for Kumikae AS3: what each stage delivers, how you can tell that it's done, and what is
being built now. It's written for the project owner and for anyone who joins later.

For why the project exists and how it works, see the [README](../README.md).

## Where we are

**Stage 1 of 6, the front end.** The parser is built. Name resolution is in progress. Type checking is next.

![Roadmap](diagrams/roadmap.svg)

## How to read a stage

- **Each stage ends in something you can run.** A stage isn't done because code exists. It's done when the
  "done when" line is true.
- **Each stage gets its own step-by-step plan** in [`docs/superpowers/plans/`](superpowers/plans/), written
  just before the stage starts.
- **Each stage is reviewed independently before it merges.** The reviewer's findings are fixed, each with a
  test that failed first.
- **Measured claims come with their data.** Results are in [`docs/results/`](results/), with the raw rows.

## The stages

### Stage 0: speed spike (done)

**Question:** can code compiled this way match Godot C#, before we build a compiler around the idea?

**What was built:** hand-written C++ in the shape the compiler will generate, attached to nodes as scripts,
and measured against C# and GDScript on desktop.

**Result:**

1. The first design lost to C# on four of six benchmarks.
2. A redesign, based on published research, removed almost all the cost of AS3's safety checks.
3. The second measurement was ahead of C# on seven of nine benchmarks and level on two, with steadier frames.
4. The owner accepted that result on 2026-11-11.

Read more: [performance redesign](superpowers/specs/2026-11-11-performance-redesign.md),
[results](results/2026-11-stage-0b-desktop.md).

### Stage 1: front end (in progress)

**Delivers:** a command-line compiler, `kas3c`, that reads AS3 and reports every error with a file, line and
column. Stage 1 has three parts.

| Part | Delivers | Done when | Status |
|---|---|---|---|
| 1a. Parser | `kas3c parse` | Every syntax error is reported with its position, and no input can crash the parser. | Done |
| 1b. Names | `kas3c check` | Packages, imports, classes and scopes resolve. Unknown and duplicate names are reported. | In progress |
| 1c. Types | Type checking in `kas3c check` | Every expression has a type. Type errors and definite null misuse are reported. | Next |

### Language server, first slice (proposed)

**Delivers:** `kas3c lsp`, the program an editor talks to. Errors as you type, go to definition, find
references and a document outline, in VS Code and JetBrains IDEs.

**Done when:** a typing mistake shows an error at once, and go to definition works on locals, classes,
imports and inherited members.

**Why here and not in stage 6:** it is the first thing you can use in an editor, and it makes every later
stage faster to build. The owner hasn't approved this order yet. Completion follows once types exist; the
JetBrains plugin and Godot's documentation in hover stay in stage 6.

Read more: [language server design](superpowers/specs/2026-11-12-language-server-design.md).

### Stage 2: interpreter in Godot

**Delivers:** AS3 scripts that run in the Godot editor.

**Done when:** an AS3 script on a `CharacterBody2D` moves a character in the editor. Exported properties,
signals and hot reload follow in the same stage.

**Built in this stage:** the typed IR, the interpreter, the bindings generated from Godot's API description,
and the integration that makes AS3 appear to Godot as a scripting language.

### Stage 3: native export on desktop

**Delivers:** release builds that run AS3 as native code.

**Done when:** the conformance suite passes on both the interpreter and native builds, and the speed gate
passes on real compiled AS3 (not hand-written code, as in stage 0).

### Stage 4: mobile

**Delivers:** iOS and Android, as interpreted debug builds and native release builds.

**Done when:** the same game runs on both, and the speed gate passes on devices.

### Stage 5: console

**Delivers:** Nintendo Switch through the Kumikae build.

**Done when:** the same test game runs on hardware, as a debug build and as a release build. Work that
touches console vendor material stays out of this repository.

### Stage 6: tooling

**Delivers:** the rest of the language server (Godot's documentation, code actions, rename), the debug
adapter, and one JetBrains plugin that covers Rider and the other IDEs.

**Done when:** you can set a breakpoint in Rider and step through AS3, in the editor and on a device.

Basic editor highlighting and error reporting arrive earlier, with stage 2.

### Later, designed separately

- A way to wait for a signal in the middle of a function, like GDScript's `await`.
- The Flash API (`flash.display.Sprite`, `MovieClip`) as a library on top of Godot nodes.
- Loading `.swf` files.
- Web export.

The design keeps each of these possible. No stage above delivers them.

## What is being built now: stage 1b, names

**Goal:** `kas3c check` reads a set of `.as` files as one program and works out what every name refers to.

| Step | What it does |
|---|---|
| 1. Program model | Reads several files as one program, and finds each file's package and definitions. |
| 2. Declarations without bodies | Lets the compiler know about classes it doesn't compile, such as the built-in types and, later, Godot's classes. |
| 3. Imports and type names | Resolves `import` and every type name, and reports unknown or ambiguous ones. |
| 4. Class hierarchy | Resolves `extends` and `implements`, and reports cycles and misuse. |
| 5. Members | Collects fields, methods, getters and setters, and checks `override`. |
| 6. Scopes | Resolves every name inside function bodies, including closures and hoisted `var` declarations. |
| 7. Carry-overs from the parser | Rejects `dynamic` classes, invalid assignment targets, and similar cases the parser lets through. |

## Decisions waiting on the owner

| Decision | Why it matters |
|---|---|
| Whether to build the language server's first slice before the interpreter | The [language server design](superpowers/specs/2026-11-12-language-server-design.md) proposes it. It gives you something to use in an editor sooner, and delays the interpreter by that much. |
| Permission to download an open-source AS3 codebase as a test corpus | Every AS3 file the parser has seen so far was written by a model. Real code would find gaps that our tests share. |
| When to open the repository | The repository is private and has no licence file. Before it opens, the design's description of the console fork must be trimmed. |

## How to follow progress

- **This page and the diagram** change when a stage or part changes status.
- **Repository:** `AniCanon/kumikae-as3` on GitHub, private.
- **Branches:** `main` holds finished, reviewed work. Each part is built on its own branch, such as
  `feat/frontend-names`, and merges when its review is clean.
- **To update the diagram,** change a status in [`diagrams/build.py`](diagrams/build.py), and then run
  `python3 docs/diagrams/build.py`.

## Terms

| Term | Meaning |
|---|---|
| Front end | The part of the compiler that reads source code and checks it. |
| IR | Intermediate representation: the compiler's own form of the program, between source code and running code. |
| Backend | A way of running the IR. This project has two: the interpreter and the C++ generator. |
| Debug build | A build that runs your code in the interpreter, so that the debugger and hot reload work. |
| Release build | A build that runs your code as native code. |
| JIT | Just-in-time compiler: one that produces machine code while the game runs. iOS and consoles forbid it, so this project never uses one. |
| Speed gate | The benchmark comparison against Godot C# that a stage must pass. |
| Conformance suite | AS3 programs with expected output, run on both backends to prove that they behave identically. |
