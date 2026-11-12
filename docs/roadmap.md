# Roadmap

This page is the plan for Kumikae AS3: what each stage delivers, how you can tell that it's done, and what is
being built now. It's written for the project owner and for anyone who joins later.

For why the project exists and how it works, see the [README](../README.md).

## Where we are

**Stage 1 of 6, the front end.** The parser, name resolution and type checking are built and checked against
about 190,000 lines of real AS3. The compiler knows Godot's whole API under AS3 names, including arithmetic
on its math types, and reports its errors to an editor through a language server. The front end is
complete. The language server's first slice is built and passes its tests; it has not yet been tried by hand
in VS Code and Rider.

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
column.

| Part | Delivers | Done when | Status |
|---|---|---|---|
| 1a. Parser | `kas3c parse` | Every syntax error is reported with its position, and no input can crash the parser. | Done |
| 1b. Names | `kas3c check` | Packages, imports, classes and scopes resolve. Unknown and duplicate names are reported. | Done |
| 1c-1. Expression types | `kas3c check --dump-types` | Every expression has a type, and `a.b` resolves to a member with AS3's access rules. | Done |
| 1c-2. Type errors | Type checking in `kas3c check` | A value that doesn't fit where it is used is reported: assignments, arguments, returns, overrides, interfaces, `const`. | Done |
| 1d. Godot's API in AS3 | A generator that turns Godot's API description into AS3 declarations for the `godot` package, under the [naming design](superpowers/specs/2026-11-12-godot-naming-design.md) | `moveAndSlide()`, `position`, `timer.timeout.add(f)` and `Key.ESCAPE` all check, for all 1,036 Godot classes, and no generated name clashes with another. | Done |
| 1e. Godot's math types | Operators and indexing on `Vector2` and its relatives | `position + velocity * delta` has a type, and a wrong operand is reported. | Done |

### Language server, first slice (built; editor check pending)

**Delivers:** `kas3c lsp`, the program an editor talks to. Errors as you type, go to definition, find
references, hover, a document outline and symbol search.

**Done when:** in VS Code and Rider, a typing mistake shows an error at once, and go to definition works on
locals, classes, imports, inherited members and Godot's API.

| Part | Status |
|---|---|
| The server: protocol, errors as you type, definition, references, hover, outline, symbols | Done. Tested by scripted sessions that send what an editor sends. |
| Speed: a re-check of 100,000 lines in under 50 ms | Done: about 27 ms, by keeping the parse of files that did not change. |
| A VS Code client and the [set-up guide](../editors/README.md) | Written. Not yet run: it needs one library from npm, and Rider needs a plugin. |
| Tried by hand in VS Code and Rider | Not yet. |
| Windows | Not yet: the server runs on macOS and Linux only. To do before the slice is called done. |

**Why here and not in stage 6:** it is the first thing you can use in an editor, and it makes every later
stage faster to build. The owner left the order to the builder. Completion follows in a second slice; the
JetBrains plugin and Godot's documentation in hover stay in stage 6.

Read more: [language server design](superpowers/specs/2026-11-12-language-server-design.md),
[how the server works](../compiler/README.md#language-server).

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

- `async` and `await` as a first-class part of the language, with work off the main thread and debugger
  support. A research document comes first: how far to go beyond main-thread-only, what it costs, and what
  the debugger needs. See [language additions](superpowers/specs/2026-11-12-language-additions.md#async-and-await-research-before-design).
- The Flash API (`flash.display.Sprite`, `MovieClip`) as a library on top of Godot nodes.
- Loading `.swf` files.
- Web export.

The design keeps each of these possible. No stage above delivers them.

## What comes next: the interpreter (stage 2)

**Goal:** an AS3 script on a `CharacterBody2D` moves a character in the Godot editor.

| Step | What it does |
|---|---|
| 1. Typed IR | The checked syntax tree becomes the one form of the program that both backends read. |
| 2. Interpreter | Runs the IR. |
| 3. Godot bindings | Generated from Godot's API description: how a script calls the engine and the engine calls a script. |
| 4. The extension | Godot loads `.as` files as scripts, with exports, signals and hot reload. |

Before it starts, the language server's first slice needs its check by hand in VS Code and Rider.

## Decisions made

| Decision | Outcome |
|---|---|
| Order of the language server and the interpreter | Left to the builder. Order: type errors, Godot's API in AS3, math types, the language server's first slice, then the interpreter. |
| `async`/`await` | The owner wants it first-class, not limited to the main thread, efficient, and with a proper debugger. Research first, after stage 2; no syntax chosen yet. |
| Additions to the language | No new keywords or syntax yet; first get things working. The candidates are parked in the [language additions](superpowers/specs/2026-11-12-language-additions.md) list. |
| 64-bit integers in declarations | A Godot integer marked 64-bit is declared `Number`, which is exact to 2^53. Object ids use higher bits, so stage 2 must settle a 64-bit type before bindings ship. |
| A real AS3 test corpus | Approved. Six open-source projects (about 190,000 lines) are fetched by `compiler/tests/fetch_external.sh` and kept out of this repository. They have already corrected three rules. |
| [Godot naming design](superpowers/specs/2026-11-12-godot-naming-design.md) | Approved on 2026-11-12: lowerCamel methods (`moveAndSlide`), properties instead of accessors, signals as properties, `_ready` keeping its underscore, and a `Godot` prefix on the five names that clash with AS3's. |

## Decisions waiting on the owner

| Decision | Why it matters |
|---|---|
| When to open the repository | The repository is private and has no licence file. Before it opens, the design's description of the console fork must be trimmed. |

## How to follow progress

- **This page and the diagram** change when a stage or part changes status.
- **Repository:** `AniCanon/kumikae-as3` on GitHub, private.
- **Branches:** `main` holds finished, reviewed work. Each part is built on its own branch, such as
  `feat/frontend-types`, and merges when its review is clean.
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
