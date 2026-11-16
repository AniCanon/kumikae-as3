# `runtime/`: what generated code is built against

The C++ that [`backend_cpp/`](../backend_cpp/README.md) writes includes `kas3.h` from here and nothing
else of ours. The folder also holds the host a program library runs in where there is no engine: a plain
executable, which is what `tests/run_native.py` builds for every program of `tests/run/`.

| Files | Job |
|---|---|
| `kas3.h` | Everything generated code includes |
| `values.h` | The values (the runtime's own: `vm/runtime.h`'s `Str`, `Object`, `Vector`, `Slot`), the `Context` every generated function takes first, `retain`/`release`, `put`/`store` |
| `numbers.h` | AS3's arithmetic and conversions on machine numbers, as the interpreter's loop does them |
| `fast.h` | The fast paths of operations the library owns, each for the usual case only: a vector's element in range, a String joined to another, a new object of a class the collector does not look at, and the rest declared for `calls.cpp` and `properties.cpp` |
| `api.h`, `api.cpp` | Every function of the runtime generated code calls out of line, once: the table a program library is handed, and the host's side of it |
| `operations.h` | What generated code calls out of line: errors and their traces, a class's first use, thunks, `trace` |
| `program.h` | The table a program library gives its host (`kas3_program`): classes, functions, static fields, strings, thunks, all by number |
| `engine.h`, `godot_values.h` | Godot with no header of Godot's: handles on its objects and values (`Ref`, `Val`), its values with parts as plain structs with their maths inline, what the host worked out for each place the program calls the engine (`Resolved`), the engine's few entry points (`EngineApi`) |
| `native_host.h`, `host.cpp` | The host: the context's state, its interpreter, the engine it was given by what embeds it (`Embedder`: none for a program run on its own, the extension's inside Godot) |
| `errors.cpp` | Raising, catching, the stack trace, the report of an error nothing caught |
| `direct.h`, `direct.cpp` | The library's functions of plain values (`vm/library.h`) called with no thunk: one entry for each name `ir/library_entries.h` gives an instruction, and what those functions ask of whoever runs the program (`vm::Runner`) answered for generated code: an error raised is the context's at once, a function value and a `toString()` are called as the C++ they are |
| `calls.cpp`, `properties.cpp` | The fast paths that are not inline: typed and untyped values made of one another, a function value and a `toString()` called with no interpreter between; an Object's property under a String key, an element added at a vector's end |
| `stub_module.cpp`, `bridge.cpp` | The module the host's interpreter is given (every function's body one `Native` instruction, the thunks after them), and the host's side of that instruction (`vm/native_code.h`) |
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
  Error rather than run off the stack's end (`stack_low`). A function that cannot fail leaves the count to
  whoever calls it (`backend_cpp/facts.h`). The host with no engine runs a program on a
  thread with 512 MB of stack, so that the count is what ends a recursion.
- **Errors** are `c->failing` and `c->pending`. A trace is built on the error path, by each function an
  error leaves (`leave_failed`).
- **`kas3_program`** is the one symbol a program library exports.

## The library: called directly where it is a function of plain values, by thunk where it is not yet

AS3's library is written once, in `vm/`. The parts that are functions of plain values (`vm/library.h`: a
String's methods, numbers as text, vectors and Arrays, the untyped value's conversions and `is`, objects
and tables made) are called by generated code as they are: `ir/library_entries.h` names the function for an
instruction, the generator writes a call of the runtime's function of that name (`direct.h`), and
`direct.cpp` hands the values on. What such a function asks of whoever runs the program (`vm::Runner`:
raise, out of memory, call a function value, a value's text) is answered for generated code there: an
error is the context's at once, and a function value or a `toString()` is called as the C++ it is.

The rest (RegExp, Date, JSON, properties by name, a value's text, the untyped value's operators, function
values) are still members of `vm::Interpreter` that work on a frame's registers. For those, and for the
parts of them a moved function still asks for (`vm::Unmoved`), the host keeps an interpreter of its own:

- Its module (`stub_module.cpp`) has the program's classes, static fields and strings under their own
  numbers: made from the program's table for a program run on its own, and a copy of the program's own
  module where the embedder compiled it (the Godot extension). Each function's body is one `Native`
  instruction, which the interpreter hands to the host (`vm/native_code.h`) and the host carries out by
  running the generated function with the frame's arguments: that is how the library, and Godot, call a
  function without knowing its body is native (a sort's comparison, a `toString()`, `_process`).
- An error that leaves a generated function called that way goes on in the interpreter as that error,
  with the trace native code built (`hand_over`), unless generated code itself began what led to the
  call: then it stays the context's, and the interpreter only leaves its own frames.
- A **thunk** is one instruction as a function of that module. Generated code hands it the registers the
  instruction reads (`thunk(c, number, slots)`) and takes back what it writes. An error the instruction
  raises is caught inside the thunk and becomes the context's, so the error and its words are the
  library's.
- The host's own needs go the same way (`Builtin`): Error #1009 and #1023 are raised by the interpreter,
  never worded here.

What this costs, until the split:

- A thunk is an entry into the interpreter (a frame, a few instructions): 40 to 80 ns, tens of times what
  most operations take. `fast.h` exists for that reason, and each family that becomes a function of plain
  values loses its thunks.
- The interpreter lets entries nest only `MAX_ENTRIES` (64) deep, its own library's callbacks among them
  (a sort's comparison that sorts ends with Error #1023 at the 64th under `kas3c run` too). A thunk is an
  entry, so a recursion that passes through one on every level ends at the 63rd; a library function
  called directly is none, and such a recursion goes on natively where the interpreter stops (see the
  list below: the two are to agree by the stack's depth).
- Where the interpreter itself refuses an entry (its own library's calls count too), it keeps the error
  until the instruction beneath ends; generated functions in between are left without their handlers
  being offered it (`Host::carried`): a `finally` there does not run.
- A program run on its own has no limit on its time, where `kas3c run` stops one after 15 seconds
  (Error #1502): the limit is the interpreter's loop's.

## Inside Godot

- **No Godot header and no godot-cpp** in generated code, in this folder or in a program library. What a
  program needs of the engine it is handed through the context: `c->engine` (the entry points),
  `c->resolved` (for each place it calls the engine directly, what the host found once: a method bind, a
  singleton's object, the defaults of parameters left out) and `c->names` (a `StringName` for each name
  the program writes). The extension fills them from what it already works out for the interpreter
  (`godot_ext/native_program.cpp`, from `godot_prepared.cpp`'s `prepare`).
- **A value with parts held inline** is boxed into the host's handle wherever it crosses into what the
  library and the host hold: an instruction run by thunk (`Boxed`), a field, an untyped value, an Array.
  Only a register that can never be null is inline; a parameter, a field and a call's result are handles.
- **The runtime's functions reach a program library through a table** (`api.h`, `c->api`), as the
  engine's do: every out-of-line function generated code calls is one entry of `KAS3_RUNTIME_API`, an
  inline call through the table in a program library and the function itself in the host. A program
  library is linked against nothing of ours; the native oracle builds each program that way, so a function
  reached by name fails the build.

## To split out of `vm/`, so that no interpreter is needed

Done: `Heap` (who holds what, the collector, `list_for()`: `vm/heap.h`); a String's methods, `split()`,
the URI functions and numbers as text; vectors and Arrays (elements, length, making, methods, `sort` and
`sortOn`); `to_any`, `from_any`, `is` and `is_class`; `New` and `TableNew`; an Error's fields
(`new_error()`), which native code fills in for an error it raises itself.

Still used here through a thunk, or asked of the interpreter by a moved function (`vm::Unmoved` lists
exactly those), named so that the split can be checked against it:

1. The method wrappers left: `regexp_methods.cpp`, `regexp_strings.cpp` (and with them `split()` by a
   RegExp, which goes by the host's own thunk), `date_methods.cpp`, `json_methods.cpp`,
   `json_stringify.cpp`, `library_members.cpp`.
2. A value's text (`any_text`, a vector's text in `interpreter_vectors.cpp`, `object_text`), which needs
   one more thing of the runner: a function of the program called by its number with typed values. With
   it `Runner::text` and `Runner::call` become library functions themselves.
3. Properties by name and tables (`interpreter_properties*.cpp`, `interpreter_property_calls.cpp`), and the
   untyped value's operators (`interpreter_any_operators.cpp`: equality, `+`, `<`), `any_primitive` and
   Godot's Arrays and Dictionaries copied.
4. Function values (`interpreter_functions.cpp`): making one (the one value of a static function is kept
   by the interpreter), the arguments of a call through one, `apply`. Until then a call the fast path
   does not take is the library's `apply`, by the host's thunk.
5. Raising: the words of #1009, #1023 and #1000 are still raised by the interpreter for the host
   (`Builtin`), and `object_text` / `error_heading`.
6. Lifetime: freeing an object with what it holds is the heap's (`Heap::drop_object`), still reached
   through the interpreter's `free_kept()` and `discard()`; an inline allocation path on the context's
   own free lists is not written.
7. `any.h`: the tag and the payloads as inline functions, so that an untyped value is kept and let go of
   inline.
8. Classes as the host's descriptors (`ClassInfo`) where the library takes `ir::Class`: `each_held()`,
   the collector, `is_class`.
9. Callbacks nest as deep as the C++ stack allows where the library's function is called directly; the
   interpreter (and any thunk) still allows 64 calls from outside in progress, so a recursion through a
   library callback ends with Error #1023 there at the 64th. The interpreter needs the stack's floor, as
   the context has (`stack_low`), in place of that count.

## Rules

- **`runtime/` includes `vm/` and `ir/`, and no Godot header yet.** The headers generated code sees
  (`kas3.h` and what it includes) stay small: every generated file reads them.
- **Nothing here words an AS3 error.** An error is raised by the shared library.
- **A fast path is inline in a header, and only that:** what it does not handle goes to `operations.h` or
  to the thunk of the same instruction.

Tests: the `native` group, in the normal and the sanitizer builds.
