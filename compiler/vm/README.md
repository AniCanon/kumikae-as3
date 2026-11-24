# `vm/`: the interpreter

The interpreter runs a module of IR. It's the same code under `kas3c run`, inside Godot and in the tests.
Only the host differs. This folder also holds AS3's standard library, which native code shares.

**Depends on:** `ir/ir.h` and the C++ standard library. It includes no Godot header and nothing from
`src/`.

## The host

A typed callback uses its Function object's target and bound receiver or closure environment. The interpreter checks the target signature and passes raw slots into the new frame. A mismatching target from dynamic code raises `TypeError`.

The interpreter knows where it runs only through four interfaces, gathered in `host.h`:

| Interface | What it gives |
|---|---|
| `Output` (`output.h`) | Where `trace` and uncaught errors go |
| `EngineHandles` (`engine_handles.h`) | What can be done to an engine value the interpreter holds: call it, read it, compare it, free it |
| `EngineSource` (`engine_source.h`) | Where engine values come from: constructors, singletons, constants, defaults |
| `Clock` (`clock.h`) | The time and the time zone, for `Date` and the time limit |

`godot_ext/` implements them with Godot's calls. `no_engine.h` and `standalone_host.cpp` answer where there
is no engine, and the tests use a recorder.

Each instruction that asks something of the engine is prepared once, when the program loads:
`EngineHandles::prepare()` is called for every such instruction, and `run()` carries out what it kept. A
host can decline before invoking the operation if the supplied operands do not match. Only a decline
or an absent binding sends the request by name. A failure after invocation raises an AS3 error.

A constant member name can keep four resolved receiver shapes at its instruction. Additional shapes share a table per literal name. At 64 entries, a new shape replaces one randomly chosen entry. Each access checks the receiver is alive. A reload replaces the interpreter table and invalidates the caches in native code.

At load time the interpreter finds stores that can take ownership of a temporary on its last use.
A local that receives a call result can release the last reference when you reassign it. Another named
local holding the same object keeps its own reference.

## Files

| Files | Job |
|---|---|
| `interpreter.h`, `interpreter.cpp`, `interpreter_records.h` | Frames, calls, and the public entry points |
| `interpreter_library` | The standard library's callbacks into the interpreter |
| `interpreter_run.cpp` | The loop: one case for each operation |
| `interpreter_errors.cpp`, `interpreter_time_limit.cpp` | Raising and catching errors, stack traces, and the time limit (Error #1502) |
| `interpreter_host.cpp`, `interpreter_godot_refuses.cpp` | Requests to the engine, and the checks made before one |
| `interpreter_objects.cpp`, `interpreter_conversions.cpp` | Creating objects, and conversions the loop hands on |
| `stack` | The C++ stack a program runs on, and the point where it's nearly used up (Error #1023) |
| `heap`, `runtime` | The lifetime of objects and strings |
| `fields.h` | Where an object's fields are. The one accessor every field read and write goes through |
| `held.h` | What an object holds: `each_held()`, the one visitor of every container's slots |
| `collector` | The cycle collector |
| `library` | The standard library as functions of plain values, with no interpreter frame |
| `value_text.cpp`, `function_calls.cpp`, `object_calls.cpp` | A value as text, and a function value called |
| `text_methods`, `text_calls.cpp`, `string_methods.cpp`, `number_text` | `String`'s methods, and numbers as text |
| `text_units`, `case_table.cpp` | Strings by UTF-16 unit, and case mapping |
| `vector_*.cpp`, `vector_calls.h`, `interpreter_vectors.cpp` | `Vector.<T>` and `Array` |
| `table`, `interpreter_properties*.cpp`, `interpreter_property_*.cpp` | `Object`, `Dictionary` and properties by name |
| `any`, `any_operators.cpp`, `any_calls.cpp`, `interpreter_any.cpp` | The untyped value (`*`) and its operators |
| `numeric`, `math.cpp` | Number conversions C++ leaves undefined, and `Math` |
| `regex*`, `regexp_methods.cpp`, `regexp_strings.cpp` | The regular expression engine, and `RegExp` |
| `date_*` | `Date` |
| `json_*` | `JSON` |
| `uri_text` | `escape`, `unescape` and the URI functions |
| `class_values.cpp`, `class_statics.cpp` | Classes as values |
| `named_sites`, `interpreter_named_site` | Literal member sites, their shared overflow table, and checked reuse of math results |
| `engine_named` | What an untyped value does with Godot by name: a Godot class held as a class value made new, and a member of a Godot value read, written or called by its AS3 name |
| `library_calls`, `library_members.cpp`, `interpreter_library.cpp`, `interpreter_functions.cpp` | Library methods called by name on an untyped value, and function values |
| `reload`, `reload_update.cpp`, `constructions` | Hot reload: moving live objects onto a new build, and updating them in place |
| `native_code.h` | How the interpreter hands a function to native code |

## Rules

### Speed

- **Nothing is allocated for each instruction, and no text is built on the path that succeeds.**
- **Measure a change to the loop.** A change to `interpreter_run.cpp` comes with numbers from
  `tests/bench/run_bench.py <build> --against <other build>`. Even moving a two-line helper into a header
  has made the loop measurably slower.

### The engine

- **Something new asked of Godot is a new entry on `EngineHandles` or `EngineSource`.** `NoEngine` must
  answer it, and the recorder in `tests/test_vm.cpp` must record it.

### Errors

- When you call a member by name on an attached script, its public methods, fields and accessors take
  precedence over Godot members. A getter that returns a function runs once before the call.
- **Every failure is an AS3 error** with a class, a message and a stack line. Handles and objects are
  released on the error path too. The tests count what is left alive.
- **The interpreter never recurses in C++ for an AS3 call.** It keeps its own stack of frames, so runaway
  recursion is an AS3 error, not a crash, and a debugger can walk the frames.
- **A runaway script is stopped.** Every loop passes a statement marker, where the clock is read once in
  4,096 statements.

### Memory layout

- **Where a field is, is said once, in `fields.h`.** Nothing else turns a field's number into a place in
  the object or counts an object's slots.
- **Create an object of a program's class with `Heap::object_slots()` slots,** never `slots_of()` alone.
  When hot reload remembers constructor arguments, each object has room for them after its own slots.
- Untyped values use three low address bits for pointer kinds. On 64-bit targets one more bit records
  a tag in the top byte. On wasm32 that fourth bit belongs to the address.
- **A `Vector.<byte>` stores one byte per element.** The int, uint and float vectors use four bytes
  per element. Other vectors use an eight-byte slot. Use `place()`, `set_place()`, `move_places()`
  and `clear_places()` in `runtime.h` to access any layout. Scalar bytes use the slot's unsigned
  integer member. A scalar float uses its double member to hold the exact widened binary32 value.
- **A fixed array allocates its header and element storage together.** It uses the same packed
  element widths as a vector. Its storage is never reserved again.
- **A `Vector.<Vector2>` or `Vector.<Vector2i>` stores each value in place:** eight bytes, never a handle
  and never `null`. A value crosses into a register through `read_element()` and back through
  `write_element()`. Storing `null` is TypeError #1034.

### The collector

- **What a container holds is said once, in `each_held()`.** Freeing an object releases each slot it
  visits, and the collector follows each. A new kind of container adds its slots there and nowhere else.
- **The collector needs no list of roots.** An object with more references than other objects hold on it
  is held from outside, and so is everything it reaches. The rest is a cycle and is freed.
- **The collector looks only at what can lead back to a script object** (`may_hold_objects()`). It skips a
  vector of numbers, and a vector of Godot objects of a class that's never reference counted, such as
  nodes. Twenty thousand particles in a vector aren't walked every frame.
- **Collection runs in steps.** The host calls `collect_step()` with a budget of work. The interpreter
  collects once more when it ends, and `kas3c run` reports what is still alive after that.

### Tables

An `Object` or a `Dictionary`, and the extra properties of an `Array`, keep their keys and values in
`Entries` (`table.h`), in the order each key was first set. A deleted entry holds nothing. Before an object
is freed, `take_entries()` takes its properties out, so that engine code run meanwhile finds the table
empty and never half emptied.

## Library behavior

The library follows the AS3 language reference. Where that leaves a question open, it follows the source
of avmplus, and the code names the file it read. These choices are worth knowing before you change the
library:

- **Strings** are stored as UTF-8 and counted, indexed and compared by UTF-16 unit.
- **Property order** is the order of first assignment. AS3 leaves it unspecified. A fixed order means a
  program's output never depends on a hash.
- **Array holes.** An index never written, or deleted, is a hole and not an element that holds
  `undefined`. `for in`, `for each`, `in` and `hasOwnProperty` skip holes. Reading one gives `undefined`.
  `sort` puts defined elements first, then elements that hold `undefined`, then holes. Every `Array` is
  dense: avmplus's sparse form isn't implemented.
- **Values as text.** An `Array` writes nothing for a `null` or `undefined` element. A `Vector` writes
  `null` and `undefined`. An `Array` or `Vector` that contains itself, or nests deeper than 500
  levels, raises Error #1023 when it's converted to text.
- **Numbers as text.** `toString(radix)` writes only the whole part of a number in a radix other than 10.
- **`arguments`.** A function that names `arguments` accepts more arguments than it declares, as one with
  `...rest` does.
- **Regular expressions** run on the interpreter's own backtracking engine, over UTF-16 units. It counts
  its steps and stops at a limit.
- **`Date`** asks the host's clock for the time and for the zone's offset at a moment.
- **JSON** reads and writes by ECMA-262, fifth edition. The parser keeps its own stack and doesn't recurse.
- **Operators on an untyped value that holds a Godot value** ask Godot's own operator first. Where Godot
  has none for the two operands, the answer is AS3's.

`Date`'s texts, JSON's error numbers and the URI functions' errors haven't been compared with a running
Flash Player.

## Tests

`test_vm`, `test_runtime`, `test_library`, the `programs` group, and the sanitizer build. Every program in
`tests/run/` must leave nothing alive when it ends.
