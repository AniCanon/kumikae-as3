# `vm/`: the interpreter

Runs a module of IR. It is the same program under `kas3c run`, inside Godot, and in the tests; only the
host differs.

| Files | Job |
|---|---|
| `interpreter.h`, `interpreter.cpp` | Frames, calls, errors, the public entry points |
| `interpreter_run.cpp` | The loop: one case per operation |
| `interpreter_host.cpp` | Requests to the engine, one function per operation |
| `interpreter_objects.cpp`, `runtime` | Lifetime of objects and strings |
| `interpreter_vectors.cpp`, `vector_methods.cpp` | `Vector.<T>`: an object whose class says what its elements are; making, indexing, length, its methods, its text |
| `table`, `interpreter_properties.cpp`, `interpreter_property_calls.cpp` | Objects and Dictionaries (`vm::Table`: an object whose one slot holds its `Entries`, keys and values in the order each key was first set) and an Array's properties beyond its indices; a property by name on anything (`DynGet` and the rest), a property called by its name, the loops over properties, and Godot's Dictionaries copied in |
| `reload` | Moves a running program's objects, vectors and static fields onto a newer build, by name |
| `any`, `interpreter_any.cpp` | The untyped value (`*`, `Object`): one slot, a Number as its bits and the rest tagged; ECMAScript's conversions, equality, `+`, `<` |
| `numeric`, `math.cpp` | Conversions C++ alone would leave undefined; `Math` where AS3 and C's library differ |
| `text_units`, `case_table.cpp` | Strings by UTF-16 unit: indexing, cutting, searching and joining text held as UTF-8, and changing case (the table is written by `tools/case_table.py`) |
| `number_text`, `string_methods.cpp` | Numbers as text and text as numbers (radix, `toFixed` and the rest, `parseInt`, `parseFloat`); the instructions that run `String`'s methods and those |
| `host.h` | What the interpreter is given: `Output`, `EngineHandles`, `EngineSource` |
| `output.h`, `engine_handles.h`, `engine_source.h`, `engine_types.h` | The three interfaces and the values they pass |
| `no_engine.h`, `standalone_host.cpp` | The answers where there is no engine |

## Rules

- **No Godot header, and nothing from `src/`.** It includes `ir/ir.h` and the standard library.
- **The engine is known only through the three interfaces.** Something new asked of Godot is a new entry on
  `EngineHandles` (done to a value the interpreter holds) or `EngineSource` (where a value comes from).
  `NoEngine` must answer it, and the recorder in `tests/test_vm.cpp` must record it.
- **Nothing is allocated per instruction, and no text is built on the path that succeeds.**
- **The loop is measured, not guessed.** A change to `interpreter_run.cpp` comes with
  `tests/bench/run_bench.py <build> --against <other build>`. A helper in a header has cost 3% before.
- **Every failure is an AS3 error** with a class, a message and a stack line. Handles and objects are
  released on the error path too: the tests count what is left alive.

## What a cycle collector must visit

Every counted thing a container holds, in one place per kind of container:

- an object of a class with fields: each field of kind `Obj`, and each `Any` field that holds an object;
- a vector (`klass.element != Void`): each element of kind `Obj`, or of kind `Any` that holds an object,
  for `i < length`; **and**, for an Array, its `properties` (below) when the pointer is not null;
- a table (`klass.table`, an Object or a Dictionary): `entries_of(object)` (below);
- a function value: its `FUNCTION_FIRST` field (an object, or null), as any `Obj` field.

`Entries::all()` (vm/table.h) is the one loop over a table's or an Array's properties: for each entry whose
key is not undefined, the key when it is an object (a Dictionary's object key, held strongly) and the
value when it is an object. Deleted entries have both undefined. Keys that are Strings hold no object.
Freeing goes through `Interpreter::let_go_entries`, which takes every entry out first.

Tests: `test_vm`, `test_runtime`, the `programs` group, and the sanitizer build.
