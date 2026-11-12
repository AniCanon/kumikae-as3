# Stage 2a: Interpreter Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `kas3c run Main.as` runs an AS3 program outside Godot and prints what it traces, for the part of
the language a simple gameplay class needs.

**Architecture:** The checked syntax tree is lowered to a typed, register-based IR that holds no pointers
into the tree. An interpreter runs the IR on its own frame stack, with a small runtime (strings, objects,
reference counts, errors) behind it. Neither includes a Godot header: everything the engine will supply
goes through one host interface, which has a stand-in here.

**Tech stack:** C++17, no exceptions, no RTTI, no third-party code. Python 3 for the test runner.

**Spec:** `docs/superpowers/specs/2026-11-11-kumikae-as3-design.md` ("Typed IR", "Interpreter backend",
"Language", "Memory", "Debugging"), amended by `2026-11-11-performance-redesign.md`.

## Stage 2 in slices

Stage 2 is too large for one plan. Each slice ends in something that runs.

| Slice | Delivers | Done when |
|---|---|---|
| **2a. Interpreter core** (this plan) | The IR, the interpreter, `kas3c run`, and the start of the conformance suite | A class with fields, methods, inheritance, numbers, strings and loops runs and traces the right output. |
| **2b. In Godot** | The extension: a `.as` file is a script; engine calls in both directions; generated bindings | An AS3 script on a `CharacterBody2D` moves a character, in headless Godot 4.7.2. |
| **2c. The rest of the language** | Closures, `Array`, `Vector`, `Dictionary`, `Object`, `*`, interfaces, `try`/`catch`, `switch`, `for in`, accessors, the standard library | The corpus programs run. |
| **2d. Editor features** | `[Export]`, `[Signal]`, hot reload, errors in the Godot editor, the cycle collector | Changing a script while the game runs changes the game. |

2b comes straight after 2a, before the language is widened, because the engine boundary decides how values
are represented. Finding that out after writing the library would be expensive.

## Promises earlier stages made to stage 2

Each is owed by the slice named.

| Promise | Slice |
|---|---|
| `toString()` answers Godot's `_to_string`, and calls Godot's `to_string` | 2b |
| Signals: `add`, `add(f, true)`, `addOnce`, `remove`, `removeAll`, `has`, `numListeners`; a second `add` of the same listener does nothing; `emit` | 2b (2d for `[Signal]`) |
| `object[name]` with a signal's AS3 name reaches the signal | 2b |
| Defaults declared `null` (`Vector2(0, 0)`, `[]`, `Callable()`) get their real value from Godot's description | 2b |
| A 64-bit integer type, or a ruling that `Number` stands, before bindings ship | 2b, a decision for the owner |
| AS3 `Array`, `Vector` and `Object` convert when passed to the engine; engine collections are not converted back | 2b and 2c |
| Operators on Godot's value types run as Godot's own | 2b |
| Entry points from the engine report and clear an error, and save and restore one already propagating | 2b |
| A method closure connected to a signal is a native callable with a tombstone | 2b |

## Global Constraints

- Library code builds with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
- The interpreter and runtime include no Godot header.
- `kas3c check` output does not change.
- A program the interpreter does not support yet is refused with an error that names the construct and its
  position. It never runs wrongly.
- No C++ exceptions: an AS3 error is state in the interpreter.
- Expected outputs for number formatting, integer conversion and evaluation order are checked against
  Node (`node -e`), where AS3 and JavaScript share the rule, not written from memory. A test that could not
  be checked that way says which rule of the AS3 language it follows.
- No console vendor material. Stage files by name. No downloads without the owner's yes.

## Design decisions (rulings made while planning)

1. **The IR is plain data.** Functions, instructions, constants and class descriptions refer to each other
   by index, never to the syntax tree. Reason: debug builds ship IR without the front end, so it must be
   writable to a file later. Cost: the lowering copies names and positions.
2. **Registers are typed and untagged.** A register holds an `int`, `uint`, `Number`, `Boolean` or a
   reference, and the instruction says which: an `int` add is an integer add. Untyped values (`*`) are a
   tagged value added in 2c. Until then, a program that needs one is refused.
3. **Arithmetic follows the checker's types exactly.** Where the checker says `int + int` is a `Number`,
   the IR adds doubles and converts on the store. Shortcuts that give the same answer (a wrapping add) are
   the native backend's business.
4. **The interpreter keeps its own frame stack** and never recurses in C++ for an AS3 call. Reasons: deep
   recursion becomes an AS3 error instead of a crash; the debugger can walk and resume frames; and a
   function that pauses (`await`, later) needs frames that outlive a C++ call. Cost: a call is a little
   slower than a C++ call.
5. **Debugger readiness from the first commit.** Every instruction has a source position; every function
   has a table of named locals; and each statement starts with a marker the interpreter can stop at. The
   hook is null when no debugger is attached.
6. **Errors.** A runtime error or `throw` sets a pending error and unwinds frames, recording each call
   site, as the redesign's section 1 describes. `try`/`catch` arrives in 2c; in 2a an error always reaches
   the top, prints an AS3 stack trace, and `kas3c run` exits with status 1.
7. **Reference counts go through one seam** (`retain` and `release` in one header). The design says plain
   counts on the main thread; the owner has since said main-thread-only is too conservative for
   `async`/`await`. This plan does not settle it. The seam keeps the choice open.
8. **In 2a every register that holds a reference owns it.** The ownership rules of the redesign (borrowed
   parameters, moves on last use) are optimisations for the native backend and are not built here. No
   cycle collector yet: 2d.
9. **Strings sit behind the host interface.** The design says strings are Godot's own. The stand-in here is
   a reference-counted UTF-8 string. Only what 2a needs is in the interface: concatenation, comparison,
   length, and conversion from numbers.
10. **The program's entry** is the public class of the first file given, constructed with no arguments, as
    Flash constructs a document class.
11. **Dispatch** is a `switch` in a loop. Faster techniques wait for a measurement.

## What 2a runs

Classes with fields (instance and static, with initializers), constructors, methods, `static` methods,
inheritance with `override` and `super`; `int`, `uint`, `Number`, `Boolean`, `String`, `null`; arithmetic,
comparison, logical, bitwise, assignment and compound assignment, `++`/`--`, the conditional operator;
locals; `if`, `while`, `do`, `for`, `break`, `continue`, `return`; calls with default parameters; `new`;
`this`; `is` and `as` on classes; string concatenation; `trace`; `throw` of an `Error`; several files and
packages; package-level functions.

Refused for now, by name: function values and closures, `Array`, `Vector`, `Object` and `Dictionary`,
`*`, interfaces, `try`, `switch`, `for in`, `for each`, labels, getters and setters, rest parameters
(other than `trace`'s), regular expressions, the `godot` package.

## Review Focus

1. A conversion the checker implies is missing or wrong in the IR: `int` from a `Number` (NaN, infinity,
   above 2^31), `uint` wrap, `Boolean` from a string or number, `String` from a number.
2. Evaluation order: operands left to right, the target of a compound assignment evaluated once,
   short-circuit operators, `i++` yielding the old value.
3. A reference leaked or released twice: on overwrite, on return, when an error unwinds frames, in a
   constructor that throws.
4. A refused construct that slips through and runs wrongly, or a supported one that is refused.
5. Deep recursion, a loop that never ends, and a huge allocation: an AS3 error or a clean stop, never a
   crash.

---

### Task 1: The IR and its printer

**Files:** Create `compiler/ir/ir.h`, `compiler/ir/ir_print.cpp`, `compiler/tests/test_ir.cpp`. Modify
`compiler/CMakeLists.txt` (library `kas3ir`).

**Produces:** `namespace kas3::ir`: `enum class Kind { I32, U32, F64, Bool, Str, Obj, Void }`;
`struct Instruction { Op op; uint32_t a, b, c; uint32_t position; }`; `struct Function { name, owner class
index, parameter kinds, register kinds, instructions, locals, calls' argument lists }`; `struct Class {
name, base index, field kinds and names, method table, statics }`; `struct Module { classes, functions,
strings, numbers, files }`; `std::string print(const Module &)`.

- [ ] Tests first: a module built by hand prints as expected; every `Op` has a name and an operand shape.
- [ ] Run, fail; implement; pass; commit.

### Task 2: Lowering expressions and statements

**Files:** Create `compiler/ir/lower.h`, `compiler/ir/lower.cpp`, `compiler/tests/golden/ir/*.as` with
`.ir`. Modify `compiler/tools/kas3c.cpp` (`kas3c ir <file>...` prints the module), `run_golden.py`.

**Produces:** `bool lower(Compilation &, ir::Module *r_module, Diagnostics per unit)`; refused constructs
are reported as `'<construct>' is not supported by the interpreter yet`.

- [ ] Tests first: golden IR for arithmetic with conversions, locals, control flow, calls with defaults,
  fields, `new`, `super`, static members, string concatenation; one test per refused construct.
- [ ] Run, fail; implement; pass; commit.

### Task 3: The runtime

**Files:** Create `compiler/vm/host.h` (the host interface), `compiler/vm/standalone_host.cpp`,
`compiler/vm/value.h`, `compiler/vm/object.h/.cpp`, `compiler/vm/numbers.h/.cpp`,
`compiler/tests/test_runtime.cpp`.

**Produces:** `retain`/`release`; `Object` (header: count, class); `to_int32(double)`, `to_uint32(double)`,
`number_to_string(double)`, `string_to_number`; the string interface.

- [ ] Tests first: `to_int32` and `to_uint32` on NaN, infinities, 2^31, 2^32 + 5, -1.5; `number_to_string`
  on 0, -0, 1e21, 1e-7, 0.1 + 0.2, 123456789012345680000, NaN, each checked against Node; objects freed
  when their count reaches zero, fields released.
- [ ] Run, fail; implement; pass; commit.

### Task 4: The interpreter

**Files:** Create `compiler/vm/interpreter.h/.cpp`, `compiler/tests/run/*.as` with `.out`,
`compiler/tests/run_programs.py`. Modify `compiler/tools/kas3c.cpp` (`kas3c run`), `CMakeLists.txt`.

**Produces:** `class Interpreter { explicit Interpreter(const ir::Module &, Host &); bool run_entry(); }`;
`kas3c run <file>...`: standard output is what the program traces; an uncaught error goes to standard
error with its stack trace; exit status 0, 1 for an AS3 error or a compile error, 2 for bad usage.

- [ ] Tests first, one program per area, each with its expected output: numbers and conversions; strings;
  control flow; classes and inheritance; statics and initializer order; evaluation order; recursion
  (a deep one ends in an AS3 error, not a crash); an uncaught error's trace; null access; a division and
  a modulo by zero.
- [ ] Every corpus and golden program either runs or is refused with a named construct: none crashes
  (sanitizer build).
- [ ] No leak: after each test program the count of live objects is zero.
- [ ] Run, fail; implement; pass; commit.

### Task 5: Documents

**Files:** `README.md`, `compiler/README.md`, `docs/roadmap.md` (stage 2 in slices; the editor check no
longer gates stage 2), `docs/diagrams/build.py`, `roadmap.svg`.

- [ ] Update; commit; push the branch.
