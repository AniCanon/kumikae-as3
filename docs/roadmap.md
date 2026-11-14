# Roadmap

This page is the plan for Kumikae AS3: what each stage delivers, how you can tell that it's done, and what is
being built now. It's written for the project owner and for anyone who joins later.

For why the project exists and how it works, see the [README](../README.md). For everything a review put
off, see the [backlog](backlog.md): those items are owed, not dropped.

## Where we are

**Stage 1 of 6, the front end.** The parser, name resolution and type checking are built and checked against
about 190,000 lines of real AS3. The compiler knows Godot's whole API under AS3 names, including arithmetic
on its math types, and reports its errors to an editor through a language server. The front end is
complete. The language server's first slice is built and passes its tests; it works in VS Code and is
not yet tried in Rider.

**Stage 2 is under way: four of its nine slices are done, and the fifth is two thirds built.** `kas3c run` executes AS3 on the command line
(classes, inheritance, numbers, strings, loops). In headless Godot 4.7.2 an AS3 script attached to a
`CharacterBody2D` moves the body, does arithmetic on Godot's values, listens to signals, and uses
singletons such as `Input`, Godot's constants and its static methods. Godot's 64-bit integers are an AS3
type, `Int64`. The same scripts have run on an iPhone. A clean-up of the C++ comes next, then global functions (the rest of stage 2b-3), then the
rest of the language.

**Everything a review put off has a stage that fixes it.** The [backlog](backlog.md) holds 83 items; each
is marked with its stage, and stage 3 does not start while one of them is open.

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
| A VS Code client and the [set-up guide](../editors/README.md) | Done. |
| Tried in VS Code | Done on 2026-11-12, from a log of the messages between VS Code 1.145 and the server: errors arrive on open and after a change. It found that VS Code passes `--stdio`, which is now accepted. The owner confirmed the underlined errors by eye. |
| Tried in Rider | The LSP4IJ plugin is installed in Rider 2026.2. Adding the server in its settings is not yet tried. |
| Windows | Not yet: the server runs on macOS and Linux only. To do before the slice is called done. |

**Why here and not in stage 6:** it is the first thing you can use in an editor, and it makes every later
stage faster to build. The owner left the order to the builder. Completion follows in a second slice; the
JetBrains plugin and Godot's documentation in hover stay in stage 6.

Read more: [language server design](superpowers/specs/2026-11-12-language-server-design.md),
[how the server works](../compiler/README.md#language-server).

### Stage 2: interpreter in Godot (in progress)

**Delivers:** AS3 scripts that run in the Godot editor.

**Done when:** an AS3 script on a `CharacterBody2D` moves a character in the editor, with exported
properties, signals and hot reload.

| Slice | Delivers | Done when | Status |
|---|---|---|---|
| 2a. Interpreter core | The typed IR, the interpreter, `kas3c run`, and the start of the conformance suite | A class with fields, methods, inheritance, numbers, strings and loops runs and traces the right output. | Done |
| 2b-1. In Godot, first slice | The extension: a `.as` file is a script; Godot calls its methods; it reads and writes properties, calls methods and makes `Vector2`s | An AS3 script on a `CharacterBody2D` moves the body, in headless Godot 4.7.2. | Done |
| 2b-2, part A. Godot's values | Operators and comparison on Godot's values, their text, `position.x = 5`, elements by index, defaults Godot fills in, a script's `toString()` shown by Godot | Each has a test in Godot that compares the result with GDScript's. | Done |
| 2b-2, part B. Signals | A method as a listener; `add`, `addOnce`, `remove`, `removeAll`, `has`, `numListeners`, `emit`; `timer[name].add(f)` | Each has a test in Godot. | Done |
| 2b-3. The rest of Godot's API | Singletons (`Input`), constants, global functions (`print`), Godot's own error reporting, the `Int64` type | The test programs that need no unsupported language feature run in Godot, and the backlog items marked 2b-3 are closed. | Parts A and B of three done |
| 2c. The rest of the language | Closures, `Array`, `Vector`, `Dictionary`, `Object`, `*`, interfaces, `try`/`catch`, `switch`, `for in`, accessors, the standard library. With them, what stage 1 promised that needs them: AS3 collections converting when handed to Godot, `object[name]` in general, and elements of Godot's untyped collections | The corpus programs run, and the backlog items marked 2c are closed. | Planned |
| Language server, second slice | Completion, Windows, Rider, and the backlog items marked LS-2 | Completion offers the members of a typed value in VS Code and Rider, on macOS and Windows. | Planned |
| 2d. Editor features | `[Export]`, `[Signal]`, hot reload, errors in the Godot editor, the cycle collector | Changing a script while the game runs changes the game, and the backlog items marked 2d are closed. | Planned |
| Clean-up pass | The 34 backlog items no slice touches | The backlog holds only items the owner has ruled on. Stage 3 starts after this. | Planned |

Godot comes before the rest of the language because the engine boundary decides how values are represented.

Read more: [stage 2a plan](superpowers/plans/2026-11-12-stage-2a-interpreter-core.md), which also lists what
earlier stages promised to this one.

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

## What comes next: the rest of Godot's API (stage 2b-3)

**Goal:** a script can use everything Godot offers that is not an object's own member.

| Part | What it does | Status |
|---|---|---|
| A. Singletons, constants, static methods | `Input.isActionPressed("jump")`, a singleton's signals, `Vector2.ZERO`, `Key.ESCAPE`, `Vector2.fromAngle(a)`. A Godot value with parts starts at its zero. Closes 3 backlog items. | Built and reviewed |
| B. `Int64` | The 64-bit integer type the owner approved. The [design](superpowers/specs/2026-11-14-int64-design.md) was approved on 2026-11-14. A Godot integer is an `Int64` unless Godot says it is narrower; 1,059 declarations changed and none became narrower; the table kept by hand is gone. | Built and reviewed |
| C++ clean-up | Asked for by the owner on 2026-11-14. Done: no file is over 800 lines (the largest was 3,087 and is now 723), no function is over 120 except three dispatch switches, no banner comments, rules that were written twice are written once, `vm::Host` is three interfaces, and calls into Godot are about 16% faster. No test changed, and an independent comparison of over 33,000 programs against `main` found no difference in what the compiler produces. See the [guidelines](cpp-guidelines.md), the [plan](superpowers/plans/2026-11-14-cpp-cleanup.md) and the [figures](results/2026-11-cpp-cleanup.md). | Built and reviewed |
| Early iOS test | Decided by the owner on 2026-11-14, ahead of stage 4. **AS3 runs on an iPhone and in the iOS Simulator:** the test scene passed 14 of 14 checks on both, with the same scripts as on desktop, in the interpreter. It found that an exported game could not find its scripts on any platform, now fixed. The Simulator uses an export template built from Godot's unmodified source for Apple silicon; no fork. See the [first run](results/2026-11-ios-first-run.md). | Done |
| C. Global functions and errors | `print`, `lerp`, `randf` and the rest; an AS3 error shown the way Godot shows its own. Closes 6 backlog items. | After the clean-up |

Read more: [stage 2b-3 plan](superpowers/plans/2026-11-14-stage-2b3-rest-of-godot-api.md).

**Stage 2b-2 is done, both parts.** Signals, which run now:

- A method is a listener: `timer.timeout.add(onTimeout)`, or `this.onTimeout`, or `other.onTimeout`.
- `add`, `add(f, true)` for the end of the frame, `addOnce`, `remove`, `removeAll`, `has`, `numListeners`,
  `emit`. Adding a listener twice does nothing.
- The same method of the same object is the same listener, however it is written.
- A listener goes when the object it belongs to is freed. A callable kept elsewhere then refuses to run.
- A class with no Godot class behind it can listen too.
- `timer[name].add(f)` reaches a signal by its AS3 name when the name is only known as the program runs.
- A method fits anywhere Godot takes a function: `array.filter(isBig)`.

Read more: [stage 2b-2 plan](superpowers/plans/2026-11-13-stage-2b2-values-and-signals.md).

Godot's values, which run now:

- Operators as Godot's own: `position + velocity * delta`, `-v`, `v == w`, `a == b` for two nodes, `v += w`.
- Text: `trace(v)`, `"at " + v`, `String(v)` and `v.toString()` give Godot's text for the value or object.
- One part of a value: `position.x = 5`, `position.x += 5`, `transform.origin.y = 3`.
- Elements by index: `v[0]`, `v[1] = 3`, a packed array's elements.
- Arguments left out take Godot's defaults, and so does a `null` written last for one.
- A script's `toString()` is what `str(node)` and `print(node)` show in GDScript.
- What Godot gives back is checked against the type it was declared as: a wrong one is an AS3 error.

Three promises of stage 1 need AS3's untyped value or its own collections, and moved to stage 2c with them:
AS3 `Array`, `Vector` and `Object` converting when handed to Godot; `object[name]` other than for a signal;
and the elements of Godot's untyped collections.

Open items:

- **The [backlog](backlog.md) gives every deferred item a stage:** 6 left in 2b-3, 16 in 2c, 9 in the
  language server's second slice, 14 in 2d, 34 in a clean-up pass that closes stage 2, and 4 that wait on
  the owner.
- **Listener loops:** a loop of objects that passes through a listener is found and freed. Any other loop
  waits for the cycle collector (2d).
- **Known fault:** the first editor scan of a project with the extension crashes as Godot exits. Later runs
  are clean.
- **Language server:** Rider is not yet tried (the plugin is installed); Windows is not supported.

## Decisions made

| Decision | Outcome |
|---|---|
| Order of the language server and the interpreter | Left to the builder. Order: type errors, Godot's API in AS3, math types, the language server's first slice, then the interpreter. |
| Royale's language extensions | The owner said on 2026-11-14 to support them as well: abstract classes, arrow functions, import aliases, `?.`, `??`, private constructors, strict function types, type inference, verbatim strings. With Royale's spelling, after stage 2c. See [language additions](superpowers/specs/2026-11-12-language-additions.md). |
| C++ guidelines | The owner asked on 2026-11-14 for best practices and a clean-up "for scalability before we keep going". The rules are in [C++ guidelines](cpp-guidelines.md); a check in the test suite enforces the ones a script can judge. |
| `async`/`await` | The owner wants it first-class, not limited to the main thread, efficient, and with a proper debugger. Research first, after stage 2; no syntax chosen yet. |
| Deferred review findings | The owner said on 2026-11-12 that all of them are to be fixed. They are listed in the [backlog](backlog.md) and stay there until fixed or ruled on. |
| Additions to the language | No new keywords or syntax yet; first get things working. The candidates are parked in the [language additions](superpowers/specs/2026-11-12-language-additions.md) list. |
| Which Godot integers are `Int64` | Decided on 2026-11-14: every Godot integer is an `Int64` unless Godot's description says it is narrower. `array.size()` and `randi()` are `Int64`; the table kept by hand goes. |
| 64-bit integers | Decided on 2026-11-12: supported, in the way that is easiest to maintain. A real `Int64` type, given by one rule from Godot's description with no hand-kept table, converting to `int` or `Number` with a range check and never silently. Built in stage 2b-3; until then such values are declared `Number`. |
| Static fields and null calls | Decided on 2026-11-12, and built: both follow Flash. A class's static fields are set on its first use, and a call on a null object evaluates its arguments before raising the error. |
| A real AS3 test corpus | Approved. Six open-source projects (about 190,000 lines) are fetched by `compiler/tests/fetch_external.sh` and kept out of this repository. They have already corrected three rules. |
| [Godot naming design](superpowers/specs/2026-11-12-godot-naming-design.md) | Approved on 2026-11-12: lowerCamel methods (`moveAndSlide`), properties instead of accessors, signals as properties, `_ready` keeping its underscore, and a `Godot` prefix on the five names that clash with AS3's. |

## Decisions waiting on the owner

| Decision | Why it matters |
|---|---|
| 60 Godot methods that take raw pointers are left out | AS3 has nothing to pass them. The proposal is that this stands. |
| What `"" + this` reads in a script without `toString()` | Today AS3's `[object Values]`; Godot's own text for the node is `<Node2D#123>`. |
| `removeAll()` on a singleton's signal | It removes every script's listeners on that signal, not only the caller's, as on any signal. On a singleton that reaches across the whole game. |
| Wide integers Godot cuts inside its other built-in types | `items.insert(4294967297, x)` inserts at 1 and `Color.hex` keeps 32 bits, without an error, as in GDScript. The integer vectors and `PackedInt32Array` are guarded; guarding the rest needs a short list kept by hand. See the [backlog](backlog.md). |
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
