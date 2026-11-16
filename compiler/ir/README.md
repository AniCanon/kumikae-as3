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
| `lower_expressions`, `lower_operators`, `lower_calls`, `lower_statements`, `lower_classes` | By what each lowers |
| `lower_literals` | Number and string literals (a string's escapes), and the built-in library's constants: `NaN`, `Infinity`, the limits of the number types, Array's sort options |
| `lower_engine` | Godot's members: properties, constants, singletons, parts of a value |
| `lower_try` | `try`, `catch`, `finally` (handlers in `ir::Function`; the `finally` written once, every way out going through it) and labels |
| `lower_any` | The untyped value: coercion where a type is declared, `==`, `<`, `+`, `typeof`, `is`/`as` |
| `lower_dynamic` | Properties by name (`class Dynamic`, a friend of the lowerer): Object literals, `new Object()` and `flash.utils.Dictionary`, `o.a` and `o[k]` as places, `delete`, `in`, a property called by its name, `for in` and `for each` over anything that is not a vector |
| `lower_vectors`, `lower_math` | `Vector.<T>` (a class per element type, places, methods, loops, handing one to Godot); `Math` |
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
