# `vm/`: the interpreter

Runs a module of IR. It is the same program under `kas3c run`, inside Godot, and in the tests; only the
host differs.

| Files | Job |
|---|---|
| `interpreter.h`, `interpreter_records.h`, `interpreter.cpp` | Frames, calls, the public entry points; the records the class keeps (a frame, a kept trace, a property's key and the rest) |
| `interpreter_errors.cpp` | Errors: raising one, catching it, its stack trace, and reporting it to the host |
| `interpreter_run.cpp` | The loop: one case per operation |
| `interpreter_conversions.cpp`, `interpreter_time_limit.cpp` | What the loop hands on: an Int64 converted with a check, an object as text; the time limit on a call from outside (Error #1502) |
| `interpreter_host.cpp` | Requests to the engine, one function per operation |
| `interpreter_godot_refuses.cpp` | Why Godot can't hold an untyped value, checked before a request is made |
| `interpreter_objects.cpp`, `runtime` | Lifetime of objects and strings |
| `held.h` | What an object holds: `each_held()`, the one visitor of every container's slots |
| `collector` | The cycle collector: frees objects that refer to each other in a loop, in steps of bounded work |
| `interpreter_vectors.cpp`, `vector_methods.cpp` | `Vector.<T>`: an object whose class says what its elements are; making, indexing, length, its methods, its text |
| `table`, `interpreter_properties.cpp`, `interpreter_property_reads.cpp`, `interpreter_property_calls.cpp` | Objects and Dictionaries (`vm::Table`: an object whose one slot holds its `Entries`, keys and values in the order each key was first set) and an Array's properties beyond its indices; a property by name on anything (`DynGet` and the rest; reading one, and whether one is there, in its own file), a property called by its name, the loops over properties, and Godot's Dictionaries copied in |
| `reload` | Moves a running program's objects, vectors and static fields onto a newer build, by name; what the program's static fields reach, and the container classes a newer build must keep |
| `any`, `interpreter_any.cpp`, `interpreter_any_operators.cpp` | The untyped value (`*`, `Object`): one slot, a Number as its bits and the rest tagged; ECMAScript's conversions, then its equality, `+`, `<` |
| `numeric`, `math.cpp` | Conversions C++ alone would leave undefined; `Math` where AS3 and C's library differ |
| `text_units`, `case_table.cpp` | Strings by UTF-16 unit: indexing, cutting, searching and joining text held as UTF-8, and changing case (the table is written by `tools/case_table.py`) |
| `regex.h`, `regex_tree.h`, `regex_parser.h`, `regex_parse.cpp`, `regex_parse_escapes.cpp`, `regex_classes.cpp`, `regex_compile.cpp`, `regex_match.cpp` | Regular expressions on their own: a pattern read into a tree (its structure, then its escapes and classes), the tree written as a small program, and a backtracking matcher over UTF-16 units that counts its steps and stops at a limit |
| `regexp_methods.cpp`, `regexp_strings.cpp` | The instructions of RegExp (`new`, `exec`, `test`, its properties) and of String's methods that take one (`match`, `search`, `replace`, `split`); the patterns read, kept by source and flags |
| `date_time`, `date_parse.cpp`, `date_methods.cpp` | Date: ECMAScript's arithmetic of times, AS3's texts and what `Date.parse` reads, and the instructions, with local time from the host's clock |
| `library_calls.h`, `library_calls.cpp` | A method of the library called by name on an untyped value (`u.push(1)`), or read as a function bound to its value (`u.push`): the arguments converted as `ir/library_methods.h` says, and the typed operation run on registers set past the running frame's, by one helper class (`LibraryCall`), a friend of the interpreter |
| `class_values.cpp` | Classes as values: the one object for each class, `x is c`, `new c(a)` |
| `library_members.cpp` | RegExp's and Date's properties and methods by name on an untyped value (`u.lastIndex`, `u.getTime()`), by the names `ir/library_names.h` gives |
| `json_text` | JSON text on its own: the grammar read into a builder's calls, with a stack of its own (no recursion), and a String written as Quote() writes it |
| `json_work.h`, `json_methods.cpp`, `json_stringify.cpp` | JsonCall: what is read made into Objects and Arrays and revived, and stringify, by one helper class (`Json`), a friend of the interpreter |
| `uri_text` | `escape`, `unescape` and the four URI functions |
| `number_text`, `string_methods.cpp` | Numbers as text and text as numbers (radix, `toFixed` and the rest, `parseInt`, `parseFloat`); the instructions that run `String`'s methods and those |
| `host.h` | What the interpreter is given: `Output`, `EngineHandles`, `EngineSource`, `Clock` |
| `output.h`, `engine_handles.h`, `engine_source.h`, `engine_types.h`, `clock.h` | The four interfaces and the values they pass; `SystemClock` is the system's time and zone (`standalone_host.cpp`), which a host without a clock of its own gets |
| `no_engine.h`, `standalone_host.cpp` | The answers where there is no engine |

## Rules

- **No Godot header, and nothing from `src/`.** It includes `ir/ir.h` and the standard library.
- **The engine is known only through the interfaces.** Something new asked of Godot is a new entry on
  `EngineHandles` (done to a value the interpreter holds) or `EngineSource` (where a value comes from).
  `NoEngine` must answer it, and the recorder in `tests/test_vm.cpp` must record it.
- **Each engine instruction is worked out once, when the program loads:** `EngineHandles::prepare()` is
  asked for every instruction that names something of Godot's (an `E` operand), and `run()` carries out
  what it kept. A host that keeps nothing, or can't run one with the values it is given, gets the requests
  by name (`call`, `get`, `operate` and the rest), which say what is wrong.
- **Nothing is allocated per instruction, and no text is built on the path that succeeds.**
- **The loop is measured, not guessed.** A change to `interpreter_run.cpp` comes with
  `tests/bench/run_bench.py <build> --against <other build>`. A helper in a header has cost 3% before.
- **What a container holds is said once, in `each_held()` (`held.h`).** An object's fields, a vector's
  elements, a function value's bound object or environment (its `FUNCTION_FIRST` field): freeing lets go of
  each slot it visits, and the cycle collector follows each. A new container (an Array, a keyed table for
  Object literals and Dictionary) adds its slots there and nowhere else; `may_hold_objects()` beside it
  says which classes the collector looks at. Every object is made with `collector_.list_for(class)` as its
  list, so that the collector sees it.
- **The cycle collector deletes by trial** (`collector.h`): an object whose count of references is more than
  the references from other objects is held from outside, and so is what it reaches. It needs no list of
  roots. The host calls `collect_step()` with a budget of work; the interpreter collects once more when it
  goes, and `kas3c run` reports what is still alive after that.
- **Every failure is an AS3 error** with a class, a message and a stack line. Handles and objects are
  released on the error path too: the tests count what is left alive.

## What a table holds

An Object or a Dictionary (`klass.table`), and an Array's `properties` when the pointer is not null, hold
their keys and values in `Entries` (vm/table.h). `each_held()` visits them through `each_entry()`: for each
entry whose key is not undefined, the key (a String, or a Dictionary's object key, held strongly) and the
value, both untyped. A deleted entry has both undefined and holds nothing. Before freeing an object, or the
collector emptying its garbage, `take_entries()` takes the properties out and the caller lets go of them:
engine code run meanwhile finds the table empty, never half emptied.

Tests: `test_vm`, `test_runtime`, the `programs` group, and the sanitizer build.
