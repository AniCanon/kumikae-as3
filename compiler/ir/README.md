# `ir/`: the typed IR and the lowerer

`ir.h` is the instruction set the interpreter runs and, later, what a native backend will read. The lowerer
turns the checked tree from `src/` into it.

| Files | Job |
|---|---|
| `ir.h` | Kinds of register, the table of operations (`KAS3_IR_OPS`: a name and three shape letters each), a module |
| `ir_print.cpp` | The text form the golden files hold |
| `ir_compare.cpp` | What changed between two builds that a reload alone would not show (`run_once_changes`) |
| `lower.h`, `lower.cpp` | The entry point, and units and classes in order |
| `lowerer.h` | The one class: its state and every method's declaration |
| `lower_emit`, `lower_types`, `lower_places` | Registers and constants; kinds, conversions, default values; what can be read and written |
| `lower_expressions`, `lower_operators`, `lower_calls`, `lower_statements`, `lower_classes` | By what each lowers |
| `lower_engine` | Godot's members: properties, constants, singletons, parts of a value |
| `lower_any` | The untyped value: coercion where a type is declared, `==`, `<`, `+`, `typeof`, `is`/`as` |
| `lower_vectors`, `lower_math` | `Vector.<T>` (a class per element type, places, methods, loops, handing one to Godot); `Math` |

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
