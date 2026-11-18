# `backend_cpp/`: the IR written as C++

The C++ backend takes a module of IR and writes it as C++ source. The platform's own C++ compiler turns
that source into native code. The generated program must behave exactly as the interpreter does:
`tests/run_native.py` runs every program in `tests/run/` both ways and compares the output.

`kas3c cpp <file>... -o <folder>` is the command. See
[the compiler's README](../README.md#write-a-program-as-c).

**Depends on:** `ir/` only. It includes no header from `vm/`, `runtime/` or Godot, because it writes text.
What generated code needs from [`runtime/`](../runtime/README.md), it names in that text.

## What it writes

| File | Holds |
|---|---|
| `program.h` | Each class as a struct, and a declaration of every function |
| `<Class>.cpp` | The functions of one class |
| `program.cpp` | The functions of no class, and the table that the one exported function, `kas3_program`, returns |

With `--unit` (`Options::one_unit`), every class's code goes into `program.cpp`. A release build uses that
form, because the C++ compiler then sees every function at once and can inline small ones. One file for
each class suits development, where only what an edit changed is rebuilt.

With `--remember` (`Options::remembers`), the constructor of a class whose constructors can be run again
tells the host what its object is built with, so that hot reload can update objects created as native
code. See [the runtime](../runtime/README.md#what-objects-are-constructed-with). Without the option the
output is the same to the byte.

## Files

| Files | Job |
|---|---|
| `generate` | The entry point. Returns the files, or the first thing that can't be translated |
| `support` | What is refused, decided before anything is written |
| `facts` | What is known about every function before any is written: which can't fail, which fields each reads or writes, and which functions a virtual call can reach |
| `names` | Identifiers, C++ types and literals |
| `function_writer` | One function: registers as locals, ownership, labels, error paths and handlers |
| `patterns.h`, `function_simple.cpp` | Operations that are one C++ statement, as a table |
| `function_objects.cpp`, `function_classes.cpp`, `function_fields.cpp` | Fields, static fields and classes |
| `function_calls.cpp` | Direct calls, virtual calls and interface calls |
| `function_fast.cpp`, `function_library.cpp` | Library operations with an inline fast path, and direct calls into the library |
| `function_loops.cpp`, `function_whole.cpp` | Loops over vectors, and 32-bit integer arithmetic |
| `function_rounds.cpp` | Where a loop goes round: the look at the host's alarm that stops a loop without end, and loops counted by a whole number |
| `function_borrowed.cpp` | Reads that don't need a reference count |
| `function_engine.cpp`, `function_values.cpp`, `function_signals.cpp` | Calls into Godot, Godot's value types, and `emit()` |
| `thunks` | Operations left to the interpreter's library |
| `program_writer` | `program.h` and the program's table |

## The shape of generated code

- **Every function takes `Context *c` first.** Generated code has no thread-local and no global that
  changes.
- **A register is a C++ local of its kind's type:** `int32_t`, `uint32_t`, `double`, `bool`, `int64_t`,
  `Str *`, `Object *`, or `Slot` for an untyped value.
- **A call between the program's functions is a C++ call.** A virtual call goes through the class's method
  table, and an interface call through its interface table.
- **A class is a C++ struct.** A field of an object whose class is known is written through the struct, so
  the C++ compiler knows that a write to `vx` leaves `mass` unchanged.
- **Reference counts change only in `retain(c, x)` and `release(c, x)`.** A register that holds a string,
  an object or an untyped value owns a reference. A parameter that's never written borrows its caller's.
- **An error is `c->failing`,** tested after each operation that can raise one. The function then goes to
  its handlers, or leaves and adds its line to the stack trace. Nothing about lines is kept while a
  program runs without errors.
- **A loop can be stopped.** Where a loop goes round, the function looks at the host's alarm once in
  `ROUNDS` rounds, so that a call that runs too long ends with Error #1502 as under the interpreter. A
  function with a loop can therefore fail. See [the runtime](../runtime/README.md#the-time-limit).
- **A null check is explicit,** and left out on `this`.
- **`#line` markers point at the `.as` source.**

## Optimizations

Each of these rests on a fact the backend proves about the whole program. Each has a test program written
to give a wrong answer if the proof were wrong.

| Optimization | What it does | Test |
|---|---|---|
| Direct virtual calls | When the receiver's class is known, the call compares the object's class with the classes the program creates, four at most, and calls each one's function by name. A method with one implementation is a direct call. | `tests/run/virtual_calls.as` |
| No error check after a call that can't fail | A function in which nothing can fail keeps no call depth and sets no error. | `tests/run/deep_leaf.as` |
| Fields in locals | A number or Boolean field of `this` that a loop uses stays in a local. Memory is updated only around code that could see the field another way. | `tests/run/field_in_loop.as` |
| Static fields in locals | A static number or Boolean that a function reads and never writes is read once. A constant of the function's own class is read once whatever the function runs. | `tests/run/static_still.as`, `tests/run/static_const_still.as` |
| Counted loops | A loop counted by a whole number against a bound that stays the same (`for (i = 0; i < n; i++)`) is held to stretches of `ROUNDS` steps, worked out where a stretch begins. Each round costs one comparison, as before, and the alarm is looked at where a stretch ends. | `tests/run/loops_counted.as`, `tests/run/stopped/` |
| Indexes held to a stretch | In a loop's copy with locals, a stretch ends no further on than the vectors the counter indexes. An element read or stored under the counter then needs no test of its own. | `tests/run/loops_counted.as`, `tests/run/loops_vector_end.as` |
| Fields through the class's struct | Tells the C++ compiler which field of which class a read or write touches. | `tests/run/field_classes.as` |
| Loops over vectors | A loop that reads and writes vectors of numbers is written twice. The first copy keeps each vector's length and elements in locals. An index out of range jumps to the second copy, which is the loop as written. | `tests/run/vector_loops.as` |
| Narrow vector elements | An element of a `Vector.<int>` is read and written as four bytes. | `tests/run/vector_narrow.as` |
| Borrowed elements | A variable given an element of a vector counts no reference when nothing in between could release the object. | `tests/run/element_lent.as` |
| Handing over the last use | A register copied into a field or a vector as its last use hands its reference over. | `tests/run/last_use.as` |
| Objects that end alone | Releasing an object that holds only numbers, Booleans and strings runs no other code. | `tests/run/last_use.as` |
| Recycled objects | `new` of such a class takes a block the host kept when one was released. | [runtime](../runtime/README.md) |
| 32-bit integer sums | A sum of `int`s stored back into an `int` is computed in 32 bits, which is exact. | |
| Whole sums held untyped | A sum of `int`s and `uint`s stored where no type is declared (an element of an `Array`, a `*`) is the `int` it is, by integer arithmetic. | `tests/run/whole_untyped.as` |
| Machine math | `Math.sqrt`, `abs`, `floor` and `ceil` are the processor's own instructions. | |

### Calls into Godot

| Optimization | What it does | Test |
|---|---|---|
| Values held inline | A Godot value with parts, such as `Vector2`, that can't be `null` is a plain struct in a local, laid out as Godot lays it out. | `tests/godot_native/Probe.as` |
| Inline math | For `Vector2` and `Vector3`, the arithmetic operators, `normalized()`, `length()`, `dot()` and `cross()` are the runtime's own code, identical to Godot's to the last bit. | `Probe.as` |
| Method binds on bare values | A property or method of a Godot object is called through Godot's method bind, with no boxed values and no lookup by name. | `Probe.as` |
| Fields read through their handle | A field that holds a value or a signal, read only to be used at once, is used through the field's own handle. | `Probe.as`, `fields()` |
| Handles kept | A register given the same object again keeps the handle it has. | `Probe.as`, `fieldObjects()` |
| Results made in place | A `Dictionary`, `Array`, `Callable` or `RID` that Godot returns is written straight into a new handle. | `Probe.as`, `inHandles()` |
| Direct `emit()` | `emit()` hands the host the values as they're held. Nothing is boxed and no name is looked up. | `tests/godot_project/Emits.as` |

Everything else that asks Godot runs through a thunk: strings, arrays, callables, adding and removing
listeners, and untyped values.

## Rules

- **Identical to the interpreter, or refused.** A program the backend can't translate exactly is refused by
  name in `support.cpp` (`not yet supported: <what>`). It's never translated to something close.
- **A fast path keeps the library's path for the rest.** Where an operation is written inline, anything
  unusual (a `null`, an index out of range, a vector that must grow) runs the same instruction through
  its thunk. The error and its message then come from one place.
- **A Godot value is inline only where it can't be `null`.** A parameter, a field and a call's result are
  handles.
- **Inline math must match Godot's to the last bit.** Every operation done inline on a Godot value has a
  line in `tests/godot_native/Probe.as`, which Godot runs both ways.
- **An optimization comes with a number** from `tests/bench/run_native_bench.py`, measured in alternating
  runs.
- **Generated code builds without a warning** under `-Wall -Wextra`. The tests build it with `-Werror`.

## Where a new thing goes

An operation that is one C++ statement goes in `patterns.h`, with the kinds its registers must hold. Any
other goes in the `function_*.cpp` that owns that kind of code. An operation with no case runs through a
thunk, which is always correct and always slower.

## Tests

The `native` group (`tests/run_native.py`), in the normal build and the sanitizer build, and
`test_backend`. `tests/native_skipped.txt` lists the programs the backend refuses and why.
