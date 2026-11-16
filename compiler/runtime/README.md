# `runtime/`: what generated code is built against

The C++ that [`backend_cpp/`](../backend_cpp/README.md) writes includes `kas3.h` from here and nothing
else of ours. The folder also holds the host a program library runs in where there is no engine: a plain
executable, which is what `tests/run_native.py` builds for every program of `tests/run/`.

| Files | Job |
|---|---|
| `kas3.h` | Everything generated code includes |
| `values.h` | The values (the runtime's own: `vm/runtime.h`'s `Str`, `Object`, `Vector`, `Slot`), the `Context` every generated function takes first, `retain`/`release`, `put`/`store` |
| `numbers.h` | AS3's arithmetic and conversions on machine numbers, as the interpreter's loop does them |
| `fast.h` | The inline fast paths: a vector's element in range, its length, a String joined to another |
| `operations.h` | What generated code calls out of line: errors and their traces, a class's first use, thunks, `trace` |
| `program.h` | The table a program library gives its host (`kas3_program`): classes, functions, static fields, strings, thunks, all by number |
| `host.h`, `host.cpp` | The host with no engine: the context's state, a program run as `kas3c run` runs it |
| `errors.cpp` | Raising, catching, the stack trace, the report of an error nothing caught |
| `calls.cpp` | A function value called from generated code without the interpreter between |
| `stub_module.cpp`, `bridge.cpp` | The interpreter the host keeps for the shared library (below) |
| `standalone.cpp`, `standalone_main.cpp` | A program library run as a plain program, on a thread whose stack holds 10,000 calls of any likely size; `main` |

## The fixed shape

- **`Context *c`** is the host's, one per thread: the pending error, the depth of calls, static fields,
  string constants, which classes have been set up. A program library has no thread-local and no global
  that changes; its tables are constants.
- **Objects** are `vm::Object` (the header) followed by one eight-byte slot for each field, at
  `sizeof(Object) + 8 * field`. A class is a C++ struct deriving from `Object` (or from its base's
  struct), and a descriptor (`ClassInfo`): name, base, a hash of its layout, each field's name, kind and
  offset, its method table by method number. The host reads descriptors, never C++ types.
- **Counts** change only in `retain(c, x)` and `release(c, x)` (`values.h`).
- **The depth of calls** is counted (`c->depth`), so that Error #1023 comes at the 10,001st call as it does
  under the interpreter. A generated function's frame is on the thread's stack, though, where the
  interpreter's are on the heap: a function that finds its frame below `c->stack_floor` raises the same
  Error rather than run off the stack's end (`stack_low`). The host with no engine runs a program on a
  thread with 512 MB of stack, so that the count is what ends a recursion.
- **Errors** are `c->failing` and `c->pending`. A trace is built on the error path, by each function an
  error leaves (`leave_failed`).
- **`kas3_program`** is the one symbol a program library exports.

## The library is the interpreter's, through thunks

Strings' and vectors' methods, RegExp, Date, JSON, properties by name, the untyped value's conversions and
function values are written once, in `vm/`, as members of `vm::Interpreter` that work on a frame's
registers. Until they are split from it (the list below), the host keeps an interpreter of its own:

- Its module (`stub_module.cpp`) has the program's classes, static fields and strings under their own
  numbers. Each function's body is one call of the bridge, which runs the generated function with the
  frame's arguments: that is how the library calls back (a sort's comparison, a `toString()`, a getter).
- A **thunk** is one instruction as a function of that module. Generated code hands it the registers the
  instruction reads (`thunk(c, number, slots)`) and takes back what it writes. An error the instruction
  raises is caught inside the thunk and becomes the context's, so the error and its words are the
  library's.
- The host's own needs go the same way (`Builtin`): Error #1009 and #1023 are raised by the interpreter,
  never worded here.

What this costs: a thunk is an entry into the interpreter (a frame, a few instructions), tens of times
what the operation itself takes; and the interpreter lets entries nest only `MAX_ENTRIES` (64) deep, so
a recursion that passes through a thunk that calls back on every level (an untyped value's `toString()`,
a sort's comparison that sorts) ends with Error #1023 earlier than the interpreter would end it. Calls
through function values, `toString()` of an object of a class, and interface calls do not pass through a
thunk, for that reason.

## To split out of `vm/`, so that no interpreter is needed

Each is used here through a thunk or a workaround, named so that the split can be checked against it:

1. The method wrappers, as functions of plain values with what they need passed in (the classes, a way to
   call a function of the program, a way to raise): `string_methods.cpp`, `vector_methods.cpp`,
   `vector_sort.cpp`, `interpreter_vectors.cpp`, `regexp_methods.cpp`, `regexp_strings.cpp`,
   `date_methods.cpp`, `json_methods.cpp`, `json_stringify.cpp`, `library_members.cpp`.
2. Properties by name and tables (`interpreter_properties*.cpp`, `interpreter_property_calls.cpp`), and the
   untyped value's conversions and operators (`interpreter_any*.cpp`): `to_any`, `from_any`, `any_text`,
   equality, `+`, `<`, `is`.
3. Function values (`interpreter_functions.cpp`): making one (the one value of a static function is kept by
   the interpreter), the arguments of a call through one, `apply`.
4. Raising: `raise(class, message)`, the words of #1009, #1023 and #1000, an Error's fields filled in, and
   `object_text` / `error_heading`. The host makes the interpreter raise each (`Builtin`).
5. Lifetime: freeing an object with what it holds (`drop_object`, now reached through `free_kept()` and
   `discard()`), and making one on the collector's list (`Collector::list_for` is the interpreter's own:
   `new` goes by thunk for that alone). With both, allocation can have the inline fast path the design asks
   for.
6. `any.h`: the tag and the payloads as inline functions, so that an untyped value is kept and let go of
   inline.
7. Classes as the host's descriptors (`ClassInfo`) where the library now takes `ir::Class`: `each_held()`,
   the collector, `is_class`.

## Rules

- **`runtime/` includes `vm/` and `ir/`, and no Godot header yet.** The headers generated code sees
  (`kas3.h` and what it includes) stay small: every generated file reads them.
- **Nothing here words an AS3 error.** An error is raised by the shared library.
- **A fast path is inline in a header, and only that:** what it does not handle goes to `operations.h` or
  to the thunk of the same instruction.

Tests: the `native` group, in the normal and the sanitizer builds.
