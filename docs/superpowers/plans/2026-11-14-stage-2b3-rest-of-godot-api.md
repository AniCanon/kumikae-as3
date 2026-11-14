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

Planned in detail when the owner has answered the design's open question. Outline:

- B1. The type in the checker: conversions, operators, literals, the whole-number rule.
- B2. The `I64` register kind in the IR and the interpreter; the checks of section 4 of the design.
- B3. The generator's rule; the table deleted; a test that no integer is declared narrower than before.
- B4. At Godot's edge: arguments, results, listeners, virtual methods, fields read by GDScript.
- B5. Documents, review, fix pass.

## Part C: global functions and errors

Planned in detail after part A's review, which will show what the static call path can carry. Outline:

- C1. Global functions as `HostCallStatic` with no class: `print`, `lerp`, `randf`, `clampi`.
- C2. An uncaught AS3 error shown as Godot shows its own, with the script's file and line.
- C3. The six backlog items of this part, each with a failing test first.
- C4. Documents, review, fix pass.
