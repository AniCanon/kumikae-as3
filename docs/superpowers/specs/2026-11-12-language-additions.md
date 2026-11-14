# Kumikae AS3: additions to the language

Design, 2026-11-12. Status: **parked. A list of candidates, not a plan.**

## Decision

The owner decided on 2026-11-12: **no new keywords or syntax yet. First get things working.** Until a
script runs in Godot (stage 2), the language is AS3 as Adobe defined it, and every valid AS3 program is
accepted.

**What is needed to make things work is allowed** (the owner, same day): metadata such as `[Export]` and
`[Signal]`, and anything else a running script can't do without. What is parked is the optional kind.

ActionScript 3 (2006) is the last version of the language. Adobe cancelled its successor and ECMAScript 4
was abandoned, so any later version would be this project's to define. This page keeps the candidates in
one place so that they are decided together, not one at a time as they come up.

## What an addition costs

| Kind | Example | Cost |
|---|---|---|
| A new check | Reporting a handler whose parameters don't match the signal | None for valid programs. |
| Metadata or a library type | `[Export]`, `[Signal]`, `Signal` | None: metadata is already AS3 syntax. |
| New meaning for valid syntax | `position.x = 5` writes the vector back; `a + b` on `Vector2` | Other AS3 tools accept the file but don't know the meaning. |
| New syntax or a keyword | `await`, `enum`, `String?` | The file is no longer AS3. JetBrains' own ActionScript support flags it until this project's plugin replaces it. |

The first three kinds are allowed now. The fourth is what the decision parks.

## Already in the design

- `position.x = 5` on a Godot property (naming design, section 6).
- Arithmetic on Godot's math types (stage 1e).
- `[Export]` and `[Signal]`, and signals as properties.
- AS3's collections convert when they are handed to the engine.

## Candidates

| Candidate | Why | Needs new syntax? |
|---|---|---|
| `async` and `await`, with work off the main thread | Sequences without callback chains, and heavy work (a download, loading a level) that does not stall the game. The owner wants this first-class; see below. | Yes. |
| A 64-bit integer type | Godot integers are 64-bit, and object ids don't fit a `Number`. Stage 2 must settle this anyway. | No: a library type would do. |
| Real enums | A Godot enum is an `int` today, so a wrong constant is not reported. | No, if done as a check using the enum recorded in the declarations. Yes, for scripts to declare their own. |
| Types that can't be `null` | Null errors found before running. | Yes. |
| Typed function types | A signal handler's parameters checked against the signal. | No, as a check on `connect`. Yes, as a written type. |
| `getNode` without the `as` cast | `getNode("Hitbox") as Area2D` is the most repeated line in a script. | No: metadata on a field, as SwiftGodot's `@Node`. |
| Abstract classes and private constructors | Apache Royale's AS3 compiler offers both. | Yes. |

## Async and await: research before design

The owner's direction, 2026-11-12:

- **First-class,** not a library form: `async` functions and `await`, with a proper debugger (step over an
  `await`, see where a paused function is waiting and what started it).
- **Not main thread only.** That is too conservative: heavy work such as a background download must not
  stall the game.
- **Performance and efficiency matter.** An `await` that finishes at once should cost close to nothing, and a
  paused function should hold little memory.

No syntax or runtime is chosen yet. Before the design is written, a research document answers these, with
measurements where a number decides it:

| Question | What to compare |
|---|---|
| Is the larger, multi-threaded design worth its cost, and how much of it? | Main thread only (GDScript's `await`); main thread plus background tasks that hand results back (C#'s `Task` on a thread pool); full structured concurrency with checked thread safety (Swift's tasks, actors and `Sendable`). |
| How is a paused function represented? | A state machine written by the compiler (C#), against separate stacks. It must work in the interpreter and in generated C++, with no JIT, on every target including consoles. |
| What may a background task touch? | Godot's scene tree is not thread-safe. Whether the compiler can prove a task touches no node, or the runtime must hop back to the main thread, decides how much checking the language needs. |
| What does it cost? | Time per `await`, memory per paused function, and the cost of a thread hop, measured against C# in Godot and GDScript, as the speed spike did. |
| What happens to a paused function when its node is freed? | Cancellation, and what `try`/`catch`/`finally` see. |
| What does the debugger need? | Stepping across an `await`, a paused function's variables, and a call chain that crosses the pause. This shapes the debug adapter in stage 6. |
| What do AS3 programmers expect? | JavaScript's `async`/`await` (the same language family), and what Apache Royale and Haxe did. |

**When:** after stage 2, when the interpreter exists to measure with, and before the debug adapter is
designed, so that the two are designed together.

## Prior art: Apache Royale's optional extensions

Apache Royale, the AS3 compiler that came out of Apache Flex, extends the language with features that are
each switched on by a compiler flag: abstract classes, arrow functions, import aliases, the null
conditional operator (`?.`), the nullish coalescing operator (`??`), private constructors, strict function
types, type inference, and verbatim strings. Its documentation warns that other AS3 compilers do not
accept them. Source: <https://apache.github.io/royale-docs/features/as3>.

**Owner, 2026-11-14:** "we should add to support the version as well." Read as: this compiler is to
support Royale's version of the language, these nine extensions, as well as Adobe's. (If the owner meant
something else by "the version", this entry is wrong and is to be corrected.) Each takes Royale's spelling
and Royale's meaning, so that AS3 written for one compiler reads the same in the other. They are built
after stage 2c, when the rest of Adobe's language runs, each with its own short design; whether each sits
behind a flag, as in Royale, is decided there.

## When to return to this

After stage 2, when scripts run and real use shows which of these hurts most. The candidates that need no
new syntax can be proposed earlier, each as its own small design.
