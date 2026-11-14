# C++ Clean-up — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the C++ in `compiler/` follows `docs/cpp-guidelines.md`, so that the stages still to come are
built on files a person can hold in their head, and nothing got slower.

**Architecture:** No behaviour changes. Each oversized class keeps one header and has its methods moved,
unedited, into files named for what they do. Rules written twice are written once. Then the C++17 and
speed changes, each measured.

**Tech stack:** C++17, CMake, Python for the style check and the benchmarks.

**Spec:** `docs/cpp-guidelines.md` (the owner's request of 2026-11-14: "massive god files, breaking the
solid and dry principles", banner comments, redundant comments; "a cleanup for scalability before we
keep going").

**Branch:** `chore/cpp-cleanup`, from `main` once `Int64` is merged.

## Where it stands (measured on `feat/int64`)

| File | Lines | Longest function | Functions over 60 lines |
|---|---|---|---|
| `ir/lower.cpp` | 3,087 | `binary`, 202 | 11 of 86 |
| `src/names_types.cpp` | 1,734 | `compute`, 167 | 5 of 59 |
| `vm/interpreter.cpp` | 1,394 | `run`, 579 | 2 of 35 |
| `src/names_bodies.cpp` | 723 | | |
| `src/parser_expr.cpp` | 673 | | |
| `lsp/server.cpp` | 671 | `handle`, 71 | 2 of 30 |
| `godot_ext/godot_host.cpp` | 617 | | 0 of 25 |
| `godot_ext/callable.cpp` | 435 | `sweep_listeners`, 226 | 1 of 13 |

38 banner comments, in 11 files. Rules written twice: the whole-number rule and the reading of a wide
literal (checker and lowerer), a member's resolved symbol, a tag on a declaration, the null-error text
(8 times), the write of a Boolean register (about 20 times), the gathering of arguments for Godot (4
times).

## Global Constraints

- No test file changes, except to add tests. The IR golden files and every expected output stay byte for
  byte. `kas3c check` output does not change.
- A move is a commit of its own with no edits in it.
- The suite is green after every commit. The sanitizer build is run at the end of each task.
- No benchmark gets worse by more than the spread between its own runs.
- No console vendor material. Stage files by name. No attribution lines.

## Review Focus

1. A method that changed while it moved: compare each moved body with `git diff --color-moved`.
2. A static or file-local helper that two new files now each have a copy of.
3. A cache added for speed that outlives what it caches: a `StringName` kept past extension shutdown, a
   singleton handle kept across a hot reload, a map keyed by a `string_view` whose text is freed.
4. An `std::optional` or structured binding that copies where the old code referred.
6. The split of `vm::Host`: an entry placed in the wrong interface (one that a values-only host would
   need from the objects interface), and a host that can be built with a null engine interface.
5. The style check itself: a rule it enforces that the guide does not state, or the reverse.

---

### Task 1: Benchmarks and a baseline

**Files:** `compiler/tests/bench/run_bench.py` (new), `compiler/tests/bench/loop.as`,
`compiler/tests/bench/engine_calls/` (a small Godot project), `docs/results/2026-11-cpp-cleanup.md`.

**Produces:** `run_bench.py <build dir> [--godot]`, printing one line per benchmark: the median of 7 runs
and their spread. Four benchmarks: a full check of a generated 100,000-line project; a re-check after one
edit (the existing `bench_recheck`); `kas3c run` of a numeric loop with method calls; in headless Godot,
200,000 calls from AS3 into the engine (a property read and write, a method call, a `Vector2` operator, a
singleton method).

- [ ] Write the benchmarks; run them three times and check the medians agree within their spread.
- [ ] Record the baseline, with the commit, in `docs/results/2026-11-cpp-cleanup.md`. Commit.

### Task 2: The style check

**Files:** `compiler/tools/check_style.py` (new), `compiler/CMakeLists.txt` (a `style` test).

**Produces:** the check of section 1 and of banners in the guide, with a list of the files and functions
that are over a limit today. The list may only shrink: a file not on it that goes over fails, and so does
a listed file that is under its limit and still listed.

- [ ] Test first: the check run on a fixture with a long file, a long function and a banner reports each.
- [ ] Implement; add to CTest; commit with today's list.

### Task 3: Say it once

**Files:** `compiler/src/literals.h/.cpp` (new: `wide_literal`), `src/ast.h` (`Member::resolved()`, a
`whole` flag on `Expr` set by the checker), `src/names.h` (`has_tag`), `vm/interpreter.cpp`
(`NULL_REFERENCE`, `set_bool`), `godot_ext/godot_host.cpp` (`Arguments`: values and pointers for a call).

- [ ] One helper per commit; each replaces every copy. Suite green; commit.

### Task 4: Split the lowerer

`ir/lower.cpp` becomes one header, `ir/lowerer.h` (the class: its state and its method declarations), and:

| File | Holds |
|---|---|
| `lower.cpp` | `lower()`, the module's tables (strings, numbers), units and classes in order |
| `lower_types.cpp` | Kinds of types, conversions, default values |
| `lower_places.cpp` | `Place`, load and store, fields, globals |
| `lower_engine.cpp` | Godot's members: properties, constants, singletons, parts of a value, zero values |
| `lower_expressions.cpp` | `evaluate`, literals, unary, conditional |
| `lower_operators.cpp` | Arithmetic, comparison, equality, `Int64`, Godot's operators |
| `lower_calls.cpp` | Calls, arguments, bound methods, signals by name, static calls |
| `lower_statements.cpp` | Statements, locals, loops |
| `lower_classes.cpp` | Classes, constructors, static initialisers, functions |

- [ ] Move, one file per commit, no edits. Then split `binary`, `evaluate`, `statement`, `declare_class`,
  `call` and the rest of the 11 long functions into named parts, one function per commit.
- [ ] Banners removed. Style list shrinks. Suite green in both builds; commit.

### Task 5: Split the checker

`src/names_types.cpp` becomes `src/typer.h` and `type_table.cpp`, `typer_conversions.cpp`,
`typer_symbols.cpp`, `typer_expressions.cpp`, `typer_operators.cpp`, `typer_godot.cpp` (operators and
indexing a declaration describes), `typer_statements.cpp`. `names_bodies.cpp` and `parser_expr.cpp` are
brought under 500 lines the same way.

- [ ] As task 4.

### Task 6: Split the interpreter

`vm/interpreter.cpp` becomes `interpreter.cpp` (frames, calls, errors, the public entry points),
`interpreter_run.cpp` (the loop), `interpreter_host.cpp` (`host_op`), `interpreter_objects.cpp`
(lifetime), and `vm/numeric.h/.cpp` (`to_int32`, `clamp_wide`, `compare_wide`, `string_to_wide`). In the
loop each case of more than three lines calls a named `inline` function, so that the loop reads as a
table. `host_op` becomes one function per operation.

- [ ] As task 4, and the loop benchmark is run after each commit that touches the loop.

### Task 7: The extension and the language server

`godot_ext/callable.cpp`: `sweep_listeners` (226 lines) becomes `listener_sweep.cpp`, one function per
step of the sweep. `godot_host.cpp` is split into `godot_host.cpp` (calls) and `godot_values.cpp`
(`to_variant`, `from_variant`, `arrives`). `lsp/server.cpp`: `handle` and `notification` become a table
of method names to handlers.

- [ ] As task 4.

### Task 7a: Split `vm::Host` (added by the owner, 2026-11-14)

`vm::Host` has about 15 entries, and every host must answer all of them: the stand-alone host answers 13
with "there is no engine here". It becomes three interfaces in `vm/`, each in its own header:

| Interface | Entries | Who implements it |
|---|---|---|
| `Output` (`output.h`) | `print`, `error` | Every host |
| `EngineHandles` (`engine_handles.h`) | What is done to a value the interpreter holds, whether one of Godot's values or objects: `copy`, `drop`, `text`, `call`, `get`, `set`, `member`, `index_get`, `index_set`, `operate` | Godot, the tests' recorder |
| `EngineSource` (`engine_source.h`) | Where values come from when none is held: `construct`, `constant`, `singleton`, `call_static`, `bind` | Godot, the tests' recorder |

(The first cut was by "values" and "objects". The review showed that it separated nothing: a property
read and a method call work on both. This one follows whether the interpreter holds a handle.)

`vm::Host` stays as the one thing the interpreter is given: a small struct holding an `Output &` and the
two engine interfaces. Where there is no engine it holds `NoEngine`, one class in `vm/no_engine.h` that
answers every engine entry with the same message. A host that implements one engine interface and not
the other does not compile. `Argument` and `Wanted` are in `vm/engine_types.h`.

- [ ] Tests first: `StandaloneHost` compiles implementing `Output` only; a program that uses a Godot
  value under `kas3c run` still fails with "there is no engine here"; the tests' recorder implements the
  two engine interfaces.
- [ ] Split; the interpreter's `host_op` calls through the interface each operation belongs to. The
  engine benchmark is run before and after: one more pointer hop per call must not show.
- [ ] Suite green in both builds; commit.

### Task 8: C++17 and speed

Each is one commit with its numbers, kept only if no benchmark is worse:

- [ ] `GodotHost` keeps each `StringName` it makes (method, property and class names), keyed by the
  module's string, and each singleton it looks up. Dropped at unload.
- [ ] `value_type()` is a table built once, not a walk over Godot's types for each call.
- [ ] The lowerer's `string()`, `number()` and `wide()` find a constant through a hash map.
- [ ] `std::function` leaves `engine_call` and `walk`; `Place::part` is a `std::unique_ptr` or a value.
- [ ] `std::optional` results replace `bool` plus out-parameter in the helpers touched by tasks 3 to 7,
  where it shortens the caller. `[[nodiscard]]` on `vm::Host`'s entries and on `lower()`.
- [ ] Structured bindings and `if` with an initialiser where a pair or an iterator is unpacked.

### Task 9: Documents, review, fix pass

- [ ] `docs/results/2026-11-cpp-cleanup.md`: before and after, per benchmark. `compiler/README.md`: the
  map of files. `docs/roadmap.md`, diagram, `docs/backlog.md`.
- [ ] One independent review of the branch on the most capable model, with the Review Focus above; one
  fix pass. The style list is empty, or each entry left has a line in the backlog saying why.
