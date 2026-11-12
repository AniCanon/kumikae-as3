# Kumikae AS3 compiler

The front end of the ActionScript 3 compiler for Godot: source text to a checked syntax tree. This stage
contains the lexer and parser. Name resolution and type checking come next.

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

## Tests

- `tests/test_*.cpp`: unit tests for source positions, the lexer, the tree printer, and the parser.
- `tests/golden/ok/X.as` with `X.ast`: programs that must parse, and the tree they must produce.
- `tests/golden/errors/X.as` with `X.diag`: programs that must fail, and the exact diagnostics.
- `tests/corpus/*.as`: real-shaped programs that must parse with no diagnostics. The parser test also parses
  every byte-prefix of each one, to check that no malformed input can crash or hang it.

To add a golden test, add the `.as` file, run
`python3 compiler/tests/run_golden.py compiler/build/kas3c compiler/tests --update`, and **read the generated
file** before committing it.
