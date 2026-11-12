# Kumikae AS3 compiler

The front end of the ActionScript 3 compiler for Godot: source text to a checked syntax tree. It contains the
lexer, the parser, name resolution, and the first part of type checking: every expression has a type, and
`a.b` resolves to a member.

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
compiler/build/kas3c check --declarations godot.as src/*.as
```

`--dump-types` prints the type of each expression statement and each initializer, as `<line>:<col> <type>`.
A type is a class name, `Vector.<T>`, `Class.<T>` for a class itself (the expression `Math`), `*`, `void`,
`null`, or `?` when the compiler could not work it out.

`--dump-names` prints one line per resolved name, in source order:
`<line>:<col> <name> -> <kind> <qualified name> <where>`, where `<where>` is `@<line>:<col>` in the same
file, `@<path>:<line>:<col>` in another, or `(built in)`.

## Declaration files

A declaration file tells the compiler which names exist without giving their code. It is ordinary AS3 in
which classes and functions are `native` and have no body. Like any file, it has one package block.

```actionscript
package godot {
    public native class Node {
        public native function add_child(node:Node):void;
    }
}
```

The built-in library (`Object`, `String`, `Error`, `trace`, `flash.utils.Dictionary`, and the rest) is a set
of declaration files in `builtins/`, embedded into the compiler when it is built. Pass further ones with
`--declarations`. Stage 2 generates the declarations for Godot's classes from Godot's own API description;
until then `tests/stubs/godot.as` is a small hand-written stand-in.

A declaration file describes code that was compiled without your program, so your program can't change what
its names mean: inside it, names resolve only to other declarations, and its own package comes first.

## What `check` checks

- **Definitions:** duplicates in a package or a class; one definition per package block, named after its
  file; `dynamic` classes and `native` outside a declaration file are rejected.
- **Imports and type names:** every import and every written type must name something visible. A name
  written with its package (`flash.utils.getTimer`) still needs the import, as in Adobe's compiler. After
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
  is reached through its own class (`Base.count`, not `Sub.count` and not `object.count`).

Not checked yet (the next part of stage 1c): whether a value fits where it is used (assignments, arguments,
returns), argument counts, override signatures, whether a class implements all of its interfaces' methods,
and assignment to `const`.

## How expressions are typed

- **Arithmetic on two numbers is a `Number`,** as in AS3: `int + int` is computed without wrapping, and wraps
  only when it is stored in an `int`. Bitwise operators give `int`; `>>>` gives `uint`.
- **`+` with a `String` on either side is a `String`.** With a `*` on either side it is `*`.
- **A whole-number literal is an `int` if it fits, then a `uint`, then a `Number`.**
- **An unknown member of a dynamic class is `*`.** The dynamic classes are `Object`, `Array`, `Vector`,
  `Date`, `RegExp`, `Error` and `Dictionary`. Their subclasses are not dynamic.
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
- **A built-in name can't be told apart from an imported one of the same spelling.** With a class `Object`
  in package `godot`, `import godot.*` makes a bare `Object` ambiguous, and the built-in one has no longer
  spelling. Stage 2 must settle how Godot's classes are named with this in mind.
- **Names are checked only when every file parses.** With a syntax error anywhere, `check` reports syntax
  errors only.
- **One function may declare a variable twice only with the same type.** Adobe's compiler warns in that case
  and this one is silent; with different types both report an error.
- **Helpers outside the package block count as part of the file's package** when deciding whether an
  `internal` class is visible. Adobe's compiler is stricter.
- **Inside a class whose base class is unknown, undefined names aren't reported,** because the missing class
  may declare them.
- **Members of the built-in classes are exempt from the override rules.** A subclass of `Array` may define
  `push`, and any class may define `toString`, without `override`. Not checked against Adobe's compiler.
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
- `tests/golden/types/`: whole programs through `kas3c check --dump-types`.
- `tests/test_names.cpp`: name resolution, one check per message, with the expected error positions marked
  in the source as `/*!*/`.
- `tests/golden/names/ok/X.as` with `X.names`, and `tests/golden/names/errors/X.as` with `X.diag`: whole
  programs through `kas3c check`.
- The corpus is also checked as one program, with `tests/stubs/godot.as` standing in for Godot.

To add a golden test, add the `.as` file, run
`python3 compiler/tests/run_golden.py compiler/build/kas3c compiler/tests --update`, and **read the generated
file** before committing it.
