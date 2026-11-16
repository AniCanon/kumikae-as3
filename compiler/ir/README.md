# `ir/`: the typed IR and the lowerer

`ir.h` is the instruction set the interpreter runs and, later, what a native backend will read. The lowerer
turns the checked tree from `src/` into it.

| Files | Job |
|---|---|
| `ir.h` | The IR as a whole, for every includer: the three headers below, and printing and comparing a module |
| `ir_ops.h` | Kinds of register, the table of operations (`KAS3_IR_OPS`: a name and three shape letters each), an instruction |
| `ir_codes.h` | What the codes in some operations' operands mean: which of Math's, a vector's, String's, RegExp's, Date's or JSON's functions; the fields of the library's objects |
| `ir_module.h` | A module: classes, fields, functions, handlers, globals |
| `ir_print.cpp` | The text form the golden files hold |
| `ir_compare.cpp` | What changed between two builds that a reload alone would not show (`run_once_changes`) |
| `lower.h`, `lower.cpp` | The entry point, and units and classes in order |
| `lowerer.h` | The one class: its state and every method's declaration |
| `lower_flow.h` | What is kept of the loops and `try` statements around the code being lowered (`Loop`, `TryBlock`, `TryExit`) |
| `lower_emit`, `lower_types`, `lower_places` | Registers and constants; kinds, conversions, default values; what can be read and written |
| `lower_expressions`, `lower_calls` | By what each lowers |
| `lower_operators.h`, `lower_operators` | Operators on two values (the `Operators` class, a friend of the lowerer): arithmetic, bitwise, string, logical, comparisons, equality, `is` and `as` |
| `lower_statements.h`, `lower_statements` | Statements (the `Statements` class, a friend of the lowerer): locals, `if`, the loops, `switch`, `break`, `continue`, `return`, `throw` |
| `lower_classes` | Each class's place in the module, before any body is lowered: what it inherits, its fields, its methods and their table slots, Godot's virtuals it answers |
| `lower_bodies` | The body of each function: its start and end, its parameters, a method, a constructor (with the fields' first values), a class's static initializer, package variables |
| `lower_literals` | Number and string literals (a string's escapes), and the built-in library's constants: `NaN`, `Infinity`, the limits of the number types, Array's sort options |
| `lower_engine.h`, `lower_engine` | Godot's members (the `Engine` class, a friend of the lowerer): properties, constants, singletons, parts of a value, the zero of a value with parts, 32-bit integers, operators Godot carries out |
| `lower_engine_calls.h`, `lower_engine_calls` | Calls of Godot's methods and global functions (the `EngineCalls` class, a friend of the lowerer): the arguments the engine is given, with Godot's own defaults; a method bound where Godot takes a function; a signal reached by its name (`timer[name].add(f)`) |
| `lower_try` | For `Statements`: `try`, `catch`, `finally` (handlers in `ir::Function`; the `finally` written once, every way out going through it) and labels |
| `lower_any` | The untyped value: coercion where a type is declared, `typeof`; for `Operators`, `==`, `<`, `+`, `is`/`as` |
| `lower_dynamic` | Properties by name (`class Dynamic`, a friend of the lowerer): Object literals, `new Object()` and `flash.utils.Dictionary`, `o.a` and `o[k]` as places, `delete`, `in`, a property called by its name, `for in` and `for each` over anything that is not a vector |
| `lower_vectors.h`, `lower_vectors`, `lower_math` | `Vector.<T>` (a class per element type; then, in the `Vectors` class, a friend of the lowerer: places, methods, loops, handing one to Godot); `Math` |
| `lower_members` | Getters and setters (a property is a place; each half a function with a table slot of its own, `get x`), and interfaces (a class with no objects; each class's table from an interface method's name to its function, `CallInterface`). A helper class, `Members`, that the lowerer befriends |
| `lower_regexp`, `lower_date` | RegExp and Date for the `Library` class: their classes (made on first use), a regular expression literal, `new`, their properties as places, their methods and String's that take a pattern |
| `library_names.h` | RegExp's and Date's members by name and their codes in RegExpCall and DateCall: read by the lowerer, and by the interpreter for a member reached by name on an untyped value |
| `lower_json` | JSON for the `Library` class: `JSON.parse`, `JSON.stringify` and a Dictionary's `toJSON()`, one `JsonCall` each, with the classes it makes and raises put into the module |
| `lower_library.h`, `lower_library` | The standard library (the `Library` class, a friend of the lowerer): `String`'s methods, the numbers' `toString(radix)`, `toFixed`, `toExponential` and `toPrecision`, `parseInt`, `parseFloat`, `isNaN`, `isFinite`, and `toString()` of any object |

## Rules

- **Includes `src/` only.** No Godot header, nothing from `vm/`. Godot's own names come from the name table
  through `godot_name(...)`.
- **A new operation** is one entry in `KAS3_IR_OPS`, with its shape letters, plus its case in
  `vm/interpreter_run.cpp`. `tests/test_ir.cpp` checks the letters; nothing else lists operations.
- **What is not supported yet is refused by name** with `refuse(position, "...")`, never lowered to
  something that runs wrongly. Each refusal has a line in the list in `tests/test_vm.cpp`.
- **Evaluation order is AS3's:** left to right, receiver before arguments, a null receiver reported after
  the arguments are worked out.
- **A clean-up leaves the golden IR byte for byte** (`tests/run/*.ir`).

Tests: `test_ir`, `test_vm`, and the `programs` group (`tests/run/`).
