# Kumikae AS3 compiler

The front end of the ActionScript 3 compiler for Godot: source text to a checked syntax tree. It contains the
lexer, the parser, name resolution, type checking, and Godot's API as AS3 declarations.

It is a static library (`kas3front`) built without exceptions or RTTI, so that the same code can run in the
command-line tool, inside the Godot editor, and in the language server. The language server is a second
library, `kas3lsp` (`lsp/`), and both are in `kas3c`.

## Where things are

The C++ follows [docs/cpp-guidelines.md](../docs/cpp-guidelines.md); `tools/check_style.py`, which runs with
the tests, enforces the limits on size.

Each folder has its own README, with what it may depend on and where a new thing goes; the rules for
anyone changing the repository are in [AGENTS.md](../AGENTS.md).

| Folder | Holds | Files |
|---|---|---|
| `src/` | The front end: source text to a checked syntax tree | `lexer`, `parser*` (one file per kind of syntax), `names*` (packages, scopes, links), `typer.h` and `typer_*.cpp` (the type of every expression and what fits where), `type_table.cpp`, `whole_numbers` (rules the checker and the lowerer share), `query` (what the language server asks) |
| `ir/` | The typed IR and the lowerer that produces it | `ir.h`, `ir_print.cpp`, `lowerer.h` and `lower_*.cpp`, by what each lowers: types, places, Godot's members, expressions, operators, calls, statements, classes |
| `vm/` | The interpreter. No Godot header | `interpreter.cpp` (frames, calls, errors), `interpreter_run.cpp` (the loop), `interpreter_host.cpp` (requests to the engine), `interpreter_objects.cpp` (lifetime), `numeric` (conversions C++ alone would not make safe), `runtime` (strings and objects) |
| `vm/` host | What the interpreter needs from where it runs | `host.h` holds four interfaces: `output.h`, `engine_handles.h` (what is done to a value the interpreter holds), `engine_source.h` (where values come from), `clock.h` (the time and the zone, for Date). `no_engine.h` answers where there is no engine |
| `godot_ext/` | The GDExtension | `godot_host` (the four interfaces, with Godot's calls), `callable` (a method as a Godot function, and the look for listener loops), `instance`, `script`, `program`, `language`, `register` |
| `lsp/` | The language server | `server`, `workspace`, `json` |
| `godot/` | The generator of Godot's API as AS3 declarations | `generate.py`, `naming.py`, the generated `godot.as` and `godot_names.txt` |
| `tests/` | Tests, and `bench/` for the timings | See [Tests](#tests) |

## Build and test

```sh
cmake -S compiler -B compiler/build
cmake --build compiler/build -j
ctest --test-dir compiler/build --output-on-failure
```

Run the suite once with sanitizers before a release:

```sh
cmake -S compiler -B compiler/build-san -DKAS3_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build compiler/build-san -j && ctest --test-dir compiler/build-san --output-on-failure
```

## Command line

```sh
compiler/build/kas3c parse src/Player.as              # report syntax errors
compiler/build/kas3c parse --dump-ast src/Player.as   # also print the syntax tree
```

Diagnostics go to standard error as `<file>:<line>:<column>: error: <message>`. Exit status is 0 for no
errors, 1 if any file has syntax errors, and 2 for bad usage or an unreadable file.

`kas3c check` reads several files as one program and also reports names that are unknown, ambiguous,
duplicated or misused:

```sh
compiler/build/kas3c check src/*.as                        # report syntax and name errors
compiler/build/kas3c check --dump-names src/Player.as      # also print what every name refers to
compiler/build/kas3c check --declarations lib.as src/*.as  # lib.as declares what exists elsewhere
compiler/build/kas3c check --no-godot src/*.as             # without Godot's API
```

Godot's API (the `godot` package) is built into `kas3c`, so `import godot.*` needs no option.

`--dump-types` prints the type of each expression statement and each initializer, as `<line>:<col> <type>`.
A type is a class name, `Vector.<T>`, `Class.<T>` for a class itself (the expression `Math`), `*`, `void`,
`null`, or `?` when the compiler could not work it out.

`--dump-names` prints one line per resolved name, in source order:
`<line>:<col> <name> -> <kind> <qualified name> <where>`, where `<where>` is `@<line>:<col>` in the same
file, `@<path>:<line>:<col>` in another, `(built in)`, or `(Godot)`.

## Running a program

`kas3c run` checks the files as one program and runs it. The first file's class is constructed, as Flash
constructs a document class, and what it traces goes to standard output.

```sh
compiler/build/kas3c run Main.as game/*.as   # run; the entry class is Main
compiler/build/kas3c ir Main.as              # print the typed IR that is run
```

An error that nothing catches goes to standard error with an AS3 stack trace, and the exit status is 1:

```
TypeError: Error #1009: Cannot access a property or method of a null object reference.
	at Player/hit()[Player.as:10]
	at Player()[Player.as:6]
```

**What runs so far (stage 2a):** classes with fields, constructors, methods and statics; inheritance with
`override` and `super`; `int`, `uint`, `Number`, `Boolean`, `String` and `null` with AS3's conversions;
every arithmetic, comparison, logical and bitwise operator; assignment and `++`/`--`; `if`, `while`, `do`,
`for`, `break`, `continue`, `return`; calls with default parameters; `new`, `is`, `as`; `trace`; `throw`;
`toString()` on any object (its class's own, or `[object Name]`; an `Error`'s name and message);
`Error` and its subclasses; `NaN`, `Infinity` and the limits of the number types (`int.MAX_VALUE`); several
files, packages, and package-level functions and variables. `String`'s methods, the numbers' (and `Int64`'s) `toString(radix)`,
`toFixed`, `toExponential` and `toPrecision`, `parseInt`, `parseFloat`, `isNaN`, `isFinite`, `split` and `Math`.
`Vector.<T>` (`Vector.<*>` too) and `Array`, with their methods, those that take a function included;
function values (a method or a function as a value, `f(a, b)`) and function expressions that capture
their variables, and that use the members of the object they are written in (`count++`, `update()`);
`[Signal]` on a field of a class that extends one of Godot's. `RegExp` (literals, `new RegExp`, the flags
`g i m s x`, its properties, `exec` and `test` with `lastIndex`) and String's `match`, `search`, `replace`
(with `$1`, `$&` and the rest, or a function) and `split` by one; `Date`, with the host's clock and zone
(its getters and setters, local and UTC, its texts, `Date.parse` and `Date.UTC`); `escape`, `unescape`,
`encodeURI`, `encodeURIComponent`, `decodeURI` and `decodeURIComponent`.

**What is refused, by name, until later slices:** `Object` literals and dynamic properties (so `exec`'s
result has no `index`, `input` or named groups yet), `Dictionary`, interfaces, `for in` over anything but a
vector or an Array, getters and setters, `this` written inside a function expression (AS3's is the
caller's object), a class that extends `RegExp`, `JSON`, and the `godot` package. A program that uses
one of these does not run: the compiler says `... is not supported by the interpreter yet` at the place.

How it works:

- **The IR** (`ir/ir.h`) is the one meaning of a program that the interpreter and, later, the C++ generator
  both read. It is register-based and typed: an instruction says whether it adds two `Number`s or compares
  two strings, so values carry no tags. It is plain data with no pointers into the syntax tree, so that it
  can be written to a file and run without the compiler.
- **Arithmetic follows AS3 exactly.** `int + int` is computed as a `Number` and wraps to 32 bits when it is
  stored in an `int`, which is what the checker's types say.
- **The interpreter** (`vm/interpreter.h`) keeps its own stack of frames and never recurses in C++ for an AS3
  call. Runaway recursion is therefore an AS3 error, not a crash, and a debugger can walk the frames.
- **For a debugger:** every instruction has a source position, every function lists its named locals, and
  each statement begins with a marker where a hook can stop the program.
- **Static fields** get their values the first time their class is used, a base class before its
  subclasses, as in Flash. A class nothing uses is never set up.
- **A missing object** is reported where it is used, after a call's arguments have been evaluated, as in
  Flash.
- **Memory** is reference-counted. Objects that refer to each other in a loop are not freed yet.
- **Nothing in `vm/` or `ir/` names Godot.** What the engine will supply goes through the interfaces in `vm/host.h`.

Strings are held as UTF-8 and counted, indexed and compared by UTF-16 unit, as AS3 does: a character past
U+FFFF is two units, and `charAt` gives each half (`vm/text_units.h`). Case changes a character for a
character, by Unicode's mappings within U+0000 to U+FFFF (`"ß".toUpperCase()` stays `"ß"`).

Where Flash and ECMAScript part, Flash is followed as far as it is known, and each test says so where it
does: `toString(radix)` writes only the whole part of a number in a radix other than 10, a
digit count left out is the declared 0 (`toPrecision()` is a `RangeError`), `replace` with a String to
find inserts its replacement as it is written, and `localeCompare` orders by UTF-16 unit.

**Regular expressions** are read and run by the interpreter's own engine (`vm/regex*`), over UTF-16 units
as AS3 indexes strings: ECMAScript 3's syntax and meaning, with what AS3 takes from PCRE (the flags `s`
and `x`, named groups `(?P<name>...)`, lookbehind of a fixed length, atomic groups, possessive
quantifiers). A pattern that can't be read is a `SyntaxError` where the RegExp is made (ECMAScript's rule;
Flash, by recollection, makes one that never matches). A search that backtracks without end stops after
ten million steps, plus a thousand for each unit of the text, with an `Error` that says so.

**Date** asks the host for the time and for the zone's offset at a moment (`vm/clock.h`): Godot's clock in
the extension, the system's in `kas3c run`, where `KAS3_NOW` fixes the time and `TZ` the zone. Its texts
are AS3's: `Thu Feb 29 13:05:09 GMT-0800 2024`, `Thu Feb 29 21:05:09 2024 UTC`, and the rest, written
from recollection of avmplus, not checked against Flash.

## Inside Godot

`godot_ext/` is a GDExtension that registers ActionScript 3 as a Godot scripting language. A `.as` file is
then a script: attach it to a node whose class it extends.

It needs [godot-cpp](https://github.com/godotengine/godot-cpp) for Godot 4.7, which is not in this
repository. Give CMake a checkout of it:

```sh
cmake -S compiler -B compiler/build-godot -DKAS3_GODOT_CPP=<path to godot-cpp>
cmake --build compiler/build-godot --target kas3_godot
```

The library is written to `tests/godot_project/bin`, beside the project that tests it:

```sh
python3 compiler/tests/run_godot.py compiler/tests   # runs the project in headless Godot 4.7
```

To use it in a project of your own, copy `tests/godot_project/kas3.gdextension` and the `bin` folder into
it.

**What works so far (stages 2b-1 to 2b-3):**

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

- A script class extends a Godot class. Godot calls the virtual methods it overrides (`_physicsProcess` is
  Godot's `_physics_process`), and any other method by its AS3 name, from GDScript too.
- The script reads and writes the node's properties, calls its methods, and makes Godot values with `new`.
- Numbers, Booleans, strings, Godot objects and Godot values cross in both directions. GDScript can read a
  script's fields. A call from GDScript with too few arguments, the wrong kind of argument, or a number that
  does not fit an `int` is refused, never bent to fit.
- A script class may extend another script class, and may be attached to a reference-counted object
  without keeping it alive.
- A Godot object can be compared with `null`. Calling a method on one that Godot has freed is an AS3 error.
- Operators on Godot's values are Godot's own: `position + velocity * delta`, `-v`, `v == w`, `v += w`. Two
  Godot objects are equal when they are the same object.
- `trace(v)`, `"at " + v`, `String(v)` and `v.toString()` give Godot's text for a value or an object.
- One part of a value can be written: `position.x = 5` reads the value, changes the part and writes it
  back. So do `position.x += 5`, `transform.origin.y = 3` and `v[0] = 1`.
- An argument left out takes Godot's default. So does a `null` written last for a parameter declared
  `= null`.
- A script's `toString()` is what `str(node)` and `print(node)` show in GDScript.
- Signals are listened to the AS3 way: `timer.timeout.add(onTimeout)`, `add(f, true)` for the end of the
  frame, `addOnce`, `remove`, `removeAll`, `has`, `numListeners`, `emit`. Adding a listener twice does
  nothing. `removeAll` and `numListeners` are about the listeners scripts added, not the engine's own. A
  listener goes when its object is freed.
- A method is a function wherever Godot takes one: `array.filter(isBig)`.
- `timer[name].add(f)` reaches a signal by its AS3 name when the name is only known as the program runs.
- A singleton is used by its class name: `Input.isActionPressed("jump")`, `Engine.timeScale = 0.5`,
  `Input.joyConnectionChanged.add(onJoy)`. `Input.singleton` is the object itself, where one is wanted.
- Godot's constants: `Key.ESCAPE`, `Node.NOTIFICATION_READY`, `Vector2.ZERO`, `Color.RED`. One that has a
  number in the declarations costs nothing when the program runs.
- Static methods: `Vector2.fromAngle(a)`, `Color.html("#ff8000")`, `Image.createEmpty(...)`.
- A variable of a Godot value that has parts (`Vector2`, `Color`, `Transform2D`) starts at the type's zero
  when no value is written, as in GDScript. `null` can still be assigned. Any other Godot value (an array,
  a signal) starts missing.
- Godot's integer is an AS3 type, `Int64`. `items.size()`, `randi()` and an object's id are `Int64`s.
  One converts to an `int`, a `uint` or a `Number` where that is expected, and a `RangeError` says so if
  the value does not fit; `int(x)` written out cuts it, as in Flash. Arithmetic on it wraps at 64 bits,
  `/` gives a `Number`, and a comparison with a `Number` is exact.
- Godot's global functions: `print("x", n)`, `lerpf(a, b, t)`, `clampi(v, 0, 10)`, `randf()`, `str(...)`.
  One that can give back anything (`lerp`, `max`) is used where the program says what it expects:
  `var x:Number = lerp(a, b, 0.5)`.
- A Godot object that has been freed is `null` to a script: `node == null` is true, `if (node)` is
  false, and a member reached on it is AS3's null error, which says the object was freed.
- `null` written for a default AS3 can't write takes Godot's own default, wherever it is in the call:
  `color.clamp(null, max)`.
- What Godot gives back is checked against its declared type. A value of another type is an AS3 error,
  never taken as if it fitted.
- An AS3 error inside a call from the engine is reported as Godot reports a script's own error, with the
  `.as` file and line as its source and the AS3 stack trace, and the engine carries on.

**Refused by name until the next slices:** elements of Godot's untyped collections, creating
Godot objects with `new`, passing a script object to Godot, and everything `kas3c run` refuses.

How it works:

- **One program per project.** Every `.as` file under the project is compiled together when the first
  script loads. A script that fails to compile is reported once, and no script loads.
- **The interpreter does not know Godot.** `vm/host.h` is the whole boundary: four small interfaces. The extension implements them
  with Godot's own calls; the tests implement it with a recorder.
- **Godot's names come from Godot.** `godot/godot_names.txt` (21,519 names, written by the generator from
  Godot's API description) says that `moveAndSlide` is `move_and_slide`. Nothing is worked out backwards
  from the AS3 name.
- **In the interpreter a Godot value is boxed** and calls go by name. That is the simple, slow path, in
  GDScript's range. The same typed IR will give the native backend direct calls and inline values.
- **Calls can nest.** The engine calls a script, which calls the engine, which may call a script again,
  up to 64 levels deep.

Known fault: the first time Godot's editor scans a project that has the extension (for example
`godot --headless --import`), Godot crashes as it exits, after the scan has done its work. Later runs are
clean. The speed spike's extension does the same; the cause is not yet known.

## Editing a script while the game runs

When a project is run from its folder in a debug build, the extension looks at its `.as` files five
times a second. When one changes, every script is compiled again:

- **If they compile,** every script object alive is moved onto the new code. A field that kept its name
  and kind keeps its value, static fields too, and listeners added before keep listening and run the
  new code. Godot prints `ActionScript: reloaded N scripts.`
- **If they do not,** the errors are printed and the game carries on with the scripts it had.

What to expect of an object that already exists: a field that is new, or whose type changed, starts
empty (0, `NaN`, `null`), because the object is not constructed again; a new first value for a field
shows on objects made afterwards. A class that was removed while one of its objects is alive stops the
reload, and Godot says to restart. An exported game does not watch its scripts.

## Language server

`kas3c lsp` serves an editor over standard input and output with the Language Server Protocol. To connect an
editor, see [editors](../editors/README.md).

```sh
compiler/build/kas3c lsp                        # the project is the folder the editor opens
compiler/build/kas3c lsp --stdio                # the same: some editors' clients add it
compiler/build/kas3c lsp --no-godot             # without Godot's API
compiler/build/kas3c lsp --declarations lib.as  # lib.as declares what exists elsewhere
```

| It answers | With |
|---|---|
| A file opening or changing | The file's errors: what `kas3c check` prints for the project. |
| Go to definition | Where the name under the cursor is declared, in the project, the built-in library or Godot's API. |
| Find references | Every use of it in the project's files. |
| Completion | After a dot, what the value before it has, with what its bases have (`Object`'s members for a `*` or a value of a type not known), or what a package's path holds; where a type is written, types; after `override`, the base classes' methods, each written as its whole declaration; otherwise the names in reach: locals, members, definitions, keywords, with a call argument's parameter name first. Narrowed by what is typed so far, whatever its case; package/type names in imports. Nothing after a name that means nothing here, and nothing in a comment, string or regular expression. |
| Signature help | The callable declaration and active parameter, including unfinished and nested calls. |
| Hover | Its declaration, and the class or package it belongs to. |
| Document symbols | The classes and members of the file. |
| Workspace symbols | The project's definitions whose name contains the query. |

How it works:

- **The project** is the nearest folder at or above the opened one that has a `project.godot`, or else the
  opened folder. Every `.as` file under it is part of the program, except in folders whose name starts with
  a dot. An open file's text in the editor stands in for the file on disk.
- **Every change checks the whole program again.** A file that did not change keeps its syntax tree, so
  only the changed file is parsed again. On a generated project of 100,000 lines a re-check takes about
  27 ms with Godot's API loaded (`build/bench_recheck <folder>`; `tests/bench/generate_project.py` writes
  the project).
- **Typing bursts are checked once.** A change marks the program as stale; the check runs when no further
  message is waiting, or when a request needs an answer.
- **A syntax error silences only what it may have caused,** as in `kas3c check`: the rest of its own file,
  every file that uses something that file declares, and every complaint that a name is missing. Other
  errors are still shown. Names are still resolved, so go to definition, hover and the outline keep working
  in a half-typed file.
- **Positions** are counted in UTF-16 code units, or in bytes when the editor offers UTF-8.
- **Godot's API and the built-in library** are written, read-only, to `kas3c-declarations` in the system's
  temporary folder, so that go to definition has a file to open. No errors are ever reported for them.

The library side is `Compilation::check(true)` (resolve names despite syntax errors), `parse_file` and
`Compilation::add(parsed)` (reuse a parse), and `Queries` in `src/query.h` (what is at a position, where it
is declared, who refers to it). The Godot editor will use the same three.

Limits:

- Find references on a method also lists the methods that override it and the one it overrides, and an
  interface's method the methods that implement it.
- Workspace symbols are the project's own first, then Godot's classes and the built-in library's.
- A change arrives as the range of text that changed; a whole document is accepted too.
- Rename and code actions are not implemented. Completion and signature help are described in
  [the editor guide](../editors/completion.md).
- A file is one file however the editor reaches it: through a symbolic link, in another letter case, or in
  another Unicode form of its name.
- A file the editor opens from outside the project, or from a folder whose name starts with a dot, is not
  checked, unless no folder is open at all.
- A project of more than 20,000 `.as` files is read up to that number. The server says so in the editor
  and on standard error.
- **Windows:** paths are compared in one spelling (`/` between parts, a drive's letter in capitals, letter
  case not counted), `file:` URIs carry drives and shares, and the server's input and output are binary.
  The path rules are tested with Windows paths on every system; the server has not yet been built or run
  on Windows itself.

## Declaration files

A declaration file tells the compiler which names exist without giving their code. It is ordinary AS3 in
which classes and functions are `native` and have no body. Like any file, it has one package block.

```actionscript
package physics {
    public native class Body {
        public native function applyForce(x:Number, y:Number):void;
    }
}
```

The built-in library (`Object`, `String`, `Error`, `trace`, `flash.utils.Dictionary`, and the rest) is a set
of declaration files in `builtins/`, embedded into the compiler when it is built. Pass further ones with
`--declarations`. Godot's API is one more declaration file, described in the next section.

A declaration file describes code that was compiled without your program, so your program can't change what
its names mean: inside it, names resolve only to other declarations, and its own package comes first.

## Godot's API

`godot/godot.as` declares all of Godot 4.7's API for AS3: 1,036 classes, 30 value and collection types, 22
global enums and 112 global functions. `godot/generate.py` writes it from Godot's own API description under
the naming design (`docs/superpowers/specs/2026-11-12-godot-naming-design.md`), and the build embeds it.

| Godot | AS3 |
|---|---|
| `move_and_slide()` | `moveAndSlide()` |
| `is_visible()`, `set_visible(v)` | the property `visible` |
| `_physics_process(delta)` | `override public function _physicsProcess(delta:Number):void` |
| the signal `timeout` | `timer.timeout.add(f)`, also `add(f, true)` (deferred), `addOnce(f)`, `remove(f)`, `removeAll()`, `has(f)`, `numListeners`, and `timer[name].add(f)` for a name known at run time; Godot's `connect` is not declared |
| `KEY_ESCAPE` | `Key.ESCAPE` |
| `Node.PROCESS_MODE_PAUSABLE` | unchanged |
| `Input.is_action_pressed("jump")` | `Input.isActionPressed("jump")` |
| `Object`, `Array`, `Dictionary`, `JSON`, `Error` | `GodotObject`, `GodotArray`, `GodotDictionary`, `GodotJSON`, `GodotError` |
| `print()`, `lerp()`, `deg_to_rad()` | `print()`, `lerp()`, `degToRad()` |

What the declarations decide beyond the names:

- **Integers.** Godot's integer has 64 bits and is the AS3 type `Int64`. An integer is an `int` or a
  `uint` only where Godot's description says it is narrower: by a width mark, or by belonging to a type
  that stores 32-bit integers (`Vector2i` and its relatives, `PackedInt32Array`). Nothing is kept by hand.
- **Only virtual methods can be overridden.** Every other method, property and signal is `final`, so
  `override public function addChild(...)` is an error: the engine would never call it.
- **`toString()` is AS3's.** A script writes `public function toString():String` as in any AS3 class.
  Godot's `to_string()` and `_to_string()` are not declared; the binding maps them.
- **A class the engine won't create** (`CanvasItem`, `Input`, `Key`) is tagged `[Abstract]`, and `new` on it
  is an error. A script may still extend one.
- **`GodotObject`'s virtuals and `free()`** (`_notification`, `_get`, `_set` and four more)
  are missing from Godot's description. The generator adds them from a list of its own.
- **Enums** are `int`. A `[Godot(...)]` tag on the member records which enum a parameter or result is.
- **Defaults.** A default that is not a number, boolean, string or `null` is written `null`; the real
  default is applied when the call is made.
- **Value types** (`Vector2`, `Color`) have one constructor that accepts each of Godot's forms:
  `new Vector2()`, `new Vector2(1, 2)`, `new Vector2(other)`.
- **Indexed properties.** `anchorLeft` is a property, and `setAnchor(side, value)` is still a method.
- **Singletons** are classes with static members: `Input.isActionPressed("jump")`.
- **Left out:** 60 methods that take raw pointers; `typeof` and `isFinite`, which AS3 has; Godot's own
  `String`, `StringName`, `NodePath` and `Callable` classes, for which you write `String` and `Function`.

Loading the declarations costs about 30 ms and 26 MB on each run of `kas3c check` (3 ms and 3 MB
without them, measured on one small file).

To regenerate for another Godot version, see [`godot/NOTICE.md`](godot/NOTICE.md). The generator stops on
anything it has no rule for: an unknown kind of type, a name that clashes with another or with an AS3
built-in.

## What `check` checks

- **Definitions:** duplicates in a package or a class; one definition per package block, named after its
  file; `dynamic` classes and `native` outside a declaration file are rejected.
- **Imports and type names:** every import and every written type must name something visible. A name
  written with its package (`flash.utils.getTimer`) needs no import. After
  an import that names nothing, the names it could have supplied aren't reported again.
- **The class hierarchy:** `extends` names a class that isn't final, `implements` names interfaces, and
  nothing inherits from itself.
- **Overrides:** on an instance method, `override` is present exactly when a base class has that method;
  final methods and fields can't be overridden; `override` anywhere else is an error. `Object`'s methods
  (`toString` and the rest) need no `override`, as in AS3.
- **Names in function bodies:** locals, parameters, closures, members, inherited members, package-level
  definitions and imports. Also `this` and instance members in static code, `break` and `continue` targets,
  and assignment targets.

- **Members through `.`:** the member must exist and be reachable. `private` is reachable inside its class,
  `protected` inside its class and subclasses, and unmarked members inside their package. A static member
  is reached through its own class (`Base.count`, not `Sub.count` and not `object.count`). A static and
  an instance member may share a name; a bare name in instance code means the instance member.

- **Values that don't fit:** a value stored in a variable, field or property, passed as an argument,
  returned, or given as a parameter default must fit the declared type. See "What fits" below.
- **Argument counts** of calls to a known function, method or constructor, and of `super(...)`.
- **Returns:** a value of a known type returned from a `void` function, a setter or a constructor (a `*`
  value, which may be `undefined`, is let through); a function with a
  declared type whose body has no `return` with a value and no `throw` anywhere.
- **Overrides** must repeat the parameters and return type of the method they override.
- **Interfaces:** a class must have a public method or accessor, with the same signature, for every method
  of every interface it names.
- **Writes** to a `const`, to a property with no setter, to a method, a package function or a class. A
  built-in class's methods may be replaced (`o.toString = f`), because they may live on its prototype.

## What fits

A value of type A may be used where B is expected when one of these holds:

- Either is `*`, or the compiler does not know one of them.
- A is B, or B is `Object` or `Boolean`.
- Both are number types (`int`, `uint`, `Number`).
- A is `null`.
- A is a class and B is one of its base classes, or an interface that it or a base class implements.
- A is an interface and B is one that it extends.
- Both are `Vector` with the same element type. `Vector.<Sub>` does not fit `Vector.<Base>`.
- A is a class itself (`Player`, not a player) and B is `Class`.
- B is one of Godot's collections and A is the AS3 collection that converts to it when it is handed to the
  engine:

  | B | A |
  |---|---|
  | `GodotArray` | `Array`, or any `Vector` |
  | `PackedInt32Array` and the other packed number arrays | `Array`, or a `Vector` of a number type |
  | `PackedStringArray` | `Array`, or `Vector.<String>` |
  | `PackedVector2Array`, `PackedVector3Array`, `PackedVector4Array`, `PackedColorArray` | `Array`, or a `Vector` of that type |
  | `GodotDictionary` | `Object`, or `flash.utils.Dictionary` |

  Nothing converts on the way back: a collection that comes from the engine stays Godot's.

Otherwise it is an error. When B is a subclass or implementer of A, the message asks for a cast
(`a value of type 'Base' may not be a 'Sub'; use 'as' or a cast`). A `void` result fits only `*`.

**Reported only where AS3's rule is certain.** These are not checked: compound assignment (`s += 1`),
operands of operators and comparisons (except on Godot's value types, described below), conditions, `for each` variables, calls through a `Function` value,
class names called as conversions (`int(x)`), an assignment used as a value (`a = b = c`), `null` stored in
a number (Adobe's compiler warns), and use of a value that is certainly `null`.

## Operators on Godot's value types

AS3 has no operators on objects. Godot's value types have them, and Godot's API description lists every
one, so `godot.as` carries them as tags on each class and the compiler looks them up:

```actionscript
[Operators]                                            // the list that follows is complete
[Operator(op="+", right="Vector2", returns="Vector2")]
[Operator(op="*", right="Number", returns="Vector2")]
[Operator(op="*", left="Number", returns="Vector2")]   // 2 * v
[Operator(op="-", returns="Vector2")]                  // -v
[Indexed(element="Number")]                            // v[0]
public final native class Vector2 { ... }
```

- **The operators are `+ - * / %`, unary `-` and `+`, and `< > <= >=`.** `==` and `!=` work on every type
  and need no tag.
- **The result has the type Godot gives it:** `position + velocity * delta` is a `Vector2`,
  `transform * point` is a `Vector2`, `transform * transform` is a `Transform2D`.
- **An operator the type doesn't have is an error:** `can't use '+' on 'Vector2' and 'Node'`. Nothing is
  said when the other operand is `*`, an `Object`, `null` or unknown, or when neither class lists its
  operators.
- **Whole numbers and fractions.** Godot gives `Vector2i * int` a `Vector2i` and `Vector2i * float` a
  `Vector2`. An operand is a whole number if it is an `int` or `uint`, or a sum, difference, product or
  remainder of whole numbers, so `cell * (i + 1)` is a `Vector2i`. `cell * 0.5`, `cell * n` and
  `cell * (i / 2)` are `Vector2`. A whole number may use a form Godot has only for fractions
  (`transform * 2`); a fraction may not use a whole-number form (`cell % 0.5` is an error). A call such as
  `Math.floor(x)` is a `Number` in AS3, so write `cell * int(Math.floor(x))`.
- **`+` with a `String` is a `String`,** as everywhere in AS3.
- **Compound assignment** (`position += step`) is checked the same way, and a result of another type is an
  error: `cell *= 0.5` gives a `Vector2`, which can't go back into a `Vector2i`.
- **Indexing** a class tagged `[Indexed]` with a number gives the element: `points[0]` is a `Vector2`,
  `vector[0]` is a `Number`. A `GodotArray` or `GodotDictionary` gives `*`. Any other key gives `*`,
  because AS3 lets `v["x"]` name a property. Storing into `points[i]` is checked like any store.
- **The tags work in any declaration file,** not only Godot's. A tag names a type as the file would:
  `Number`, `String`, a class of its own package, or `other.Rate`; a name that means nothing is reported
  at the tag. A subclass has its base's operators. In a program's own source the tags mean nothing.

## How expressions are typed

- **Arithmetic on two numbers is a `Number`,** as in AS3: `int + int` is computed without wrapping, and wraps
  only when it is stored in an `int`. Bitwise operators give `int`; `>>>` gives `uint`.
- **`+` with a `String` on either side is a `String`,** whatever the other side is. Otherwise, with a `*` on
  either side it is `*`.
- **A whole-number literal is an `int` if it fits, then a `uint`, then a `Number`.** `-0` is a `Number`,
  because an `int` has no negative zero.
- **An assignment has the type of what is assigned to.** `i = n` and `i += n` are `int` when `i` is. Adobe's
  reference describes the result as the assigned value; which of the two its compiler produces has not been
  checked.
- **`x as T` has the type `T`,** also for `int`, `Number` and `Boolean`, as in Adobe's compiler.
- **Only a number indexes a vector's elements.** `v[i]` with a numeric `i` has the element type; any other
  index is `*`.
- **An unknown member of a dynamic class is `*`.** The dynamic classes are `Object`, `Class`, `Array`,
  `Vector`, `Date`, `RegExp`, `Dictionary`, and `Error` with its built-in subclasses. Classes you derive
  from them are not dynamic.
- **Calling, or using `new` on, a value typed `Function`, `Class` or `Object` gives `*`.**
- **`this` inside a nested function is `*`,** because AS3 decides it when the function is called.
- **An object of AS3's in arithmetic (a `Date`, a vector, a class of the program) is made a primitive as
  the line runs,** by its class's own `valueOf()` or else its text: beside `+` the result is `*` (a String
  or a Number), with `-`, `*`, `/` and `%` a `Number`. A `Date` is its time where a Number is wanted, and
  its text beside `+`. Arithmetic on anything else that is not a number, a string or a Boolean has no type
  yet (`?`), unless Godot's declarations give the operator.
- **"has no member" is reported only when every member of the type is known.** Nothing is reported on `*`,
  on a type that failed to resolve, or on a class whose base class is unknown.

## What parses

The first-version language: packages, imports, classes, interfaces, members with modifiers and metadata,
functions and closures, the full statement and expression grammar, `Vector.<T>`, regular expressions, and
statements ended by a line break instead of a semicolon.

Reported as unsupported, each with its own message: E4X (inline XML), the `with` statement, namespaces, and
`include`.

## Limits

These keep every syntax tree shallow enough for later passes to walk by plain recursion.

- Constructs nest at most 256 deep by recursion (brackets, blocks, unary operators, `else if` chains).
- A chain of operators, member accesses or calls stops growing at 448 links; a longer one is reported as
  `expression is too long` and parsing continues.
- No tree is taller than about 512 levels in total, counting statements, nested functions and parameter
  defaults. Past that, the file is reported as `code is nested too deeply` and **no tree is returned**.
- Type arguments (`Vector.<Vector.<...>>`) nest at most 32 deep.
- Source files are limited to 4 GB (offsets are 32-bit).

Name resolution adds these rules. Where one differs from Adobe's compiler, it says so.

- **Nothing shadows anything at package level.** A simple name is looked up at once in the file's helpers,
  the current package, the top-level package and every import. Two different matches are an error; write
  the name with its package. Adobe's documentation says the same for two imports. Whether Adobe's compiler
  also reports a same-package or top-level name against an import has not been checked.
- **A built-in name can't be told apart from an imported one of the same spelling.** A class `Object` in an
  imported package would make a bare `Object` ambiguous, and the built-in one has no longer spelling. This
  is why Godot's `Object` is `GodotObject` here.
- **One function may declare a variable twice only with the same type.** Adobe's compiler warns in that case
  and this one is silent; with different types both report an error.
- **Helpers outside the package block count as part of the file's package** when deciding whether an
  `internal` class is visible. Adobe's compiler is stricter.
- **Inside a class whose base class is unknown, undefined names aren't reported,** because the missing class
  may declare them.
- **A built-in member may be redefined with or without `override`.** A subclass of `Array` may define
  `push`, and any class may define `toString`, either way. Which one AS3 wants depends on whether the member
  is in the `AS3` namespace, on the prototype, or public, and this compiler doesn't model that. Adobe's
  compiler is stricter.
- **A class itself has only its own static members.** `Player.toString()` and `Player.prototype` are
  reported as missing static members.
- **`protected` members are reachable through any object,** from code in the declaring class or a subclass.
  Adobe's reference doesn't say whether its compiler allows this.
- **`this` inside a closure in a static method is an error.** AS3 allows it, with `this` being the global
  object.
- **Static code doesn't see instance members.** A name that matches only an instance member is an error; a
  name that also matches something further out (an import, a base class's static) binds to that.
- **A dotted name prefers the longest package.** If package `a` defines `b` and package `a.b` defines `C`,
  then `a.b.C` is the class.

## Tests

- `tests/test_*.cpp`: unit tests for source positions, the lexer, the tree printer, and the parser.
- `tests/golden/ok/X.as` with `X.ast`: programs that must parse, and the tree they must produce.
- `tests/golden/errors/X.as` with `X.diag`: programs that must fail, and the exact diagnostics.
- `tests/corpus/*.as`: real-shaped programs that must parse with no diagnostics. The parser test also parses
  every byte-prefix of each one, to check that no malformed input can crash or hang it.

- `tests/test_types.cpp`: one expression per check, each with the type it must have.
- `tests/test_type_errors.cpp`: values that fit and values that don't, with the expected errors marked.
- `tests/test_godot_math.cpp`: operators and indexing on Godot's value types, against Godot's real
  declarations.
- `tests/golden/types/`: whole programs through `kas3c check --dump-types`.
- `tests/test_names.cpp`: name resolution, one check per message, with the expected error positions marked
  in the source as `/*!*/`.
- `tests/golden/names/ok/X.as` with `X.names`, and `tests/golden/names/errors/X.as` with `X.diag`: whole
  programs through `kas3c check`.
- The corpus is also checked as one program against Godot's API.
- `godot/test_naming.py` and `godot/test_generate.py`: the naming rules, the generator on a small fixture
  (`godot/fixture_api.json`), and Godot's whole API. The whole-API tests need Godot's API description,
  which is not in this repository; without it they are skipped and say so.

- `tests/test_query.cpp`: checking through syntax errors, reusing a parse, and the queries. It runs every
  byte-prefix of each corpus file, and each file with one byte missing, through all of them.
- `tests/test_json.cpp`, `tests/test_workspace.cpp`: the server's JSON module, and the project as files on
  disk with open buffers over them.
- `tests/test_lsp.cpp`: the server driven in process, one message at a time. It also checks that what the
  server publishes for every golden error file is what `kas3c check` prints, and types through each corpus
  file as an editor would.
- `tests/test_lsp_stdio.py`: `kas3c lsp` as a real process: framing, malformed input, exit codes.

- `tests/run/*.as` with `.out` (and `.err`): programs through `kas3c run`, the start of the conformance suite.
  The expected output for numbers, strings and evaluation order was produced by Node from a line-by-line
  JavaScript copy, since AS3 and JavaScript share those rules. `tests/run/refused/` holds one program per
  construct the interpreter does not run yet. Every other test program must run or be refused without
  crashing, and nothing a program makes may be left alive when it ends.
- `tests/test_ir.cpp`, `tests/test_runtime.cpp`, `tests/test_vm.cpp`: the IR printer, the number rules
  (checked against Node), the hook and frames a debugger will use, calls into the interpreter that nest
  and fail, and a script run against a fake engine that records every request.
- `tests/run_godot.py` with `tests/godot_project/`: the milestone script in headless Godot. It skips itself,
  saying so, without the extension library or Godot 4.7.

- `tests/fetch_external.sh` fetches six open-source AS3 projects (Starling, Away3D, Flixel, FlashPunk,
  as3-signals, as3corelib; about 190,000 lines) into `tests/external/`, which git ignores. They are pinned
  by commit and are not part of this repository. `python3 -I tests/run_external.py build/kas3c --check`
  parses and checks them and prints the errors counted by message. A crash or hang fails; errors don't,
  because these projects use Flash's library, namespaces and XML syntax. An error of any other kind on
  this code is presumed to be a bug in the compiler until shown otherwise.

To add a golden test, add the `.as` file, run
`python3 compiler/tests/run_golden.py compiler/build/kas3c compiler/tests --update`, and **read the generated
file** before committing it.
