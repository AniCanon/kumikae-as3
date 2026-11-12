# Stage 2b-1: AS3 Inside Godot, First Slice — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** an AS3 script attached to a `CharacterBody2D` moves the body when headless Godot 4.7.2 runs the
scene.

**Architecture:** A GDExtension library registers AS3 as a Godot scripting language. Loading a `.as` file
compiles the project's AS3 to IR; attaching it to a node makes an interpreter object bound to that node.
The engine calls script methods by name; scripts call the engine through the host interface, which the
extension implements with Godot's own calls. The interpreter still includes no Godot header.

**Tech stack:** C++17; godot-cpp 10.0.0 (Godot 4.7 API) for the extension's glue only; Godot 4.7.2 headless
for the test.

**Spec:** `docs/superpowers/specs/2026-11-11-kumikae-as3-design.md` ("How AS3 appears to Godot", "Godot API
bindings", "Packaging"), amended by `2026-11-11-performance-redesign.md` (sections 2 and 8).

## Stage 2b in slices

| Slice | Delivers | Done when |
|---|---|---|
| **2b-1** (this plan) | The extension, script instances, engine calls in and out for objects, numbers, strings and `Vector2` | The milestone script below moves a body in headless Godot. |
| **2b-2** | What stage 1 promised: signals (`add`, `addOnce`, `remove`, `has`, `emit`), `toString`, real defaults, `object[name]`, collections crossing the boundary, the other value types and their operators | Each promise in the stage 2a plan's table has a test in Godot. |
| **2b-3** | Singletons and utility functions in full, errors reported in Godot's own way, the 64-bit integer decision | The corpus programs that need no unsupported language feature run in Godot. |

## The milestone script

```actionscript
package {
	import godot.*;

	public class Player extends CharacterBody2D {
		private var speed:Number = 200;
		public var steps:int = 0;

		override public function _physicsProcess(delta:Number):void {
			velocity = new Vector2(speed, 0);
			moveAndSlide();
			steps++;
		}
	}
}
```

The test scene has one `CharacterBody2D` with this script. A GDScript test driver runs 30 physics frames and
prints the body's `position`; `x` must be 100 (30 frames at 60 per second at 200 units per second), within a
tolerance, because Godot's vectors are single precision. Headless Godot has no input, so the script does not
read any.

## Global Constraints

- `compiler/vm/` and `compiler/ir/` include no Godot header. Everything the engine supplies goes through
  `vm/host.h`.
- godot-cpp is not committed. The extension target is optional (`-DKAS3_GODOT_CPP=<path>`); without it the
  20 existing test groups build and pass, and the Godot test is skipped with a message.
- The IR stays typed: it says "a `Vector2`" and "a `Node`". How the interpreter stores them is its own
  business.
- Godot names come from a table the generator writes, never from reversing a lowerCamel name.
- A script that uses anything beyond this slice is refused with an error naming the construct.
- No console vendor material. Stage files by name.

## Design decisions (rulings made while planning)

1. **godot-cpp for the glue, the host interface for the calls.** The script language, script and instance
   classes are godot-cpp classes: that is the part godot-cpp generates well, and the spike has a working
   example. Engine calls made by scripts go through `Host`, by name, with Godot's `Variant` call. Cost: the
   glue needs RTTI, so the extension library is built with it; `vm/` and `ir/` are not.
2. **In the interpreter, a Godot value is boxed.** A register of an engine kind holds a handle the host
   gives out (a `Variant` underneath), copied and freed through the host. This is simple and in GDScript's
   range, which is all the interpreter must be. The native backend will use the same typed IR with direct
   calls and inline values. Cost: every engine call in the interpreter boxes its arguments.
3. **Two new register kinds:** `Val` (a Godot value type such as `Vector2`: copied on assignment) and `Ref`
   (a Godot object such as a node: a reference the engine owns). The IR records which Godot type each is.
4. **A script instance is an AS3 object whose base is a Godot class.** Its class in the IR has
   `native_base` (the Godot class name), and the object holds the owner node's handle in a hidden first
   field. `this.position` on such an object is a property of the owner.
5. **One program per project.** All `.as` files under the project are compiled together when the first
   script loads, as the language server does. Reloading on change is 2d.
6. **The interpreter becomes long-lived and re-entrant** before anything else: the engine calls in, the
   script calls the engine, and the engine may call back in. Registers are re-read after every host call;
   an error is reported and cleared at each entry from the engine, and one already propagating is saved and
   restored; nested entries are capped.
7. **The headless test** follows the spike's procedure: a project folder with a `.gdextension` file pointing
   at the built library, run with `godot --headless --path <project>`.
8. **The debug library.** godot-cpp here is built as `template_release`. The first slice builds the
   extension against that; whether the editor needs a `template_debug` build is checked when the editor is
   first used (2d).

## Review Focus

1. The engine calls a script method while another call into the same interpreter is in progress, or after
   an error: stale registers, a pending error that poisons the next call, a halt that never clears.
2. A handle from the host that is leaked, freed twice, or used after the owner node is freed.
3. A Godot name that is wrong: an overridden virtual never called, or a property set under the wrong name.
4. A number crossing the boundary: `Number` to Godot's `float` and back, `int` to a 64-bit `int`, a
   `Vector2`'s single-precision components.
5. A script that uses something outside this slice and is not refused.

---

### Task 1: A long-lived, re-entrant interpreter

**Files:** Modify `compiler/vm/interpreter.h/.cpp`, `compiler/ir/lower.h/.cpp` (a mode with no entry class).
Test in `compiler/tests/test_vm.cpp`.

**Produces:** `bool Interpreter::start()` (runs the static initializers once);
`Object *Interpreter::construct(uint32_t p_class)` (allocates and runs the constructor; null on error);
`bool Interpreter::call_method(Object *p_object, std::string_view p_name, const Slot *p_arguments, size_t
p_count, Slot *r_result)`; `void release_object(Object *)`; `ir::lower(compilation, &module,
LowerMode::Library)`.

- [ ] Tests first, with a fake host: two calls in a row after an error in the first; a host callback that
  calls back into the interpreter while a call is in progress, deep enough to grow the stack; an error
  thrown inside the nested call is reported and cleared there and the outer call carries on; a halt does not
  end later calls; nesting past the cap is an AS3 error; nothing leaks.
- [ ] Run, fail; implement; whole suite in the normal and sanitizer builds; commit.

### Task 2: The Godot name table

**Files:** Modify `compiler/godot/generate.py` (writes `compiler/godot/godot_names.txt`), `embed.py`,
`compiler/src/builtins.h`. Test in `compiler/godot/test_generate.py`.

**Produces:** `const char *godot_name(std::string_view p_class, std::string_view p_member)` and
`godot_class_name(std::string_view)`: the Godot spelling of a declared class or member, or null.

- [ ] Tests first: `CharacterBody2D.moveAndSlide` is `move_and_slide`; `Node._physicsProcess` is
  `_physics_process`; `Node3D.getNode3d`-style names round-trip; `GodotObject` is `Object`; every member the
  generator declares has an entry.
- [ ] Run, fail; implement; pass; commit.

### Task 3: Engine kinds in the IR and the host interface

**Files:** Modify `compiler/ir/ir.h`, `ir_print.cpp`, `lower.cpp`, `compiler/vm/host.h`,
`interpreter.cpp`. Test with a fake host in `test_vm.cpp` and golden IR in `tests/run/`.

**Produces:** kinds `Val` and `Ref`; ops `HostNew` (a value type from arguments), `HostCall` (a method on
an engine object or value), `HostGet`, `HostSet`, `HostField` (`velocity.x`); `Class::native_base`; in
`Host`: `call`, `get`, `set`, `construct`, `copy`, `drop`, conversions to and from `Number`, `int`,
`Boolean` and `String`.

- [ ] Tests first: the milestone script lowers to the expected IR; with a fake host that records calls,
  running `_physicsProcess` produces `construct Vector2(200, 0)`, `set velocity`, `call move_and_slide`, in
  that order, and every handle is dropped; a script using a signal, `Input`, or an operator on a value type
  is refused by name.
- [ ] Run, fail; implement; pass; commit.

### Task 4: The extension

**Files:** Create `compiler/godot_ext/` (`register.cpp`, `language.h/.cpp`, `script.h/.cpp`,
`instance.cpp`, `godot_host.h/.cpp`), `compiler/tests/godot_project/` (project, scene, driver script,
`.gdextension`), `compiler/tests/run_godot.py`. Modify `compiler/CMakeLists.txt`.

- [ ] Test first: `run_godot.py` runs headless Godot on the test project and expects `x` near 100 and
  `steps == 30`. Skipped, with a message, without godot-cpp or Godot 4.7.
- [ ] Run, fail; implement, following the spike's host code; pass; commit.

### Task 5: Documents

**Files:** `README.md`, `compiler/README.md`, `docs/roadmap.md`, `docs/diagrams/build.py`, `roadmap.svg`.

- [ ] Update, the roadmap included; commit; push the branch.
