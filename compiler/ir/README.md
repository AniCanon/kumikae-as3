# `ir/`: the typed IR and the lowerer

The typed intermediate representation (IR) is the single definition of what a program means. The
interpreter runs it, and the C++ backend writes it as C++. The lowerer produces it from the checked syntax
tree that `src/` gives.

**Depends on:** `src/` only. It includes no Godot header and nothing from `vm/`.

Fixed-length arrays have distinct class names, including the element type (`[]int`, for example). They use the vector allocation, index and length instructions. Packed modules and generated programs retain that class identity.

A float register has kind `F32`. Binary32 arithmetic and conversions have their own instructions.
The kind and instructions follow the existing wire values, so earlier modules retain their encoding.
Loading a module that uses float requires a runtime that recognizes the appended kind and instructions.

A byte register has kind `U8`. The VM slot uses its unsigned integer member. Conversion to a byte
keeps the low eight bits of an unsigned integer, and arithmetic widens it before operating.
The byte kind and its integer conversions also use appended wire values.

A proven local uses the same registers, bound members and instructions as a local declared with its type. A local whose script or use is unknown keeps dynamic dispatch.

## The IR

A typed loop variable uses the same checked conversion as an assignment when a collection supplies an untyped element.

`CallTypedFunction` takes a Function object and an argument list of typed registers. Its destination has the declared result kind or is absent for a void callback. Packed modules preserve the operation and the register kinds.

- **Register-based and typed.** An instruction says whether it adds two `Number`s or compares two strings,
  so values carry no tags.
- **Plain data.** A module holds no pointers into the syntax tree. It can be compared, printed and
  fingerprinted without the compiler.
- **AS3's arithmetic, exactly.** `int + int` is computed as a `Number` and wraps to 32 bits when it's
  stored in an `int`, which is what the checker's types say.
- **Ready for a debugger.** Every instruction has a source position, every function lists its named
  locals, and each statement begins with a marker where a hook can stop the program.
- **Godot's methods as values.** Where Godot takes a function, a method of a Godot object is
  `HostBindMethod`: Godot's own Callable of the object and the method. Anywhere else it's a `Function`
  whose function is a forwarder the lowerer writes: it takes what Godot's method declares and calls it.
  A method read from an object is bound to an Array of the object, the AS3 name and Godot's name.
  The function table identifies these forwarders by index when you compare function values. Binary
  format version 1 permits an empty local table. The reader accepts older version-1 modules and
  recognizes an unflagged forwarder by its synthetic receiver or, with locals stripped, by its
  receiver load and matching engine call. It rejects an unsupported format version.
- **Godot by name table.** An instruction that asks something of Godot names it by an index into
  `Module::engine_names`, never by text. Each entry is a copy of one line of the generated name table, so a
  back end finds Godot's method, property or operator once.

## Files

| Files | Job |
|---|---|
| `ir.h` | The IR as a whole, for every includer |
| `ir_ops.h` | Kinds of register, and the table of operations (`KAS3_IR_OPS`): a name and three shape letters each |
| `ir_codes.h` | What the codes in some operations' operands mean, such as which `Math` or `String` function |
| `ir_module.h` | A module: classes, fields, functions, handlers, globals |
| `ir_print.cpp` | The text form that `kas3c ir` prints and the golden files hold |
| `ir_binary` | Binary codec for project-built `.kas3` modules. On load it checks record sizes and table references before passing the module to the interpreter |
| `ir_compare.cpp` | What changed between two builds that a reload alone wouldn't show |
| `ir_chain.cpp` | Whether a class's constructor chain depends only on its arguments and on constants, so that a reload can run it again for a live object |
| `ir_reach.h` | What an instruction calls, and the code reached from a function |
| `library_methods.h`, `library_names.h`, `library_entries.h` | The built-in library's methods by name: what each takes and gives, and the operation that carries it out. Read by the lowerer, the interpreter and the C++ backend |
| `adjacent_uses.h` | Single-use temporaries and control-flow entries shared by the interpreter and native backend |
| `lower.h`, `lower.cpp` | The entry point |
| `lowerer.h` | The `Lowerer` class: its state, and the helpers every part of the lowering uses |
| `lower_env.h`, `lower_flow.h` | Places a value is stored in, captured variables, and the loops and `try` statements around the code being lowered |
| `lower_emit`, `lower_types`, `lower_places`, `lower_literals` | Registers and constants, kinds and conversions, places, and literals |
| `lower_expressions`, `lower_operators`, `lower_comparisons`, `lower_calls` | Expressions, operators and calls |
| `lower_statements`, `lower_try` | Statements, and `try`/`catch`/`finally` with labels |
| `lower_classes`, `lower_bodies`, `lower_members` | A class's place in the module, function bodies, and getters, setters and interfaces |
| `lower_functions`, `lower_closures` | Function expressions, what they capture, and function values |
| `lower_any`, `lower_dynamic` | The untyped value, and properties by name |
| `lower_vectors`, `lower_vector_methods`, `lower_math` | `Vector.<T>` and `Math` |
| `lower_library`, `lower_regexp`, `lower_date`, `lower_json` | The rest of the standard library |
| `lower_engine`, `lower_engine_calls` | Godot's members: properties, constants, singletons, operators, methods and global functions |
| `lower_engine_values` | Godot's methods and functions as values |
| `lower_library_values` | The library's own functions as values (`Math.max`, `trace`): a function written from the library's declaration, whose body is the call lowered as it is anywhere else |

The lowerer is one class split across files. `Lowerer` keeps the state. Each group of what it lowers is a
helper class that it befriends, such as `Statements`, `Operators` or `Calls`. A helper holds the lowerer
by reference, is created where it's needed, and keeps nothing from one use to the next.

## Rules

- **Evaluation order is AS3's:** left to right, the receiver before the arguments. A `null` receiver is
  reported after the arguments are evaluated.
- **What isn't supported is refused by name,** with `refuse(position, "...")`. It's never lowered to
  something that runs wrongly. Each refusal has a line in the list in `tests/test_vm.cpp`.
- **A clean-up leaves the golden IR unchanged,** byte for byte (`tests/run/*.ir`).
- **Static fields are set on a class's first use,** a base class before its subclasses, as in Flash.

## Where a new thing goes

A new operation is one entry in `KAS3_IR_OPS`, with its shape letters, and its case in
`vm/interpreter_run.cpp`. `tests/test_ir.cpp` checks the letters. Nothing else lists the operations.

Its lowering goes in the `lower_*.cpp` that owns that kind of code.

## Tests

`test_ir`, `test_ir_binary`, `test_constructors`, `test_vm`, and the `programs` group, which runs every program in
`tests/run/`.
