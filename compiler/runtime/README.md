# `runtime/`: what generated code is built against

The C++ that [`backend_cpp/`](../backend_cpp/README.md) writes includes `kas3.h` from here and nothing
else of ours. The folder also holds the host a program library runs in where there is no engine: a plain
executable, which is what `tests/run_native.py` builds for every program of `tests/run/`.

| Files | Job |
|---|---|
| `kas3.h` | Everything generated code includes |
| `values.h` | The values (the runtime's own: `vm/runtime.h`'s `Str`, `Object`, `Vector`, `Slot`), the `Context` every generated function takes first, `retain`/`release`, `put`/`store` |
| `numbers.h` | AS3's arithmetic and conversions on machine numbers, as the interpreter's loop does them |
| `fast.h` | The fast paths of operations the library owns, each for the usual case only: a vector's element in range, the place `push()` writes in a vector with room, a String joined to another, a new object of a class the collector does not look at, and the rest declared for `calls.cpp` and `properties.cpp` |
| `api.h`, `api.cpp` | Every function of the runtime generated code calls out of line, once: the table a program library is handed, and the host's side of it |
| `operations.h` | What generated code calls out of line: errors and their traces, a class's first use, thunks, `trace` |
| `program.h` | The table a program library gives its host (`kas3_program`): classes, functions, static fields, strings, thunks, all by number |
| `layout.h` | The number of the layout a program library and its host share (`RUNTIME_LAYOUT`), which a library's table starts with, and the sizes that hold it to the structs |
| `engine.h`, `godot_values.h` | Godot with no header of Godot's: handles on its objects and values (`Ref`, `Val`), its values with parts as plain structs with their maths inline, what the host worked out for each place the program calls the engine (`Resolved`), the engine's few entry points (`EngineApi`) |
| `native_host.h`, `host.cpp` | The host: the context's state, its interpreter, the engine it was given by what embeds it (`Embedder`: none for a program run on its own, the extension's inside Godot) |
| `errors.cpp` | Raising, catching, the stack trace, the report of an error nothing caught |
| `direct.h`, `direct.cpp` | The library's functions of plain values (`vm/library.h`) called with no thunk: one entry for each name `ir/library_entries.h` gives an instruction, and what those functions ask of whoever runs the program (`vm::Runner`) answered for generated code: an error raised is the context's at once, a function value and a `toString()` are called as the C++ they are |
| `calls.cpp`, `properties.cpp` | The fast paths that are not inline: typed and untyped values made of one another, a function value and a `toString()` called with no interpreter between; an Object's property under a String key, an element added at a vector's end |
| `entry.cpp` | The engine calling a function of the program: the generated function itself, with no interpreter between, where nothing of the interpreter's is in progress (`Host::enter`); an error is reported, or kept for the call of the engine that generated code is making (`call_failed`), as `vm::Interpreter::call()` does for its own |
| `stub_module.cpp`, `bridge.cpp` | The module the host's interpreter is given (every function's body one `Native` instruction, the thunks after them), and the host's side of that instruction (`vm/native_code.h`) |
| `standalone.cpp`, `standalone_main.cpp` | A program library run as a plain program, on a thread whose stack holds 10,000 calls of any likely size; `main` |

## The fixed shape

- **`Context *c`** is the host's, one per thread: the pending error, the depth of calls, static fields,
  string constants, which classes have been set up. A program library has no thread-local and no global
  that changes; its tables are constants.
- **Objects** are `vm::Object` (the header) followed by its slots, eight bytes each. Which slot a field is
  in is said in `vm/fields.h` for everything that has the class only as the program runs (the host, the
  library, the collector, Godot reading a property), and by the generator for generated code, which
  writes `field<T>(object, slot)` (`values.h`) and each field's place in bytes in the class's descriptor:
  today slot and field number are the same, one slot a field. A class is a C++ struct deriving from `Object` (or from its base's
  struct), and a descriptor (`ClassInfo`): name, base, a hash of its layout, each field's name, kind and
  offset, its method table by method number. The host reads descriptors, never C++ types.
- **Counts** change only in `retain(c, x)` and `release(c, x)` (`values.h`).
- **Objects of numbers and Booleans** that the collector does not look at are not given back to the heap
  when generated code lets go of the last reference: `release()` keeps the block on a list for its number of
  fields (`ObjectCache`, the host's, reached through `c`), and `made_object()` takes the next `new` of such a
  class from it, inline. A block is the heap's own all the while, so the library and the interpreter free
  and make such objects as ever; what the cache holds goes back when the host ends.
- **The depth of calls** is counted (`c->depth`), so that Error #1023 comes at the 10,001st call as it does
  under the interpreter. A generated function's frame is on the thread's stack, though, where the
  interpreter's are on the heap: a function that finds its frame below `c->stack_floor` raises the same
  Error rather than run off the stack's end (`stack_low`). A function that cannot fail leaves the count to
  whoever calls it (`backend_cpp/facts.h`). The floor is the thread's (`vm/stack.h`), the one the
  interpreter refuses a call from outside at. A program run on its own, by either, runs on a
  thread with 512 MB of stack, so that the count is what ends a recursion.
- **Errors** are `c->failing` and `c->pending`. A trace is built on the error path, by each function an
  error leaves (`leave_failed`).
- **`kas3_program`** is the one symbol a program library exports.
- **A program library says which layout of the runtime it was built for,** first thing in its table
  (`Program::layout`, `layout.h`). A host reads nothing more of a table that names another: the extension
  says the library was built for another version of the runtime and must be built again, and the
  interpreter runs the scripts; a program run on its own says so and ends with status 2. `RUNTIME_LAYOUT` is
  one number, raised by hand with every change a library built before it would be misread after (`layout.h`
  lists them); the sizes of the shared structs are asserted beside it, so a struct that changes size without
  the number moving does not build.

## The library: called directly where it is a function of plain values, by thunk where it is not yet

AS3's library is written once, in `vm/`. The parts that are functions of plain values (`vm/library.h`: a
String's methods, numbers as text, vectors and Arrays, the untyped value's conversions and `is`, objects
and tables made, a value's text, a function value called) are called by generated code as they are: `ir/library_entries.h` names the function for an
instruction, the generator writes a call of the runtime's function of that name (`direct.h`), and
`direct.cpp` hands the values on. What such a function asks of whoever runs the program (`vm::Runner`:
raise, out of memory, a function of the program run by its number on typed values) is answered for
generated code there: an error is the context's at once, and a function of the program is the C++ it is,
called through its entry.

The rest (RegExp, Date, JSON, properties by name, `apply`)
are still members of `vm::Interpreter` that work on a frame's registers. For those, and for the
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
- The host's own needs go the same way (`Builtin`): Error #1009 is raised by the interpreter, never
  worded here. Error #1023 is the library's own function (`Library::raise_overflow`).

What this costs, until the split:

- An instruction that asks the engine is carried out with no frame (`engine_thunk.cpp`): the interpreter's
  own request on the values handed over, and a function Godot calls back meanwhile is entered as generated
  code (`entry.cpp`). It still goes by Godot's names and boxed values, which is most of what it costs. A
  signal's `emit()` no longer does: generated code hands the host the values as it holds them
  (`EngineApi::emit`, `GIVEN_WHOLE` and the rest in `engine.h`).
- Any other thunk is an entry into the interpreter (a frame, a few instructions): 40 to 80 ns, tens of times what
  most operations take. `fast.h` exists for that reason, and each family that becomes a function of plain
  values loses its thunks.
- Where the interpreter itself refuses an entry (the stack nearly used up), it keeps the error
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
- **A handle on one of Godot's objects has a head before it** (`HandleHead`, `engine.h`), the host's, which
  generated code reads with no call. One on an object that is not reference-counted is shared: `shared()`
  and `release()` count a holder there, and only the last one's letting go is the host's (`drop_handle`).
  `object_of()` reads the object the host found behind the handle while the host's count of objects
  freed (`Context::gone`) is what it was when the host found it; when the count has moved, or nothing was
  found yet, the host looks by the object's id (`EngineApi::object`), which is where a freed object is
  null. Only a handle on an object is looked at this way: a value lent for a call has no head.
- **A register given a field's object again keeps its handle** (`store_again()`) where the field still
  holds the same object: the very handle, where it is one that is shared, or another handle on the object
  the host found behind both, which Godot still has. No handle is made or let go of, and none is looked
  behind by its id once each has been asked.
- **What a method gives in a handle is made there** (`EngineApi::call_kept`): a Dictionary, an Array, a
  Callable or a RID that a method bind gives is written by Godot over the value a new handle keeps, which
  starts as the zero of the type the host keeps for the place. A method of such a value is called where a
  handle keeps the value (`value_in()`, by what the host found for the place: the number such a handle
  starts with, and where the value is), and an element of a Dictionary under a key the program wrote is
  asked for as the value with parts it is to be (`EngineApi::keyed`).
- **A value with parts in a handle is read where the handle keeps it** (`unbox()`): the host says where for
  each type (`EngineApi::value_at`, `value_type`), found from what Godot says of a handle the host makes;
  a type a handle keeps elsewhere, and a handle on anything else, are the host's `unbox`.
- **A value with parts held inline** is boxed into the host's handle wherever it crosses into what the
  library and the host hold: an instruction run by thunk (`Boxed`), a field, an untyped value, an Array.
  Only a register that can never be null is inline; a parameter, a field and a call's result are handles.
  A place of a vector of `Vector2`s or of `Vector2i`s holds the value itself (`vm/README.md`), and an
  element of one read by its index is never null: generated code copies the eight bytes between the place
  and a register that holds the value inline (`vector_of()`, `place_value()`, `value_place()`), and code
  that holds a handle is kept off such a place by the same test.
- **The runtime's functions reach a program library through a table** (`api.h`, `c->api`), as the
  engine's do: every out-of-line function generated code calls is one entry of `KAS3_RUNTIME_API`, an
  inline call through the table in a program library and the function itself in the host. A program
  library is linked against nothing of ours; the native oracle builds each program that way, so a function
  reached by name fails the build.

## To split out of `vm/`, so that no interpreter is needed

Done: `Heap` (who holds what, the collector, `list_for()`: `vm/heap.h`); a String's methods, `split()`,
the URI functions and numbers as text; vectors and Arrays (elements, length, making, methods, `sort` and
`sortOn`); `to_any`, `from_any`, `is` and `is_class`; `New` and `TableNew`; an Error's fields
(`new_error()`), which native code fills in for an error it raises itself; a value's text (`any_text`,
`vector_text`, `object_text`, `any_primitive`) and a call through a function value (`function_call`), which
run the program's own functions by number (`Runner::run`: for generated code, the function's entry); the
untyped value's equality, `+`, `<`, `-` and the rest, which ask Godot first for one of its values
(`any_operate`), and a vector's element compared (`element_is`).

Still used here through a thunk, or asked of the interpreter by a moved function (`vm::Unmoved` lists
exactly those), named so that the split can be checked against it:

1. The method wrappers left: `regexp_methods.cpp`, `regexp_strings.cpp` (and with them `split()` by a
   RegExp, which goes by the host's own thunk), `date_methods.cpp`, `json_methods.cpp`,
   `json_stringify.cpp`, `library_members.cpp`.
2. `sortOn`'s read of an element's property (`Unmoved::sort_field`), which is a property by name: it
   goes with 3.
3. Properties by name and tables (`interpreter_properties*.cpp`, `interpreter_property_calls.cpp`), `typeof`,
   and Godot's Arrays and Dictionaries copied.
4. Function values (`interpreter_functions.cpp`): `apply`, a method taken through an interface, and one
   made into a Callable. Making one (the one value of a static function is kept by the heap), its `length`
   and a call through one are the library's functions already. A class called as a function, which
   converts what it is given (`class_call`), is the interpreter's: generated code looks first whether the
   value is a function (`holds_function`), and anything else goes by the instruction's thunk.
5. Raising: the words of #1009 and #1000 are still raised by the interpreter for the host
   (`Builtin`), and `object_text` / `error_heading`.
6. Lifetime: freeing an object with what it holds is the heap's (`Heap::drop_object`), still reached
   through the interpreter's `free_kept()` and `discard()`; an inline allocation path on the context's
   own free lists is not written.
7. `any.h`: an untyped value that holds something counted (a String, an object, an Int64's box, a handle)
   is still kept and let go of by a call (`shared_any`, `release_any_slow`). The tag, the payloads and
   whether a value holds anything are inline (`vm/any.h`): a number, a Boolean, null and undefined cost no
   call, and a Number in an int's range is made untyped where it is (`number_any()`).
8. Classes as the host's descriptors (`ClassInfo`) where the library takes `ir::Class`: `each_held()`,
   the collector, `is_class`.

## Rules

- **`runtime/` includes `vm/` and `ir/`, and no Godot header yet.** The headers generated code sees
  (`kas3.h` and what it includes) stay small: every generated file reads them.
- **Nothing here words an AS3 error.** An error is raised by the shared library.
- **A fast path is inline in a header, and only that:** what it does not handle goes to `operations.h` or
  to the thunk of the same instruction.

Tests: the `native` group, in the normal and the sanitizer builds.
