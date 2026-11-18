# `runtime/`: what generated code is built against

The C++ that [`backend_cpp/`](../backend_cpp/README.md) writes includes `kas3.h` from this folder and
nothing else from the project. The folder also holds the host that runs a compiled program: on its own as
a plain executable, or inside Godot as a program library the extension loads.

**Depends on:** `vm/` and `ir/`. It includes no Godot header. The headers generated code sees (`kas3.h` and
what it includes) stay small, because every generated file reads them and the Godot extension carries a
copy of them to build a project's program library.

## Files

| Files | Job |
|---|---|
| `kas3.h` | Everything generated code includes |
| `values.h` | The values (`Str`, `Object`, `Vector`, `Slot`), the `Context` every generated function takes first, `retain` and `release` |
| `numbers.h` | AS3's arithmetic and conversions on machine numbers, as the interpreter does them |
| `fast.h` | The inline fast paths of library operations, each for the usual case only |
| `operations.h` | What generated code calls out of line: errors and traces, a class's first use, thunks, `trace` |
| `api.h`, `api.cpp` | The table of runtime functions a program library is handed |
| `program.h` | The table a program library gives its host (`kas3_program`): classes, functions, static fields, strings and thunks, all by number |
| `layout.h` | The layout number a program library and its host must share (`RUNTIME_LAYOUT`) |
| `engine.h`, `godot_values.h` | Godot without a Godot header: handles on its objects and values, its value types as plain structs with inline math, and the engine's entry points (`EngineApi`) |
| `native_host.h`, `host.cpp` | The host: the context's state, its interpreter, and the engine it was given |
| `errors.cpp` | Raising, catching, stack traces, and the report of an uncaught error |
| `direct.h`, `direct.cpp` | Library functions called with no thunk |
| `calls.cpp`, `properties.cpp` | Fast paths that aren't inline |
| `entry.cpp` | The engine calling a function of the program |
| `stub_module.cpp`, `bridge.cpp`, `engine_thunk.cpp` | The module the host's interpreter is given, and the host's side of a thunk |
| `standalone.cpp`, `standalone_main.cpp` | A program library run as a plain program, and its `main` |

## The fixed shape

- **`Context *c`** belongs to the host, one for each thread. It holds the pending error, the call depth,
  static fields, string constants, and which classes are set up. A program library has no thread-local
  and no global that changes. Its tables are constants.
- **An object** is a `vm::Object` header followed by its slots, eight bytes each. A class is a C++ struct
  that derives from `Object` or from its base class's struct, plus a descriptor (`ClassInfo`) with its
  name, base, fields and method table. The host reads descriptors, never C++ types.
- **Reference counts** change only in `retain(c, x)` and `release(c, x)`.
- **Call depth** is counted in `c->depth`, so that Error #1023 comes at the 10,001st call, as under the
  interpreter. A generated function's frame is on the thread's stack, so a function that finds its frame
  below `c->stack_floor` raises the same error instead of running off the stack. A program run on its own
  gets a thread with a 512 MB stack, so that the count is what ends a recursion.
- **Errors** are `c->failing` and `c->pending`. A stack trace is built on the error path, by each function
  the error leaves.
- **`kas3_program`** is the one symbol a program library exports. The library is linked against nothing
  from this project: the host hands it the runtime's functions in a table (`api.h`), as it hands it the
  engine's.

### The layout number

A program library says which layout of the runtime it was built for, first in its table
(`Program::layout`). A host reads nothing more from a table that names another layout. The extension then
says the library must be built again and runs the scripts on the interpreter. A program run on its own
exits with status 2.

`RUNTIME_LAYOUT` is one number in `layout.h`. Raise it by hand with every change that would make a library
built earlier be misread. The sizes of the shared structs are asserted beside it, so a struct that changes
size without the number moving doesn't build.

### Recycled objects

An object that holds only numbers and Booleans isn't returned to the heap when generated code releases its
last reference. `release()` keeps the block on a list (`ObjectCache`), and `made_object()` gives it to the
next `new` of such a class, inline. The blocks go back to the heap when the host ends.

## The library

AS3's library is written once, in `vm/`. Generated code reaches it in two ways.

**Directly.** The parts that are functions of plain values (`vm/library.h`) are called as they are:
`String`'s methods, numbers as text, vectors and arrays, the untyped value's conversions and operators,
objects and tables created, a value's text, and a call through a function value. `ir/library_entries.h`
names the function for an instruction, and `direct.cpp` hands the values on.

**Through a thunk.** The rest still works on an interpreter's frame, so the host keeps an interpreter of
its own. A thunk is one instruction run as a function of that interpreter: generated code hands it the
registers the instruction reads and takes back what it writes. An error the instruction raises becomes the
context's, so the error and its message come from the library.

What still runs through a thunk:

- `RegExp`, `Date` and `JSON`.
- Properties by name, tables, `typeof`, and copies of Godot's arrays and dictionaries.
- `apply`, a method taken through an interface, and a function made into a Godot callable.
- `sortOn`, which reads a property by name.

The host's interpreter runs a stub module (`stub_module.cpp`). It has the program's classes, static fields
and strings. Each function's body is one `Native` instruction, which runs the generated function. That's
how the library, and Godot, call a function without knowing its body is native: a sort's comparison, a
`toString()`, `_process`.

### What a thunk costs

- **A thunk is an entry into the interpreter:** 40 to 80 ns, tens of times what most operations take.
  `fast.h` exists for that reason.
- **An instruction that asks the engine through a thunk** goes by Godot's names and boxed values.
- **A stack overflow found by the interpreter inside a thunk** leaves the generated functions in between
  without running their `finally` blocks.
- **Native code has no time limit.** Error #1502 is raised by the interpreter's loop.

## Inside Godot

Generated code never sees a Godot header. What a program needs from the engine, it's handed through the
context:

| Field | Holds |
|---|---|
| `c->engine` | The engine's entry points (`EngineApi`) |
| `c->resolved` | What the host found once for each place the program calls the engine: a method bind, a singleton's object, the defaults of parameters left out |
| `c->names` | A `StringName` for each name the program writes |

The extension fills them in `godot_ext/native_program.cpp`.

- **A handle on a Godot object has a head before it** (`HandleHead`), which generated code reads with no
  call. `object_of()` reads the object from the head while the host's count of freed objects
  (`Context::gone`) is unchanged. When the count has moved, the host looks the object up by its id, which
  is where a freed object becomes `null`.
- **A handle on an object that isn't reference counted is shared,** not copied. `shared()` and `release()`
  count its holders.
- **A value with parts in a handle is read where the handle keeps it** (`unbox()`). The host says where for
  each type.
- **A value with parts held inline is boxed wherever it crosses into what the library holds:** an
  instruction run through a thunk, a field, an untyped value, an `Array`. Only a register that can never
  be `null` is inline.
- **A place of a `Vector.<Vector2>` or `Vector.<Vector2i>` holds the value itself.** Generated code copies
  eight bytes between the place and an inline register (`vector_of()`, `place_value()`, `value_place()`).

Set `KAS3_NATIVE_COUNTS` to make the host print how many times generated code left an operation to the
library, and which. The tests hold the benchmarks to those counts (`native_counts.txt`), so a change that
loses a fast path fails a test and not only a timing.

## Rules

- **Nothing here words an AS3 error.** An error is raised by the shared library.
- **A fast path is inline in a header, and only that.** What it doesn't handle goes to `operations.h` or to
  the thunk of the same instruction.
- **No two headers that generated code sees may share a file name.** The extension writes them into one
  folder.

## Tests

The `native` group, in the normal build and the sanitizer build, and `test_runtime`.
