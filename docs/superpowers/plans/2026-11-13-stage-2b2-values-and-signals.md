# Stage 2b-2: Godot Values and Signals — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** what stage 1 promised about Godot's values and signals runs in headless Godot 4.7.2: operators,
`position.x = 5`, text, `toString`, defaults, and listening to signals the AS3 way.

**Architecture:** The lowerer turns each construct into a host operation in the typed IR; the interpreter
passes it through `vm/host.h` without looking inside; the extension's host composes it from Godot's own
`Variant` calls. A listener is a bound method: a custom Godot callable that holds the script object and the
function.

**Tech stack:** C++17; godot-cpp 10.0.0 in `godot_ext/` only; Godot 4.7.2 headless for the tests.

**Spec:** `docs/superpowers/specs/2026-11-12-godot-naming-design.md` ("Decided by the owner after
approval": the signal table), `2026-11-11-performance-redesign.md` (section 8: the native callable and its
tombstone), and the promises table in `plans/2026-11-12-stage-2a-interpreter-core.md`.

## Two parts, each reviewed and merged on its own

| Part | Branch | Delivers |
|---|---|---|
| **A. Values** | `feat/godot-values` | Operators and comparison on Godot values, text from a Godot value, `position.x = 5`, typed indexing, trailing `null` defaults, `toString()` both ways |
| **B. Signals** | `feat/godot-signals` | Bound methods as listeners, the signal verbs, `emit`, `object[name]` for a signal |

Signals carry most of the lifetime risk, so they get a review of their own.

## What moves to stage 2c, and why

These promises need AS3's untyped value (`*`) or AS3's own collections, which the interpreter does not have
until 2c. The roadmap says so.

- **AS3 `Array`, `Vector` and `Object` converting when handed to the engine.** The types themselves are 2c.
- **`object[name]` in general.** In this slice it works where a signal's member follows it, which is the
  owner's spelling: `timer[name].add(f)`. `var x:* = node[name]` waits for `*`.
- **Elements of Godot's untyped collections** (`GodotArray`, `GodotDictionary`): their element type is `*`.

## Global Constraints

- `compiler/vm/` and `compiler/ir/` include no Godot header. New engine work goes through `vm/host.h`.
- `kas3c check` output does not change.
- Expected results for operators, text and signals come from Godot itself: the driver computes the same
  thing in GDScript and prints both. Nothing is written from memory.
- Every new host entry handles a null handle and a freed object by failing with a message, which the
  interpreter raises as an AS3 error. Never a crash.
- A value arriving from the engine into a typed register is checked against the type the register names.
- A construct outside this slice is refused by name.
- Every branch going to `main` carries an up-to-date `docs/roadmap.md`, its diagram, and `docs/backlog.md`.
- No console vendor material. Stage files by name. No attribution lines.

## Design decisions (rulings made while planning)

1. **Operators are one host operation.** `HostOperate out, operator, arguments` with the operator as
   written in AS3 (`"+"`, `"=="`, `"-"` with one argument for unary minus). The host maps it to Godot's
   `Variant` evaluation. Operand kinds are what the checker gave them: an `int` goes as Godot's integer, a
   `Number` as its float.
2. **`==` on two Godot objects is identity; on two Godot values it is Godot's equality.** Both go through
   `HostOperate`.
3. **Text is Godot's own.** `trace(v)`, `"" + v`, `String(v)` and `v.toString()` on a Godot value or object
   give Godot's text for it (`HostToStr`). A null gives `"null"`.
4. **`position.x = 5` is read, change, write back.** A place inside a value remembers the place the value
   came from. The outer object is evaluated once. It nests (`transform.origin.x = 5`) and works with `+=`
   and `++`. A part of a value that is stored nowhere (`getPosition().x = 5`) is refused: it would change a
   copy nobody sees.
5. **Indexing a Godot value** (`v[0]`, a packed array's element) is `HostIndexGet` and `HostIndexSet`, for
   element types the interpreter has. A write follows rule 4.
6. **Defaults: Godot fills what is left out.** The binding passes only the arguments written. A trailing
   literal `null` written for a parameter declared `= null` is left out too, which is the same thing in
   AS3's terms. A `null` anywhere else for a value-type parameter is an error when the line runs. Cost: the
   real default for a `null` in the middle would need Godot's default values in the name table; on the
   backlog.
7. **A script's `toString()` answers Godot's `_to_string`,** so `str(node)` and `print(node)` in GDScript
   show it.
8. **A listener is a bound method** written as `onHit` or `this.onHit` or `other.onHit`, where a Godot
   parameter of type `Function` is expected. Anything else used as a function value stays refused (2c).
   A private method binds to that exact function; any other binds to the object's own override.
9. **The bound method is a custom Godot callable** holding the script object (retained), the function, and
   the owner's instance id.
   - Equal when the object and function are equal, so `has`, `remove` and "a second `add` does nothing" work.
   - Godot drops its connections when the owner is freed. A callable kept elsewhere checks the owner is
     still alive before every call and fails cleanly if not: this is the tombstone.
   - Arguments from `emit` go through the same checks as a call from GDScript.
   - A class with no Godot owner can listen too; the connection keeps the object alive until the signal's
     object is freed.
10. **The signal verbs are composed in the host.** The name table gives the AS3-only verbs reserved names
    (`Signal.add=@add`), written by the generator; the host answers names that start with `@`.
    `add` checks `is_connected` first; `removeAll` and `numListeners` use `get_connections`.
11. **`object[name]` takes the AS3 name.** The host finds Godot's name through the name table, walking up
    from the object's class, and checks the result is a signal. A wrong name is an AS3 error on that line.

## Review Focus

1. A value of the wrong type reaching a typed register: a `Vector3` where a `Vector2` is named, text where
   a number is, from an operator, an index, a property or `object[name]`.
2. A null handle or a freed object at each new host entry: operate, text, part write, index, bind, each
   signal verb.
3. `position.x = 5`: the outer object evaluated twice, a nested write that loses the middle value, a
   temporary handle leaked when the write fails.
4. Signals under re-entry: a listener that removes itself during `emit`; the owner freed during `emit`; a
   deferred listener whose owner is freed before the frame ends; an error raised inside a listener.
5. A bound method's identity: `has` false after `add`, a second `add` connecting twice, a private method
   resolving to a subclass's method of the same name.

---

## Part A: values

### Task A1: Operators and comparison

**Files:** `compiler/ir/ir.h`, `ir_print.cpp`, `lower.cpp`; `compiler/vm/host.h`, `interpreter.cpp`;
`compiler/godot_ext/godot_host.h/.cpp`; tests in `compiler/tests/test_vm.cpp` (fake host),
`tests/godot_project/Values.as`, `values.gd`, `run_godot.py`.

**Produces:** op `HostOperate` (shape `RSA`); `virtual bool Host::operate(std::string_view p_operator, const
Argument *p_arguments, size_t p_count, ir::Kind p_result, std::string_view p_type, Slot *r_result,
std::string *r_error)`; `bool arrives(const Variant &, ir::Kind, std::string_view p_type)` in
`godot_host.cpp`: the one check every value from the engine passes (moved from `instance.cpp`'s
`accepts`).

- [ ] Tests first. In Godot, each line computed in AS3 and in GDScript, printed side by side:
  `Vector2 + Vector2`, `- `, `* Number`, `* int`, `Number * Vector2`, `/ Number`, unary `-`, `Vector2i *
  int`, `Vector2i * Number`, `Color * Color`, `v == w`, `v != w`, two node references `==` and `!=`,
  `Vector2i / 0` (Godot's error becomes an AS3 error). With the fake host: the operands are evaluated left
  to right and every handle is dropped.
- [ ] Run, fail; implement; pass in the normal and sanitizer builds; commit.

### Task A2: Text

**Files:** as A1.

**Produces:** op `HostToStr` (`RR-`); `virtual Str *Host::text(void *p_value)`.

- [ ] Tests first: `trace(v)`, `"at " + v`, `String(v)`, `v.toString()`, `node.toString()`, a null
  `Vector2` and a null node give `null`; each compared with GDScript's `str()`.
- [ ] Run, fail; implement; pass; commit.

### Task A3: A part of a value

**Files:** `compiler/ir/lower.cpp` (`Place` gains the place it came from), tests as A1.

- [ ] Tests first: `position.x = 5`; `position.x += 5`; `position.x++`; `transform.origin.x = 5` on a
  `Node2D`; a field `private var v:Vector2` with `v.x = 1`; a local; `other.position.y = 2` where `other`
  is evaluated once (a method that counts its calls); `getPosition().x = 5` refused by name; the write
  failing on a freed object leaks nothing.
- [ ] Run, fail; implement; pass; commit.

### Task A4: Indexing a Godot value

**Produces:** ops `HostIndexGet` (`RRR`: out, value, key) and `HostIndexSet` (`RRR`: value, key, new
value); `Host::index_get`, `Host::index_set`.

- [ ] Tests first: `v[0]`, `v[1] = 3` on a local and on `position`; a `PackedInt32Array` element read and
  written; an index out of range is an AS3 error; `GodotArray` indexing refused by name.
- [ ] Run, fail; implement; pass; commit.

### Task A5: Defaults and `toString`

**Files:** `lower.cpp` (`engine_arguments`), `godot_ext/instance.cpp` (`to_string`), tests as A1.

- [ ] Tests first: a Godot method with a value-type default called with the argument left out, and with a
  trailing `null`, gives what GDScript gives with it left out; a `null` in a value-type parameter that is
  not trailing is an AS3 error; `str(node)` in GDScript shows a script's `toString()`; a script without
  one shows Godot's own text.
- [ ] Run, fail; implement; pass; commit.

### Task A6: Documents, review, fix pass

- [ ] `docs/roadmap.md`, diagram, `docs/backlog.md`, `compiler/README.md`; commit; push.
- [ ] One independent review of the branch on the most capable model; one fix pass, failing tests first.

## Part B: signals

### Task B1: Bound methods

**Files:** `ir/ir.h`, `lower.cpp`, `vm/host.h`, `vm/interpreter.h/.cpp`, `godot_ext/callable.h/.cpp` (new),
`godot_ext/instance.cpp` (the argument checks become a shared `call_script`), tests in `test_vm.cpp` and
`tests/godot_project/Signals.as`, `signals.gd`.

**Produces:** op `HostBind` (`RRI`: out, object, a function index or a method-table slot with the top bit
set); `virtual void *Host::bind(Object *p_object, uint32_t p_function, void *p_owner, std::string
*r_error)`; `bool Interpreter::call_function(...)` used by the callable.

- [ ] Tests first: a bound method passed to `callDeferred`-style API (`Callable`-taking method) is called
  with its arguments; wrong argument types are refused as from GDScript; two bindings of one method are
  equal in Godot; a private method of a base class binds to the base's function; the callable outlives its
  owner and then fails cleanly; a function value of any other kind is refused by name.
- [ ] Run, fail; implement; pass; commit.

### Task B2: The signal verbs

**Files:** `compiler/godot/generate.py` (reserved names), `godot_names.txt`, `godot_ext/godot_host.cpp`.

- [ ] Tests first, each against a GDScript count of calls: `add`; a second `add` does nothing; `add(f,
  true)` runs at the end of the frame; `addOnce`; `remove`; `removeAll`; `has`; `numListeners`; `emit` with
  arguments; a listener that removes itself during `emit`; the owner freed during `emit`; a deferred
  listener whose owner is freed first; an error inside a listener is reported and the next listener runs;
  a plain AS3 object as the listener; a signal of a freed object.
- [ ] Run, fail; implement; pass; commit.

### Task B3: `object[name]`

**Produces:** `Host::member(void *p_receiver, std::string_view p_as3_name, std::string_view p_type, Slot
*r_result, std::string *r_error)`.

- [ ] Tests first: `timer[name].add(f)` with `"timeout"`; an inherited signal by its AS3 name
  (`"treeEntered"`); a name that is not a signal and a name that does not exist are AS3 errors;
  `node[name]` used any other way is refused by name.
- [ ] Run, fail; implement; pass; commit.

### Task B4: Documents, review, fix pass

- [ ] As A6.
