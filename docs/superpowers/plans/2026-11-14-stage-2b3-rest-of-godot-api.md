# Stage 2b-3: The Rest of Godot's API — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a script can use everything Godot offers that is not an object's own member: singletons,
constants, static methods, global functions, 64-bit integers, and errors shown the way Godot shows its own.

**Architecture:** As in stage 2b-2. The lowerer turns each construct into a host operation in the typed
IR; the interpreter passes it through `vm/host.h` without looking inside; the extension's host answers it
with Godot's own calls. A singleton is an ordinary Godot object the host looks up by name, so everything
that works on an object (methods, properties, signals) works on it unchanged.

**Tech stack:** C++17; godot-cpp 10.0.0 in `godot_ext/` only; Godot 4.7.2 headless for the tests.

**Spec:** `docs/superpowers/specs/2026-11-12-godot-naming-design.md`,
`docs/superpowers/specs/2026-11-14-int64-design.md` (part B), and the items marked **2b-3** in
`docs/backlog.md`.

## Three parts, each reviewed and merged on its own

| Part | Branch | Delivers | Backlog items it closes |
|---|---|---|---|
| **A. Singletons, statics, constants** | `feat/godot-rest` | `Input.isActionPressed("jump")`, `Engine.timeScale = 0.5`, `Input.joyConnectionChanged.add(f)`, `Vector2.ZERO`, `Key.ESCAPE`, `Vector2.fromAngle(a)` | A singleton passed as an object; a singleton's signals; a local of a Godot value type with no initializer |
| **B. `Int64`** | `feat/int64` | The type, its conversions and arithmetic, the generator's one rule; the table of widths deleted | The wide flag enum; the 32-bit wrap of whole products; `Math.floor` and its relatives counted as fractions |
| **C. Global functions and errors** | `feat/godot-functions` | `print`, `lerp`, `randf` and the rest; an AS3 error shown as Godot shows its own | The middle `null` default; a freed node and `null`; messages on a freed node; a freed handle counted as not null; a listener's argument error reported to Godot as success; the order of the null check on a Godot member |

The owner approved the `Int64` design's rule on 2026-11-14, so part B comes before part C: C's functions
with wide integers then get their final types at once.

## Global Constraints

- `compiler/vm/` and `compiler/ir/` include no Godot header. New engine work goes through `vm/host.h`.
- `kas3c check` output does not change in parts A and C. Part B changes declared types on purpose and
  says which.
- Expected results come from Godot itself: the driver computes the same thing in GDScript and prints both.
- Every new host entry fails with a message for a name Godot does not have. Never a crash.
- A value arriving from the engine into a typed register is checked against the type the register names.
- A construct outside the slice is refused by name.
- Every branch going to `main` carries an up-to-date `docs/roadmap.md`, its diagram, and `docs/backlog.md`.
- No console vendor material. Stage files by name. No attribution lines.

## Design decisions for part A (rulings made while planning)

1. **A singleton is looked up, then used as any object.** `HostSingleton out, class` gives a handle on the
   engine's singleton. A static method, property or signal of a singleton class lowers to that lookup
   followed by the ordinary call, read, write or signal verb. Nothing else is new for singletons.
2. **The generator marks singleton classes** with `[Singleton]`, so the lowerer can tell `Input.x()` (a
   singleton's method) from `Node.printOrphanNodes()` (a real static method).
3. **`Input.singleton` is the object itself,** for the places an object is wanted. The name follows Godot
   C#'s `Input.Singleton`. Cost if wrong: one generated name.
4. **A static method is one host operation,** `HostCallStatic out, class, method, arguments`. The host
   answers it with Godot's static call for a built-in type (`Vector2.from_angle`) or for a class.
5. **A constant with a value in the declarations is folded** by the lowerer (`Key.ESCAPE` is a number in
   the IR). **A constant without one** (`Vector2.ZERO`, `Color.RED`) is `HostConstant out, class, name`,
   which the host answers from Godot and checks against the declared type.
6. **A local or field of a Godot value type that has parts** (`Vector2`, `Color`: a class that declares
   variables) **with no value written is that type's zero value,** made by the type's constructor with no
   arguments, as GDScript's typed variables are. `null` can still be assigned. A Godot value without
   parts (an array, a signal) starts missing, as an AS3 object does.
7. **AS3 runs on the main thread.** A call Godot makes from another thread is refused with an error
   (found by the review: a method given to `WorkerThreadPool.addTask` crashed Godot).

## Review Focus (part A)

1. A name Godot does not have at each new entry: a singleton that is not registered (an editor-only one in
   a game), a constant or static method missing from this Godot version.
2. A singleton's signal: the listener's lifetime when the script object is freed, and `removeAll` leaving
   other scripts' listeners.
3. A write to one part of a singleton's property (`Engine.x.y = 1` shape): the singleton looked up twice,
   or the write lost.
4. A constant's type: a constant declared `Vector2` that Godot answers with something else.
5. A static field initializer that uses a singleton or a constant, run on the class's first use.

---

## Part A: singletons, statics, constants

### Task A1: Singletons

**Files:** `compiler/godot/generate.py` (`[Singleton]`, `singleton`), `godot.as`, `godot_names.txt`;
`compiler/ir/ir.h`, `ir_print.cpp`, `lower.cpp`; `compiler/vm/host.h`, `interpreter.cpp`;
`compiler/godot_ext/godot_host.h/.cpp`; tests in `tests/test_vm.cpp` (fake host),
`tests/godot_project/Statics.as`, `statics.gd`, `run_godot.py`, `godot/test_generate.py`.

**Produces:** op `HostSingleton` (`RS-`); `virtual void *Host::singleton(std::string_view p_class,
std::string *r_error)`.

- [ ] Tests first, each beside GDScript's answer: `Input.isActionPressed("ui_accept")`;
  `Engine.timeScale = 0.5` read back by GDScript; `Input.mouseMode` read; `Engine.getVersionInfo()` refused
  by name (a dictionary) or run, whichever the slice allows; `Input.joyConnectionChanged.add(onJoy)` and
  GDScript emitting it; `removeAll` on it leaves a GDScript listener; `Input.singleton.getClass()`;
  a static field `private static var scale:Number = Engine.timeScale`. With the fake host: an unknown
  singleton is an AS3 error and every handle is dropped.
- [ ] Run, fail; implement; pass in the normal and sanitizer builds; commit.

### Task A2: Constants

**Produces:** op `HostConstant` (`RSS`); `virtual bool Host::constant(std::string_view p_class,
std::string_view p_name, const Wanted &p_result, Slot *r_result, std::string *r_error)`.

- [ ] Tests first: `Key.ESCAPE`, `Input.MOUSE_MODE_CAPTURED`, `Node.NOTIFICATION_READY`, `Vector2.AXIS_X`
  (folded: the IR holds the number); `Vector2.ZERO`, `Vector2.UP`, `Color.RED`, `Vector3.FORWARD`,
  `Transform2D.IDENTITY` beside GDScript's; `Vector2.ZERO.x = 5` refused (a constant); a constant used as
  a default argument value.
- [ ] Run, fail; implement; pass; commit.

### Task A3: Static methods

**Produces:** op `HostCallStatic` (out, class, method, arguments); `virtual bool Host::call_static(
std::string_view p_class, std::string_view p_method, const Argument *p_arguments, size_t p_count, const
Wanted &p_result, Slot *r_result, std::string *r_error)`.

- [ ] Tests first: `Vector2.fromAngle(1)`, `Color.fromHsv(0.5, 1, 1)`, `Color.html("#ff0000")`,
  `Quaternion.fromEuler(v)`, a class's static method that returns an object (`Image.createEmpty`), one that
  returns nothing (`Node.printOrphanNodes()`); arguments evaluated left to right; a wrong result type is
  an AS3 error.
- [ ] Run, fail; implement; pass; commit.

### Task A4: Zero values

- [ ] Tests first: `var v:Vector2; trace(v)` gives `(0, 0)`; a field `private var tint:Color` reads as
  GDScript's `Color()`; `v = null` still stores null; a field of a class with no Godot class behind it.
- [ ] Run, fail; implement; pass; commit.

### Task A5: Documents, review, fix pass

- [ ] `docs/roadmap.md`, diagram, `docs/backlog.md`, `compiler/README.md`; commit; push.
- [ ] One independent review of the branch on the most capable model; one fix pass, failing tests first.

## Part B: `Int64`

**Spec:** `docs/superpowers/specs/2026-11-14-int64-design.md`, approved 2026-11-14. Branch `feat/int64`.

**What changes on purpose:** `kas3c check` output and hover text change wherever a Godot integer is now
`Int64` (688 marked sites that were `Number`, and the unmarked ones that were `int` or `uint`). The goldens
that show such a type are updated in the task that changes them, and the commit says which.

### Design decisions for part B (rulings made while planning)

1. **`Int64` is a fourth numeric type in the checker,** beside `int`, `uint` and `Number`: declared in
   `builtins/toplevel.as`, held as `int64_` in `names_types.cpp`, and counted by `numeric()` and `whole()`.
2. **Result types.** With both operands whole and at least one an `Int64`: `+ - * %`, the bitwise
   operators and the shifts give `Int64`; `/` gives `Number`. With a `Number` operand: `Number`.
3. **The interpreter computes `Int64` in 64-bit integers,** not through `Number` as it does for `int`: new
   ops `AddI64`, `SubI64`, `MulI64`, `ModI64`, `NegI64`, `BitAndI64`, `BitOrI64`, `BitXorI64`, `BitNotI64`,
   `ShlI64`, `ShrI64`, `UShrI64`, `LtI64`, `LeI64`, `EqI64`, and `ConstI64` (the value in the module's
   table of 64-bit constants). A comparison of an `Int64` with a `Number` is `CmpI64F64` with the operator
   as its third operand, exact for every value.
4. **Two families of conversion ops.** Checked, for a conversion the script did not write: `I64ToI32`,
   `I64ToU32`, `I64ToF64`, `F64ToI64`, each raising `RangeError` as section 4 of the design says. As
   written, for `int(x)`, `uint(x)`, `Number(x)`, `Int64(x)`: `I64WrapI32`, `I64WrapU32`, `I64RoundF64`,
   `F64ClampI64`, `StrToI64`. Widening is `I32ToI64` and `U32ToI64`. `convert()` in the lowerer takes a flag
   that says which family; only a cast written as a call sets it.
5. **`RangeError` is a class of the built-in declarations** that the interpreter can raise, as it raises
   `TypeError` today (`module_.range_error_class`).
6. **A whole-number literal where an `Int64` is expected is read from its text,** so `9223372036854775807`
   is exact; one that does not fit 64 bits is a checker error. A leading minus is part of it, so
   `-9223372036854775808` can be written.
7. **The generator's rule:** an integer is `Int64` unless its width mark is below 64 bits, or its class
   stores 32-bit integers (members marked `int32` in the description's member offsets, a class made only
   of such classes, and the two names `PackedInt32Array` and `PackedByteArray`). `WIDTHS` and
   `_INT64_ELEMENT` are deleted. Wide flag enums become `Int64`.
8. **At the edge:** `Kind::I64` goes to Godot as its integer and comes back from any Godot integer.
   `arrives` keeps the range check for `I32` and `U32`.
9. **Text:** `I64ToStr`; `toString(radix)` waits for the standard library (2c) and is refused by name.

### Review Focus (part B)

1. A conversion that loses a value silently: every pair of numeric kinds, in assignment, argument, return,
   compound assignment, `++`, a default parameter, a field initialiser, and at Godot's edge both ways.
2. The ends: `Int64.MIN_VALUE`, `MAX_VALUE`, `-MIN_VALUE`, `MIN_VALUE % -1`, `MIN_VALUE / -1`, shifts by 64
   or more and by a negative count, 2^53 and 2^53 + 1 to `Number`, `NaN` and the infinities to `Int64`.
3. Comparison of an `Int64` with a `Number` near 2^53 and 2^63, and with `NaN`.
4. A generated declaration that became narrower than before, or an integer parameter Godot cuts off
   inside (measured in the design: `Vector2i`, `PackedInt32Array`).
5. Existing programs: every corpus and test program still checks, and nothing that ran now refuses.

### Task B1: the type in the checker

**Files:** `compiler/builtins/toplevel.as`, `compiler/src/names_types.cpp`; tests in
`compiler/tests/test_types.cpp`, `test_type_errors.cpp`, `test_godot_math.cpp`.

- [ ] Tests first: `var a:Int64 = 5;` checks; `var i:int = a;`, `var n:Number = a;`, `var u:uint = a;`
  check (the range is checked when it runs); `var s:String = a;` and `var a:Int64 = "5";` are errors, as
  for `int`; the type of `a + 1`, `a * a`, `a % 2`, `a & 1`, `a << 2`, `a >>> 1`, `-a`, `a++` is `Int64`;
  of `a / 2` and `a + 1.5` is `Number`; `a < 1.5` is `Boolean`; `var big:Int64 = 9223372036854775807;`
  checks and `= 9223372036854775808` is an error; `cell * a` is a `Vector2i`; `Int64.MAX_VALUE` is an
  `Int64`; `Int64("12")` and `int(a)` check.
- [ ] Run, fail; implement; pass; commit.

### Task B2: the kind, its arithmetic and its conversions

**Files:** `compiler/ir/ir.h`, `ir_print.cpp`, `lower.cpp`; `compiler/vm/interpreter.cpp`, `runtime.h/.cpp`;
tests in `compiler/tests/run/` (programs with expected output), `test_vm.cpp`, `test_ir.cpp`.

**Produces:** `Kind::I64`; the ops of decisions 3 and 4; `ir::Module::range_error_class`;
`Module::wide_constants`.

- [ ] Tests first, as programs run by `kas3c run` with their expected trace: arithmetic at the ends
  (Review Focus 2); each checked conversion in range and out of range (the out-of-range one traces the
  caught... there is no `try` yet, so each is its own program whose expected output is the `RangeError`
  and its stack); each written conversion; text; comparison with every numeric kind; `Int64` fields,
  statics, parameters, defaults and returns; `++` and compound assignment.
- [ ] Run, fail; implement; pass in the normal and sanitizer builds; commit.

### Task B3: the generator's rule

**Files:** `compiler/godot/generate.py`, `test_generate.py`, `fixture_api.json`, `fixture.as`, `godot.as`.

- [ ] Tests first: a method marked `int64` and one marked `uint64` are `Int64`; an unmarked integer of a
  built-in type and of a global function is `Int64`; every integer of `Vector2i`, `Rect2i`,
  `PackedInt32Array` and `PackedByteArray` is `int`; a wide flag enum is `Int64`; `WIDTHS` is gone; and,
  against the real description, no integer is declared narrower than the commit before declared it.
- [ ] Run, fail; implement; regenerate; update the goldens that show a changed type; commit.

### Task B4: at Godot's edge

**Files:** `compiler/godot_ext/godot_host.cpp`, `instance.cpp`, `callable.cpp`; tests in
`compiler/tests/godot_project/Wide.as`, `wide.gd`, `run_godot.py`.

- [ ] Tests first, each beside GDScript's answer: `RefCounted`'s `getInstanceId()` round-trips through
  `instanceFromId` (it does not today); `Color.toRgba64()`; `randi()` assigned to `uint`; `items.size()`
  assigned to `int`; a `PackedInt64Array` element past 2^53; `PackedInt32Array.append` of a wide `Int64`
  is a `RangeError` before Godot sees it; GDScript calling a method with an `Int64` parameter with
  `9223372036854775807`, and one with an `int` parameter with `5000000000` (refused, as today); a listener
  with an `Int64` parameter; an `Int64` field read by GDScript; `cell * (a * b)` with a product past 2^31.
- [ ] Run, fail; implement; pass; commit.

### Task B5: documents, review, fix pass

- [ ] As A5. The backlog's three items of this part are removed, each by the test that closes it:
  the wide flag enum (B3), the 32-bit wrap of whole products (B4), and `Math.floor` and its relatives
  counted as fractions (B1: their results are whole numbers held in a `Number`; the fix is in `whole()`).

## Part C: global functions and errors

Planned in detail after part A's review, which will show what the static call path can carry. Outline:

- C1. Global functions as `HostCallStatic` with no class: `print`, `lerp`, `randf`, `clampi`.
- C2. An uncaught AS3 error shown as Godot shows its own, with the script's file and line.
- C3. The six backlog items of this part, each with a failing test first.
- C4. Documents, review, fix pass.
