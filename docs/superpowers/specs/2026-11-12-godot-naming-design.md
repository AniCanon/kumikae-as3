# Kumikae AS3: how Godot's API is named in AS3

Design, 2026-11-12. Status: **proposed. The owner set the direction; the rules below are not yet approved.**

This document replaces one mapping rule of the main design: "method and property names keep Godot's
spelling (`move_and_slide`)". Everything else in the main design and the performance redesign stands.

Lines marked **(owner)** are what the owner asked for. Lines marked **(call)** are the designer's decisions,
open to being overruled. Lines marked **(measured)** come from running the rule over Godot 4.7's API
description: 1,036 classes, 16,822 methods, 4,162 properties, 503 signals.

## Intent

AS3 must be a first-class Godot language, with naming of the quality that the Swift and C# bindings have
**(owner)**. A Godot API written in AS3 should look as if it had been designed for AS3: an AS3 programmer
should guess most names without looking them up.

## What the sources say

**AS3's own conventions** (Adobe's Flex coding conventions, the Flash Player API, and Starling, Away3D,
Flixel, FlashPunk, Nape and as3-signals):

- Packages are lowercase. Classes are UpperCamel with whole-case acronyms (`URLLoader`, not `UrlLoader`).
- Methods are lowerCamel verbs. Cheap state is a property, not `getX()`/`setX()`.
- Booleans are bare (`visible`, `mouseEnabled`); `is…` appears on read-only state and on methods.
- Constants are `static const` in UPPER_SNAKE. An "enum" is a final class of such constants
  (`BlendMode.ADD`, `Keyboard.ESCAPE`).
- A game object is a subclass that overrides lifecycle methods (`update()`, `added()`).
- Events are `addEventListener` with `Event` subclasses, or signal objects (`clicked.add(handler)`).

**Godot's C# binding** renames everything to PascalCase, recases acronyms through a hand-kept exception
table, renames `Object` to `GodotObject`, and puts `Array` and `Dictionary` in a namespace. Its documented
pain points: two naming worlds (string-based calls still need `"add_child"`), suffixes forced by clashes
(`ProcessModeEnum`), and heavy renaming between major versions.

**SwiftGodot** camel-cases members algorithmically, keeps Godot's class spelling, keeps the underscore on
virtual methods, strips enum prefixes, and exposes signals as typed properties (`timer.timeout.connect`).
It names a script's members back to the engine in snake_case. Its pain points: it has already renamed its
collection types once (`GArray` to `VariantArray`).

Lessons taken: rename by rule, not by table; solve name clashes once and narrowly; keep one way to use
signals; make the script's members look native from the engine's side too.

## The rules

### 1. Classes

- All Godot classes are in one package, `godot` **(call)**.
- A class keeps Godot's spelling: `Node2D`, `CharacterBody2D`, `HTTPRequest`, `CPUParticles2D`, `AABB`
  **(call)**. Godot already writes acronyms whole, which is AS3's rule, so no recasing table is needed.
- **A Godot name that is also an AS3 built-in gets the prefix `Godot` (call).** That is five names
  **(measured)**:

  | Godot | AS3 | Clashes with |
  |---|---|---|
  | `Object` | `GodotObject` | `Object` |
  | `Array` | `GodotArray` | `Array` |
  | `Dictionary` | `GodotDictionary` | `flash.utils.Dictionary` |
  | `JSON` | `GodotJSON` | `JSON` |
  | `Error` (the enum) | `GodotError` | `Error` |

  Without this, `import godot.*` would make a bare `Object` ambiguous in every file.
- Godot's `bool`, `int`, `float` and `String` are AS3's `Boolean`, `int`, `Number` and `String`. `Variant`
  is `*`. A `Callable` parameter is a `Function`: a method or closure is passed directly.
- `StringName` and `NodePath` are written as `String` **(call)**. The compiler builds the engine value once
  for a literal. They appear in 842 parameters and 166 return types **(measured)**, so a separate type
  would be the most frequent friction in the API.

### 2. Methods and properties

- Methods are lowerCamel: `move_and_slide()` becomes `moveAndSlide()`, `add_child()` becomes `addChild()`.
- **A Godot property is an AS3 property**, named from the Godot property: `position`, `visible`, `name`.
  Its `get_`/`set_`/`is_` accessor methods are not exposed. So `is_visible()` is simply `visible`.
- A method with no backing property keeps its name: `getParent()`, `isOnFloor()`. 1,160 such `get…`
  methods take no arguments **(measured)**. Promoting them to properties by hand is not done: the API
  description does not say which are cheap, and both other bindings leave them as methods.
- **Virtual methods keep their underscore: `_ready()`, `_process(delta)`, `_physicsProcess(delta)` (call).**
  They are overridden with `override public function`. Dropping the underscore would clash with signals
  (`Node` has both `_ready` and the signal `ready`). With it, the rule produces **no clash** between any
  method, property, signal and constant of any class, inherited members included **(measured)**.
- Parameters are lowerCamel. A parameter or method named like an AS3 keyword gets a trailing underscore:
  `class_`, `default_`. That is 49 parameters and one method, `GDScript.new` **(measured)**.

### 3. Constants and enums

AS3 has no enum type, so an enum value is an `int`.

- A class's constants and enum values keep Godot's names, which are already UPPER_SNAKE:
  `Node.PROCESS_MODE_INHERIT`, `Vector2.ZERO`, `Color.RED` **(call)**. Nothing is stripped, so Godot's
  documentation applies letter for letter and nothing can clash.
- Each of Godot's 22 global enums is a final class with the common prefix removed, as in Flash's
  `Keyboard.ESCAPE`: `Key.ESCAPE`, `MouseButton.LEFT`, `GodotError.OK` **(call)**. Where removing the prefix
  would leave a name starting with a digit, the full name stays: `Key.KEY_0`.
- A parameter that takes an enum is typed `int`. The declaration records which enum, so that the language
  server can offer the right constants.

### 4. Global functions and singletons

- Godot's 114 global functions are package-level functions of `godot`, lowerCamel: `print("hi")`,
  `lerp(a, b, t)`, `degToRad(x)` **(call)**. AS3 has package-level functions (`trace`, `getTimer`), and
  this reads like Godot's own documentation.
- Two are left out because AS3 already has them: `typeof` (an operator) and `isFinite` **(measured)**.
- A singleton is a class with static members: `Input.isActionPressed("jump")`, `Engine.physicsTicksPerSecond`.

### 5. Signals

- **A signal is a property holding a `Signal`, named in lowerCamel (call):**

  ```actionscript
  button.pressed.connect(onPressed);
  timer.timeout.disconnect(onTimeout);
  healthChanged.emit(health);
  ```

  The verbs are Godot's (`connect`, `disconnect`, `emit`). This is the as3-signals shape, which AS3
  programmers know, and it is what SwiftGodot chose.
- A script declares one with the metadata the main design already has:
  `[Signal] public var healthChanged:Signal;`
- There is no `addEventListener` form. One way to do it.

### 6. Value types

- Godot's math types stay value types with lowercase fields (`x`, `y`) and lowerCamel methods
  (`distanceTo`, `normalized`). Operators work on them, as the main design says.
- **`position.x = 5` works (call).** The compiler reads the property, changes the field and writes it back.
  C# rejects this and Flash made you do it by hand; a compiler that knows the type can do it.

### 7. What the engine sees of a script

- **A script's members are shown to the engine in snake_case (call).** `maxSpeed` marked `[Export]` appears
  as `max_speed`, labelled "Max Speed" in the inspector; `takeDamage()` is callable from GDScript as
  `take_damage()`; `_physicsProcess` answers the engine's `_physics_process`. The class name is unchanged.
- **String-based calls use Godot's names:** `call("add_child", node)`, `connect("body_entered", f)`. The
  engine resolves these, not the compiler. This is the "two naming worlds" problem that C# has. The typed
  forms above avoid it, and the language server can warn on a lowerCamel string that matches nothing.

## What this does not decide

- **Typed arrays.** `Array[Node]` is a `GodotArray` for now; the declaration records the element type.
  Whether it should read as `Vector.<Node>` depends on the conversion cost, which the performance redesign
  left unjudged.
- **`getNode` without generics.** `getNode("Player") as Player` works today. A shorter form (metadata on a
  field, as SwiftGodot's `@Node`) is a later decision.
- **64-bit integers.** The main design's rule stands: no silent truncation; the builder chooses how.
- **Waiting on a signal** inside a function. Still deferred, as in the main design.

## Cost

- Nothing has shipped, so nothing breaks for users.
- The test corpus and the Godot stand-in use 15 distinct snake_case method names across 10 files
  **(measured)**. They are rewritten when the declaration generator lands.
- The names come from a generator, so a rule change later is a regeneration, not hand edits. The hand-kept
  parts are the five-name clash table and the keyword list.

## What to build

A **declaration generator**: Godot's API description in, one AS3 declaration file for the `godot` package
out, following the rules above. It replaces the hand-written stand-in, and stage 1c part 3 (math
operators), the interpreter and the language server's completion all read its output. It ships with a test
that fails if any generated name clashes with another or with an AS3 built-in, so that a new Godot version
cannot introduce a clash unnoticed.

## Examples

```actionscript
package {
    import godot.*;

    public class Player extends CharacterBody2D {
        [Export] public var maxSpeed:Number = 300;
        [Signal] public var died:Signal;

        override public function _ready():void {
            (getNode("Hitbox") as Area2D).bodyEntered.connect(onBodyEntered);
        }

        override public function _physicsProcess(delta:Number):void {
            var direction:Number = Input.getAxis("left", "right");
            velocity.x = direction * maxSpeed;
            if (isOnFloor() && Input.isActionJustPressed("jump")) {
                velocity.y = -400;
            }
            moveAndSlide();
        }

        private function onBodyEntered(body:Node2D):void {
            if (body.isInGroup("enemies")) {
                died.emit();
                queueFree();
            }
        }
    }
}
```

| GDScript | AS3 under these rules |
|---|---|
| `move_and_slide()` | `moveAndSlide()` |
| `is_on_floor()` | `isOnFloor()` |
| `position.x = 5` | `position.x = 5` |
| `visible = false` | `visible = false` |
| `func _physics_process(delta)` | `override public function _physicsProcess(delta:Number):void` |
| `timer.timeout.connect(f)` | `timer.timeout.connect(f)` |
| `Input.is_action_pressed("jump")` | `Input.isActionPressed("jump")` |
| `KEY_ESCAPE` | `Key.ESCAPE` |
| `Node.PROCESS_MODE_PAUSABLE` | `Node.PROCESS_MODE_PAUSABLE` |
| `Vector2.ZERO` | `Vector2.ZERO` |
| `lerp(a, b, 0.5)` | `lerp(a, b, 0.5)` |
| `var d: Dictionary` | `var d:GodotDictionary` |
