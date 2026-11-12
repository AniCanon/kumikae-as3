# Kumikae AS3 compiler

The front end of the ActionScript 3 compiler for Godot: source text to a checked syntax tree. It contains the
lexer, the parser and name resolution. Type checking comes next.

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

`--dump-names` prints one line per resolved name, in source order:
`<line>:<col> <name> -> <kind> <qualified name> <where>`, where `<where>` is `@<line>:<col>` in the same
file, `@<path>:<line>:<col>` in another, or `(built in)`.

## Declaration files

A declaration file tells the compiler which names exist without giving their code. It is ordinary AS3 in
which every class and function is `native` and has no body:

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

## What `check` checks

- **Definitions:** duplicates in a package or a class; one definition per package block, named after its
  file; `dynamic` classes and `native` outside a declaration file are rejected.
- **Imports and type names:** every import and every written type must name something visible.
- **The class hierarchy:** `extends` names a class that isn't final, `implements` names interfaces, and
  nothing inherits from itself.
- **Overrides:** `override` is present exactly when a base class has that method; final methods and fields
  can't be overridden.
- **Names in function bodies:** locals, parameters, closures, members, inherited members, package-level
  definitions and imports. Also `this` and instance members in static code, `break` and `continue` targets,
  and assignment targets.

Not checked yet (stage 1c, types): anything after a `.` on a value, the types of expressions, override
signatures, and whether a class implements all of its interfaces' methods.

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
  the name with its package. (Same as Adobe's compiler.)
- **Names are checked only when every file parses.** With a syntax error anywhere, `check` reports syntax
  errors only.
- **One function may declare a variable twice only with the same type.** Adobe's compiler warns in that case
  and this one is silent; with different types both report an error.
- **Helpers outside the package block count as part of the file's package** when deciding whether an
  `internal` class is visible. Adobe's compiler is stricter.
- **A bare call to one of `Object`'s prototype methods isn't found.** Inside a class that doesn't define
  `toString`, write `this.toString()`, not `toString()`. Stage 1c revisits this.
- **Inside a class whose base class is unknown, undefined names aren't reported,** because the missing class
  may declare them.
- **A dotted name prefers the longest package.** If package `a` defines `b` and package `a.b` defines `C`,
  then `a.b.C` is the class.

## Tests

- `tests/test_*.cpp`: unit tests for source positions, the lexer, the tree printer, and the parser.
- `tests/golden/ok/X.as` with `X.ast`: programs that must parse, and the tree they must produce.
- `tests/golden/errors/X.as` with `X.diag`: programs that must fail, and the exact diagnostics.
- `tests/corpus/*.as`: real-shaped programs that must parse with no diagnostics. The parser test also parses
  every byte-prefix of each one, to check that no malformed input can crash or hang it.

- `tests/test_names.cpp`: name resolution, one check per message, with the expected error positions marked
  in the source as `/*!*/`.
- `tests/golden/names/ok/X.as` with `X.names`, and `tests/golden/names/errors/X.as` with `X.diag`: whole
  programs through `kas3c check`.
- The corpus is also checked as one program, with `tests/stubs/godot.as` standing in for Godot.

To add a golden test, add the `.as` file, run
`python3 compiler/tests/run_golden.py compiler/build/kas3c compiler/tests --update`, and **read the generated
file** before committing it.
