# Stage 1d: Godot's API in AS3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** every Godot class, method, property, signal and constant can be named from AS3 under the approved
naming design, and `kas3c check` knows them without any extra argument.

**Architecture:** A Python generator reads Godot's API description (`extension_api.json`) and writes one AS3
declaration file for the `godot` package. The file is committed, embedded in the library as text, and
`kas3c check` loads it as a declaration unit. The naming rules live in one module that stage 2's bindings
reuse. The compiler itself changes only where real declarations expose a gap.

**Tech stack:** Python 3 standard library (already required for the tests), C++17, CMake, CTest.

**Spec:** `docs/superpowers/specs/2026-11-12-godot-naming-design.md` (approved 2026-11-12).

## Global Constraints

- Library code builds with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
- The generator fails on anything it does not understand (an unmapped type string, a clash). It never
  falls back to `*` silently.
- Rename by rule. The hand-kept tables are the class-name clash table and the keyword list, and a test
  holds each of them to its source.
- Godot's API description is Godot's (MIT). It is not committed. The generated file is, with Godot's
  notice beside it.
- No console vendor material. Stage files by name, never `git add -A`.

## Design decisions (rulings made while planning)

Measured on Godot 4.7.2's description: 1,036 classes, 38 built-in classes, 22 global enums, 114 global
functions, 41 singletons.

1. **What is committed.** `compiler/godot/godot.as` (generated) and a small hand-written fixture. The full
   description stays out of the repository; tests that need it run when `KAS3_GODOT_API` or
   `spike/api/extension_api.json` exists and say so when they are skipped. Cost if wrong: a fresh clone
   cannot regenerate, only use, the declarations. Stage 2 needs the description for bindings and revisits.
2. **Not a built-in.** The file is a declaration unit, because overrides of built-in members are exempt from
   the signature check and `_physicsProcess` must not be. The library exposes the text
   (`godot_declarations()`); `kas3c check` adds it unless `--no-godot` is given. Library users (the unit
   tests with their fictional `godot` stand-in) are unaffected.
3. **Integer width.** AS3's `int` is 32 bits. A Godot `int` whose description says `int64` or `uint64`
   (688 places) is `Number`; `uint32` is `uint`; anything else is `int`. A constant outside 32 bits (2)
   is a `Number`. Cost if wrong: object ids above 2^53 lose bits in a `Number`; stage 2 must settle a
   64-bit type before bindings ship.
4. **Defaults.** A number, boolean, string or `null` default is written as it is (`&""` and
   `NodePath("")` become `""`). Any other default (`Vector2(0, 0)`, `[]`, `Callable()`) is written `null`:
   AS3 defaults must be constants. The real value is the binding's business, from the description.
5. **Built-in constructors.** AS3 has no overloading. The overloads merge by position: a parameter is
   optional if any overload is shorter, and typed `*` where the overloads disagree. `new Vector2()`,
   `new Vector2(1, 2)` and `new Vector2(other)` all check; `new Vector2(1)` is not rejected.
6. **Accessors.** A property's getter and setter are hidden only when they take no extra argument. The 433
   indexed properties (`anchorLeft`) are properties too, and their shared accessors
   (`setAnchor(side, value)`) stay as methods. A property with no setter is read-only. A property's type is
   its getter's return type when the getter is described, otherwise the first class its type string names.
7. **Types.** `enum::`/`bitfield::` are `int`; `typedarray::` is `GodotArray`; `typeddictionary::` is
   `GodotDictionary`; both are recorded in `[Godot(...)]` metadata on the member for the language server.
   Methods with raw pointer parameters or results (60) are left out. Built-in classes `Nil`, `bool`, `int`,
   `float`, `String`, `StringName`, `NodePath` and `Callable` are not declared: AS3's own types stand for
   them.
8. **Global enums.** The common prefix is removed on underscore boundaries. A value that would start with a
   digit keeps its full name. `Variant.Type` is the class `VariantType`.
9. **Singletons.** A singleton class's own members are static. Its inherited `GodotObject` members are not
   reachable; the design does not ask for an instance accessor.
10. **Global constants** (11 integer limits such as `INT32_MAX`) are left out: AS3 has `int.MAX_VALUE`.
11. **Operators and indexing on built-in classes** belong to stage 1c-3.

## Review Focus

1. A Godot name that becomes an AS3 keyword, or a different Godot name after camel-casing (two members of
   one class chain ending up with one name).
2. An override of a Godot virtual with the wrong signature goes unreported, or a correct one is rejected.
3. Every `kas3c check` now reads 1,000 classes: the cost on a one-file program.
4. A diagnostic inside the generated file reaching a user who cannot act on it.
5. A type string, default value or property shape in the description that the fixture does not have.

---

### Task 1: Naming rules

**Files:** Create `compiler/godot/naming.py`, `compiler/godot/test_naming.py`. Modify `compiler/CMakeLists.txt`
(add a `godot` CTest that runs `python3 -m unittest discover` in `compiler/godot`).

**Produces:** `camel(name) -> str`, `safe(name) -> str` (trailing underscore on a reserved word),
`class_name(godot_name) -> str`, `enum_values(names) -> list[str]`, `KEYWORDS`, `CLASS_RENAMES`,
`snake(name) -> str`.

- [ ] Tests first: `move_and_slide`→`moveAndSlide`; `_physics_process`→`_physicsProcess`; `get_node_3d`→
  `getNode3d`; `class`→`class_`; `Object`→`GodotObject`; `Node2D` unchanged; `Variant.Type`→`VariantType`;
  `KEY_ESCAPE, KEY_0`→`ESCAPE, KEY_0`; `OK, FAILED, ERR_BUG` unchanged; `SIDE_LEFT, SIDE_TOP`→`LEFT, TOP`;
  one-value enum keeps its name; `KEYWORDS` equals the reserved words in `compiler/src/token.cpp`.
- [ ] Run, see them fail; implement; run, see them pass; commit.

### Task 2: Generator on a fixture

**Files:** Create `compiler/godot/generate.py`, `compiler/godot/fixture_api.json`,
`compiler/godot/fixture.as` (expected output), `compiler/godot/test_generate.py`.

**Produces:** `generate(api: dict) -> str`; command line `generate.py <extension_api.json> <out.as>`.

The fixture has one of each shape: a class chain with a virtual, a property with accessors, an indexed
property, a read-only property, a signal, an enum, a constant, a static method, a vararg method, a keyword
parameter, a pointer method, each default kind, an `int64` return, a typed array; a singleton; a built-in
class with members, constants and three constructors; a global enum with a digit value; a dotted global
enum; global functions including `typeof`; a clashing class name.

- [ ] Tests first: output equals `fixture.as`; an unknown type string raises; two members that camel to one
  name raise; a class named like an AS3 built-in and not in the table raises.
- [ ] Run, fail; implement; pass.
- [ ] `kas3c check --declarations fixture.as` on a small user file that uses each shape is clean (test).
- [ ] Commit.

### Task 3: The real API

**Files:** Create `compiler/godot/godot.as`, `compiler/godot/NOTICE.md`. Modify `test_generate.py`.

- [ ] Tests first (skipped without the description): `godot.as` equals a fresh generation; the measured
  counts hold (5 renamed classes, keyword parameters, one keyword method, classes, methods); no generated
  class or global function is an AS3 built-in name; `kas3c check` of the file alone reports nothing.
- [ ] Generate; fix what the real data breaks, each with a fixture case first.
- [ ] Prove the oracle: plant a duplicate member and an unknown type in a copy and see both reported.
- [ ] Commit.

### Task 4: `kas3c` knows Godot

**Files:** Modify `compiler/CMakeLists.txt`, `compiler/src/builtins.h`, `compiler/tools/kas3c.cpp`,
`compiler/tests/test_cli.py`.

**Produces:** `const BuiltinFile &godot_declarations()`; `kas3c check [--no-godot]`.

- [ ] Tests first (CLI): a file that extends `CharacterBody2D`, overrides `_physicsProcess`, calls
  `moveAndSlide()`, reads `position`, connects `timer.timeout`, uses `Key.ESCAPE` and `print` checks clean
  with no flags; a wrong override signature is reported; `move_and_slide()` is an unknown name;
  `--no-godot` makes `godot.Node` unknown.
- [ ] Measure the load cost; rule on it in the ledger.
- [ ] Implement; pass; commit.

### Task 5: Corpus and stand-in

**Files:** Modify `compiler/tests/corpus/*.as`, `compiler/tests/golden/**` (files that use Godot),
`compiler/tests/run_golden.py`. Delete `compiler/tests/stubs/godot.as`.

- [ ] Rewrite the test programs to the new names; regenerate and read the goldens; all groups pass in the
  normal and sanitizer builds; the external corpus is unchanged.
- [ ] Commit.

### Task 6: Documents

**Files:** `README.md` (example, bullets, status table), `compiler/README.md`, `docs/roadmap.md`,
`docs/diagrams/build.py` and `roadmap.svg`, the main design's mapping line.

- [ ] Update; commit; push the branch.
