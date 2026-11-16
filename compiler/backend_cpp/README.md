# `backend_cpp/`: the IR written as C++

The second backend. `generate()` takes a module of IR (`ir/ir.h`) and gives C++ source: one `.cpp` for each
class with code of its own, `program.h` (each class as a struct, every function declared) and `program.cpp`
(the functions of no class, and the table `kas3_program` returns). The C++ is written against the headers
in [`runtime/`](../runtime/README.md), and says exactly what the interpreter does: `tests/run_native.py`
holds every program of `tests/run/` to the same output under both.

`kas3c cpp <file>... -o <folder>` is the command.

| Files | Job |
|---|---|
| `generate.h`, `generate.cpp` | The entry point: the files, or the first thing not translated yet |
| `support.h`, `support.cpp` | What is refused, asked before anything is written; whether the program reads an Error's stack |
| `facts.h`, `facts.cpp` | What is known of every function before any is written: which cannot fail, and which fields each may read or write |
| `function_fields.cpp` | A field of the method's own object kept in a local for the whole function, when a loop uses it and nothing else in that loop can see it |
| `names.h`, `names.cpp` | Identifiers, C++ types and literals for the things of a module |
| `function_writer.h`, `function_writer.cpp` | One function: its registers as locals, who owns what, labels, the error paths and handlers, and its entry (the same function called with slots) |
| `patterns.h`, `function_simple.cpp` | The operations that are one C++ statement, as a table (arithmetic, conversions, comparisons), and constants |
| `function_objects.cpp` | Fields, static fields, classes, jumps, returns and throws |
| `function_calls.cpp` | Calls: direct, through the class's method table, by interface |
| `function_fast.cpp` | Operations the library owns, written with an inline path for the usual case and the thunk for the rest: a new object, a vector's elements, Strings, untyped values, properties under a String key, a call through a function value |
| `function_engine.cpp` | What asks Godot, where generated code does it itself: a `Vector2` held inline and its maths, a property or a method of a Godot object called on bare values by its method bind, the script's own Godot object and singletons borrowed for the whole function |
| `function_whole.cpp` | A sum of ints or uints converted back to one, as 32-bit arithmetic (exact, and three times as fast on `loop.as`) |
| `thunks.h`, `thunks.cpp` | The operations left to the library the interpreter has, each one instruction the host carries out |
| `program_writer.h`, `program_writer.cpp` | `program.h` and the program's table |

## The shape of generated code

It is fixed by the native backend's design, and `function_writer.h` says it again where it is written:

- Every function takes `Context *c` first; nothing generated is a thread-local or a global that changes.
- A register of the IR is a C++ local of its kind's type: `int32_t`, `uint32_t`, `double`, `bool`,
  `int64_t`, `Str *`, `Object *`, and `Slot` for an untyped value.
- A call between the program's functions is a C++ call. A virtual call goes through the class's method
  table (`c->classes[object->klass].code[slot]`), an interface call through its interface table.
- A register that holds a String, an object or an untyped value owns a reference, except a parameter
  that is never written, which borrows its caller's. A result is the caller's, and a register returned
  hands its reference over. (That is the only move so far: a value's last use elsewhere still takes a
  reference and gives one back.) Counts change only in `retain(c, x)` and `release(c, x)`.
- An error is `c->failing`, tested after each operation that can raise one. The function then goes to
  its handlers, or leaves by `failed`, which puts its line into the trace: nothing about lines is kept
  while a program runs. (A program that reads `getStackTrace()` is the exception: its functions keep a
  `StackFrame` each, because that stack is of an error nothing threw.)
- A function in which nothing can fail (so one that calls nothing of the program's) keeps no count of the
  depth and sets no error. A call of it looks at the depth first, in its place (`deep`, a local the caller
  sets once), and for no error afterwards; a call through a table, which may reach such a function, looks
  first as well. Error #1023 comes at the same call either way (`tests/run/deep_leaf.as`).
- A number or Boolean field of `this` that a loop uses alone is a local (`k3`) from the function's start
  to its end. Memory is brought up to date only around what may look at the field another way: a call of
  a function that touches that slot, the slot through another register, anything that may run code
  (the library, the engine, letting go of an object), and where the function or an error leaves
  (`tests/run/field_in_loop.as`).
- A null check is explicit, and left out on `this`.
- `#line` markers point at the `.as` source.

## Rules

- **A value of Godot's with parts is inline only where it can't be null,** and only the operations
  `function_engine.cpp` lists are done inline; each is in `tests/godot_native/Probe.as`, which Godot runs
  both ways.
- **Includes `ir/` only.** No header of `vm/`, `runtime/` or Godot: it writes text. What generated code
  needs from the runtime it names in that text.
- **Identical to the interpreter, or refused.** A program the backend can't translate exactly is refused
  by name in `support.cpp` (`not yet supported: <what>`), never translated to something close.
- **A new operation in C++** goes in `patterns.h` if it is one statement, with the kinds its registers must
  hold (checked in `support.cpp`); otherwise in the `function_*.cpp` that owns that kind of code. What has
  no case goes to the library by thunk, which is always right and always slower.
- **A fast path keeps the library's path for the rest.** Where an operation is written inline (a vector's
  element in range), anything unusual (null, out of range, a growing vector) is the same instruction by
  thunk, so that an error and its words are the library's, said once.
- **An optimisation comes with a number** from `tests/bench/run_native_bench.py`, measured turn and turn about.
- **Generated code builds without a warning** under `-Wall -Wextra`: the oracle builds it with `-Werror`.

Tests: the `native` group (`tests/run_native.py`), in the normal and the sanitizer builds.
