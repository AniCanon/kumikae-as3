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

Tests: `test_vm`, `test_runtime`, the `programs` group, and the sanitizer build.
