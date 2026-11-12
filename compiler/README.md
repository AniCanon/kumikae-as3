# Kumikae AS3 compiler

The front end of the ActionScript 3 compiler for Godot: source text to a checked syntax tree. It contains the
lexer, the parser, name resolution, type checking, and Godot's API as AS3 declarations.

It is a static library (`kas3front`) built without exceptions or RTTI, so that the same code can run in the
command-line tool, inside the Godot editor, and in the language server.

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
the [naming design](../docs/superpowers/specs/2026-11-12-godot-naming-design.md), and the build embeds it.

| Godot | AS3 |
|---|---|
| `move_and_slide()` | `moveAndSlide()` |
| `is_visible()`, `set_visible(v)` | the property `visible` |
| `_physics_process(delta)` | `override public function _physicsProcess(delta:Number):void` |
| the signal `timeout` | `timer.timeout.connect(f)` |
| `KEY_ESCAPE` | `Key.ESCAPE` |
| `Node.PROCESS_MODE_PAUSABLE` | unchanged |
| `Input.is_action_pressed("jump")` | `Input.isActionPressed("jump")` |
| `Object`, `Array`, `Dictionary`, `JSON`, `Error` | `GodotObject`, `GodotArray`, `GodotDictionary`, `GodotJSON`, `GodotError` |
| `print()`, `lerp()`, `deg_to_rad()` | `print()`, `lerp()`, `degToRad()` |

What the declarations decide beyond the names:

- **Integers.** AS3's `int` is 32 bits. A Godot integer that the description marks as 64-bit is a `Number`,
  and an unsigned 32-bit one is a `uint`. The description marks widths only on the methods of classes; for
  global functions and value types a short table in the generator names the wide ones (`randi()`,
  `instanceFromId()`, `PackedInt64Array`'s elements).
- **Only virtual methods can be overridden.** Every other method, property and signal is `final`, so
  `override public function addChild(...)` is an error: the engine would never call it.
- **A class the engine won't create** (`CanvasItem`, `Input`, `Key`) is tagged `[Abstract]`, and `new` on it
  is an error. A script may still extend one.
- **`GodotObject`'s virtuals and `free()`** (`_notification`, `_toString`, `_get`, `_set` and four more)
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
operands of operators and comparisons, conditions, `for each` variables, calls through a `Function` value,
class names called as conversions (`int(x)`), an assignment used as a value (`a = b = c`), `null` stored in
a number (Adobe's compiler warns), and use of a value that is certainly `null`.

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
- **Arithmetic on anything but numbers, strings and Booleans has no type yet (`?`).** Godot's math types get
  their operators in a later part.
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
- **Names are checked only when every file parses.** With a syntax error anywhere, `check` reports syntax
  errors only.
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
- `tests/golden/types/`: whole programs through `kas3c check --dump-types`.
- `tests/test_names.cpp`: name resolution, one check per message, with the expected error positions marked
  in the source as `/*!*/`.
- `tests/golden/names/ok/X.as` with `X.names`, and `tests/golden/names/errors/X.as` with `X.diag`: whole
  programs through `kas3c check`.
- The corpus is also checked as one program against Godot's API.
- `godot/test_naming.py` and `godot/test_generate.py`: the naming rules, the generator on a small fixture
  (`godot/fixture_api.json`), and Godot's whole API. The whole-API tests need Godot's API description,
  which is not in this repository; without it they are skipped and say so.

- `tests/fetch_external.sh` fetches six open-source AS3 projects (Starling, Away3D, Flixel, FlashPunk,
  as3-signals, as3corelib; about 190,000 lines) into `tests/external/`, which git ignores. They are pinned
  by commit and are not part of this repository. `python3 -I tests/run_external.py build/kas3c --check`
  parses and checks them and prints the errors counted by message. A crash or hang fails; errors don't,
  because these projects use Flash's library, namespaces and XML syntax. An error of any other kind on
  this code is presumed to be a bug in the compiler until shown otherwise.

To add a golden test, add the `.as` file, run
`python3 compiler/tests/run_golden.py compiler/build/kas3c compiler/tests --update`, and **read the generated
file** before committing it.
