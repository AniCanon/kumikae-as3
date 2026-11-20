# Using AS3 in Godot

This page shows how to write Godot scripts in ActionScript 3: how to set up a project, how Godot's API
reads in AS3, and how properties, signals and Godot's values behave. For the language itself, read
[The AS3 language](language.md).

## Set up a project

A Godot extension makes AS3 a scripting language in stock Godot 4.7. A project needs two things:

- **`kas3.gdextension`** in the project's root. It tells Godot where the extension library is.
- **`bin/`** with the extension library, `libkas3_godot`.

To get them:

1. Build the extension. See [the compiler's README](../compiler/README.md#build-and-test). The build puts
   the library in `compiler/tests/godot_project/bin`.
2. Copy `compiler/tests/godot_project/kas3.gdextension` and that `bin` folder into your project.
3. Open the project in Godot once, so that Godot registers the extension.

The [example game](../examples/coin-dash/README.md) is a project set up this way. In Rider, the
[plugin's](../editors/jetbrains/README.md) project wizard creates one for you.

Every `.as` file in the project is part of one program, except files in folders whose name starts with a
dot and files in the project's [test folders](#project-settings-and-environment-variables). If any script
has a compile error, Godot prints the errors and no script loads.

## Write a script

A script is a class that extends a Godot class. Attach the `.as` file to a node of that class, as you
would a GDScript file.

```actionscript
package game {
    import godot.*;

    public class Door extends Area2D {
        [Export] public var openTime:Number = 0.5;
        [Export] public var tint:Color = new Color(1, 0.8, 0.2);
        [Signal] public var opened:Signal;

        private var timer:Timer;

        override public function _ready():void {
            timer = getNode("Timer") as Timer;
            timer.timeout.add(onTimeout);
            bodyEntered.addOnce(onBodyEntered);
        }

        private function onBodyEntered(body:Node2D):void {
            timer.start(openTime);
        }

        private function onTimeout():void {
            modulate = tint;
            opened.emit();
        }
    }
}
```

- **Godot calls the virtual methods you override,** such as `_ready` and `_process`.
- **Only public members are visible to Godot.** GDScript can call a public method by its AS3 name, read and
  write a public field, and use a public getter or setter as a property.
- **A script class can extend another script class.**
- **`as` and a cast work on Godot's classes and on your script classes.** `node as Coin` gives the `Coin`
  that the node carries, or `null`. `Coin(node)` gives it too, and raises TypeError #1034 if the node has no
  such script.
- **A class that extends no Godot class is a plain AS3 object.** It has no node behind it, and you can't
  hand it to Godot.
- **A script on a reference-counted Godot class,** such as `RefCounted` or `Resource`, lives while AS3 or
  Godot holds it. You can create one with `new`.
- **`toString()` is what `print(node)` shows in GDScript,** if your class defines it.
- **Scripts run on the main thread.** A call from another thread is refused with a message.

## How Godot's API is named

Godot's whole API is available in the `godot` package, under AS3 naming conventions.

| In Godot | In AS3 | Rule |
|---|---|---|
| `move_and_slide()` | `moveAndSlide()` | Methods are lowerCamel |
| `get_aabb()`, `get_world_2d()` | `getAABB()`, `getWorld2D()` | Acronyms are capitals, as in Flash's `errorID` |
| `is_visible()`, `set_visible(v)` | `visible` | A getter and a setter are one property |
| `_physics_process(delta)` | `_physicsProcess(delta:Number)` | Virtual methods keep their underscore |
| The signal `timeout` | `timer.timeout` | A signal is a property |
| `KEY_ESCAPE` | `Key.ESCAPE` | A global enum is a class of constants |
| `Node.PROCESS_MODE_PAUSABLE` | `Node.PROCESS_MODE_PAUSABLE` | Class constants are unchanged |
| `Input.is_action_pressed("jump")` | `Input.isActionPressed("jump")` | A singleton is used through its class name |
| `lerp()`, `deg_to_rad()` | `lerp()`, `degToRad()` | Global functions are functions of the package |
| `Object`, `Array`, `Dictionary`, `JSON`, `Error` | `GodotObject`, `GodotArray`, `GodotDictionary`, `GodotJSON`, `GodotError` | A class whose name AS3 already uses gets a prefix |
| `String`, `StringName`, `NodePath` | `String` | AS3's own string |
| `Callable` | `Function` | A method or a function expression |

Other things to know:

- **Only virtual methods can be overridden.** Overriding `addChild` is a compile error, because the engine
  would never call your version.
- **A class the engine won't create can't be used with `new`.** `new Input()` is an error.
- **Value types have one constructor that takes each of Godot's forms:** `new Vector2()`,
  `new Vector2(1, 2)`, `new Vector2(other)`.
- **Static methods and constants are on the class:** `Vector2.fromAngle(a)`, `Color.html("#ff8000")`,
  `Vector2.ZERO`, `Color.RED`.
- **An argument you leave out takes Godot's default.**
- **A global function that can return any type,** such as `lerp` or `max`, is used where the code says
  what it expects: `var x:Number = lerp(a, b, 0.5);`.
- **`Input.singleton` is the singleton object itself,** for where you need to pass it.
- **A method is a function wherever Godot takes a callable:** `children.filter(isEnemy)`.
- **Enums are `int`.**
- **Methods that take raw pointers aren't available.** There are 60 of them.

## Properties and values

Godot's properties are AS3 properties. You can change one part of a value, and the compiler writes the
whole value back:

```actionscript
package game {
    import godot.*;

    public class Mover extends Node2D {
        private var velocity:Vector2 = new Vector2(120, 0);

        override public function _process(delta:Number):void {
            position += velocity * delta;
            position.y = 40;
            if (position.x > 800) {
                velocity = -velocity;
            }
        }
    }
}
```

### Godot's values and operators

AS3 has no operators on objects. Godot's value types have them, and the compiler knows each one from
Godot's API description.

- **The operators are `+`, `-`, `*`, `/`, `%`, unary `-`, and the comparisons.** `==` and `!=` work on
  every type. Two Godot objects are equal when they're the same object.
- **The result has the type Godot gives it.** `transform * point` is a `Vector2`, and
  `transform * transform` is a `Transform2D`.
- **An operator the type doesn't have is a compile error:** `can't use '+' on 'Vector2' and 'Node'`.
- **Whole numbers and fractions differ, as in Godot.** `cell * 2` is a `Vector2i` and `cell * 0.5` is a
  `Vector2`, so `cell *= 0.5` is an error. `Math.floor(x)` is a `Number` in AS3: write
  `cell * int(Math.floor(x))`.
- **Indexing works where Godot has it.** `vector[0]` is a `Number`, and an element of a `GodotArray` or
  `GodotDictionary` is `*`.
- **A variable of a value type starts at zero,** not `null`, as in GDScript: `var start:Vector2;` is
  `(0, 0)`. You can still assign `null` to it.
- **`trace(v)` and `"at " + v` give Godot's text for the value.**
- **`is`, `as` and a cast work with Godot's value types.** `value is Color` asks what the value is now.
  `value as Vector2` gives the value or `null`. `Vector2(value)` gives the value, `null` for `null`, and
  TypeError #1034 for anything else. A cast doesn't convert: `Vector2(cell)` with a `Vector2i` is an error,
  and `new Vector2(cell)` converts.

### Vectors of values

A `Vector.<Vector2>` or `Vector.<Vector2i>` stores the values themselves, eight bytes each, as a C# array
of structs does.

- `new Vector.<Vector2>(3)` holds three zero vectors.
- An element is never `null`. Writing `null` into one is a compile error.
- What you read is a copy, and what you store is copied in.
- `points[0].x += 1` changes one part of an element where it is. The compiler reads the element, changes
  it and writes it back, for a vector of any of Godot's value types.
- Handed to Godot where it takes a `PackedVector2Array`, the vector is copied in one step.

A `Vector.<int>` or `Vector.<uint>` stores four bytes for each element and goes to Godot as a
`PackedInt32Array` in one step. A vector of a larger value type, such as `Vector.<Vector3>`, holds a
reference to each element, which can be `null`.

### Collections

AS3 collections convert when you hand them to Godot:

| Godot expects | You can pass |
|---|---|
| `GodotArray` | An `Array`, or any `Vector` |
| `PackedInt32Array` and the other packed number arrays | An `Array`, or a `Vector` of a number type |
| `PackedStringArray` | An `Array`, or a `Vector.<String>` |
| `PackedVector2Array`, `PackedVector3Array`, `PackedVector4Array`, `PackedColorArray` | An `Array`, or a `Vector` of that type |
| `GodotDictionary` | An `Object`, or a `flash.utils.Dictionary` |

Godot receives a copy. On the way back, a Godot collection stays a `GodotArray` or `GodotDictionary`, and
you can loop over it as it is:

```actionscript
package game {
    import godot.*;

    public class Roster extends Node {
        public function names():Array {
            var result:Array = [];
            for each (var child:Node in getChildren()) {
                result.push(child.name);
            }
            return result;
        }
    }
}
```

- **`for each` gives the elements of a `GodotArray` or a packed array, and the values of a
  `GodotDictionary`.** `for in` gives the indices, or the keys.
- **An element is checked as it's stored in the loop's variable.** One that doesn't fit is TypeError #1034.
  A node that carries your script fits a variable of the script's class.
- **A loop over `null` runs no times,** as in AS3.
- **A loop over a `GodotArray` reaches an element added on the way.** A loop over a `GodotDictionary` visits
  the keys it had when the loop began, in Godot's order, and skips a key erased on the way.
- **`key in table` and `delete table[key]` work on a `GodotDictionary`.** `delete` gives `true` whether or
  not the key was there, as it does on an `Object`.

To get the AS3 kind, ask for it: `getChildren() as Array`. A Godot dictionary converts to a
`flash.utils.Dictionary` the same way. Its keys become what a `Dictionary` keeps, so the Godot keys `1` and
`"1"` become one key.

### Integers

Godot's integers have 64 bits, and AS3's `int` has 32. Godot's integer is the AS3 type `Int64`.

- A Godot method returns `Int64` unless Godot says the value is narrower. `children.size()` is an `Int64`.
- An `Int64` converts to `int`, `uint` or `Number` where one is expected. If the value doesn't fit, the
  conversion raises a `RangeError`. It's never truncated silently.
- `int(x)` truncates, as in Flash.
- Where Godot accepts any type, as `print` and `setMeta` do, a `Number` goes as a float even when its
  value is whole, and an `int`, `uint` or `Int64` goes as an integer.

## Signals

A signal is a property of type `Signal`. You add and remove listeners on it.

| Call | What it does |
|---|---|
| `signal.add(listener)` | Calls `listener` each time the signal is emitted. Adding the same listener twice does nothing |
| `signal.add(listener, true)` | The same, but the call is deferred to the end of the frame |
| `signal.addOnce(listener)` | Calls `listener` the next time only |
| `signal.remove(listener)` | Removes the listener |
| `signal.removeAll()` | Removes the listeners that scripts added |
| `signal.has(listener)` | Whether the listener is there |
| `signal.numListeners` | How many listeners scripts added |
| `signal.emit(...)` | Emits the signal |

- **A listener is a method or a function expression.** A method's listener is removed when its object is
  freed.
- **A listener's parameters are checked when the signal fires.** An argument that doesn't fit is an AS3
  error that says so.
- **On a singleton's signal, `removeAll()` and `numListeners` cover only the calling object's listeners.**
  A signal such as `Input.joyConnectionChanged` is shared by the whole game.
- **`node[name].add(listener)` reaches a signal by its AS3 name** when you only know the name at run time.
- **Godot's `connect` isn't declared.** Use `add`.

### Declare a signal

Tag a public field of type `Signal` with `[Signal]`, in a class that extends a Godot class. Other scripts
listen to it like any signal, and you emit it with `emit`:

```actionscript
package game {
    import godot.*;

    public class Coin extends Area2D {
        [Signal] public var collected:Signal;

        override public function _ready():void {
            bodyEntered.add(onBodyEntered);
        }

        private function onBodyEntered(body:Node2D):void {
            collected.emit(10);
            queueFree();
        }
    }
}
```

## Export a field to the inspector

`[Export]` shows a field in Godot's inspector. A value saved in a scene is given to the script after its
constructor runs.

The field must be:

- A public instance field of a class that extends a Godot class.
- Of a type Godot can store: a number, `Boolean`, `String`, `Array`, one of Godot's values, a `Resource`,
  or, in a node's script, a `Node`.
- Given a constant as its first value: a number, a string, a Boolean, `null`, nothing, or a Godot value
  made from numbers, such as `new Vector2(1, 2)`.

The compiler says which rule a field breaks. In the editor, no script code runs: the inspector shows each
exported field at its first value.

## Null and freed objects

- **A Godot object can be compared with `null`.**
- **An object that Godot has freed reads as `null`.** `node == null` is true, and `trace(node)` prints
  `null`.
- **Using a `null` or freed object is an AS3 error** with a file and a line. It's never a crash.
- **`node as Enemy` gives the script object** when the node's script is `Enemy` or extends it, and `null`
  otherwise. `node is Enemy` tests the same thing.

## Errors

A script error inside a call from the engine appears in Godot's error panel, with the `.as` file and line
as its source and the AS3 stack trace. The game carries on.

- **A value from Godot is checked against its declared type.** A value of another type is an AS3 error.
- **An error in a listener comes out of the `emit()` that ran it,** so a `catch` around the `emit()` catches
  it.
- **A call from the engine that runs longer than 15 seconds is stopped** with Error #1502, and the game
  carries on. The limit is the same on the interpreter and in native code.

## Tests in a project

Scripts in the project's test folders aren't part of the game, so a mistake in a test can't stop the game
from running. A run that starts from a scene or script inside a test folder includes them. See
[Testing](../testing/README.md).

## Native code

Your scripts run on the interpreter, which needs no build step, or as native code. Either way you have
[hot reload](hot-reload.md).

- **When you press Play.** On macOS the extension builds a program library by itself. The first run after
  an edit starts on the interpreter while the library builds in the background, and the next run is native.
  Builds are cached under `.godot/kas3`. This needs Xcode's command line tools. If there's no C++ compiler
  or a build fails, Godot prints one warning and the interpreter runs the scripts. The project setting
  `actionscript/native/play` turns the build off. On other systems it's off unless you turn it on.
- **By hand.** Build a program library with `kas3c cpp` and put it in the project's `bin` folder. See
  [the extension's README](../compiler/godot_ext/README.md#the-program-library).

[How it works](how-it-works.md#two-ways-to-run) explains the two ways of running a program, and
[Performance](performance.md) has the numbers.

## Project settings and environment variables

| Project setting | Default | What it does |
|---|---|---|
| `actionscript/native/play` | On, on macOS. Off elsewhere | Builds and runs native code when a game runs from its project folder |
| `actionscript/limits/script_timeout_seconds` | `15` | How long a call from the engine can run before Error #1502 stops it. `0` means no limit |
| `actionscript/tests/folders` | `["res://tests"]` | The folders whose scripts are left out of the game |

| Environment variable | What it does |
|---|---|
| `KAS3_NATIVE_PLAY` | `1` turns native code on Play on, and `0` turns it off, whatever the project setting says |
| `KAS3_CXX` | The C++ compiler that builds the program library, as a path or a name on the `PATH` |
| `KAS3_PROGRAM_LIBRARY` | A program library to load in place of the one in the project's `bin` folder |
| `KAS3_SOURCE_DIR` | In a debug build, a folder outside the project to read and watch scripts in. A game in the iOS Simulator uses it to reload scripts you edit on your Mac |
| `KAS3_TESTS` | When set, the run includes the scripts in the test folders |

## iOS

Select an iOS simulator in Rider’s Run toolbar and press **Run**. The default debug build interprets AS3
and supports hot reload. It needs no Apple account. A release export compiles the selected AS3 scripts
into the app executable. Godot chooses the mode when it exports the Xcode project.

In **project.godot → Settings → Export**, select the iOS preset and set **AS3 execution** to
**Automatic**, **Interpreter**, or **Native**. Use **Native** to exercise compiled code in a debug
run. The console prints `ActionScript: the scripts run as native code.` when the app loads the compiled
program. Editing a script during that session moves execution to the interpreter with its state.
Godot exposes the same choice under **ActionScript → Native Code** in its export preset.

For device builds, choose the **Device signing team** by name and click **Apply**. Rider reads the
teams Xcode has cached on this Mac. If your team is missing, click **Open Xcode**, sign in under
**Settings → Apple Accounts**, then refresh the picker in Rider. You can also enter a team ID manually.
[Apple’s device signing guide](https://developer.apple.com/documentation/xcode/building-and-running-an-app).

- `compiler/tests/build_ios.sh <path to godot-cpp>` builds the extension for iOS as an `.xcframework`. It
  needs Xcode.
- Godot's published iOS export template has no Simulator library for Apple silicon.
  `compiler/tests/build_ios_simulator_template.sh` builds one from Godot's source.
- The Rider plugin's [iOS Simulator run configuration](../editors/jetbrains/README.md#run-on-ios-or-android)
  installs and launches the app, and passes `KAS3_SOURCE_DIR` so that hot reload works in the Simulator.

## Web (WebAssembly)

Godot 4.7's Web export runs ActionScript 3 games in the browser via a WebAssembly GDExtension side module
(`web.wasm32` in `kas3.gdextension`).

- Build the Web extension once with Emscripten (`emcmake` / `emcc`):
  `compiler/tests/build_web.sh <path to godot-cpp>`. This writes `bin/web/libkas3_godot.wasm`.
- Configure a Godot **Web** export preset with **Extensions Support** enabled (`variant/extensions_support=true`),
  **Thread Support** disabled (`variant/thread_support=false`), and `include_filter="*.as"`.
- Export with `godot --headless --path <project> --export-release "Web" <out>/index.html` and serve over HTTP.
  Because thread support is off, the export runs on standard static hosts without `SharedArrayBuffer` headers.
  Web exports currently run on the interpreter.

## Limits

The [roadmap](roadmap.md#known-limits) lists the known limits. The ones you're most likely to meet:

- Only macOS is tested.
- Godot's string-format operator (`"%s" % value`) isn't available. Use AS3 string concatenation.
- A method of Godot's isn't a value: `timer.timeout.add(queueFree)` is refused. Pass a method of your own
  that calls it.
- A Godot class isn't a class value: `var kind:Class = Node2D` is refused. `is`, `as` and `new` take the
  class by name.
- A Godot value or object held in a `*` or an `Object` has no members by name, and no `for in`. Store it in
  a variable of its type first.
- A script class in the default package with the same name as a Godot class is ambiguous. Put it in a
  package.
- `super._process(delta)` on a Godot virtual does nothing, as Godot's own empty versions do. It can't
  reach a virtual that another C++ extension implements.
