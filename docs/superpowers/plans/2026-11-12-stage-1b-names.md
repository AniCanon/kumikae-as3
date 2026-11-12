# Stage 1b: Name Resolution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `kas3c check <file>...` reads a set of `.as` files as one program and works out what every name
refers to, reporting unknown, ambiguous, duplicate and misused names with file, line and column.

**Architecture:** A `Compilation` owns every source file (a `Unit`), parses each with the stage 1a parser,
and runs three passes: declare (packages, definitions, members), link (imports, type names, class hierarchy,
override rules), and bodies (scopes inside functions). Results are written onto the syntax tree (`symbol`
fields), which is what stage 1c (types) and the IR lowering read.

**Tech stack:** C++17, no exceptions, no RTTI, CMake, CTest, Python 3 for golden tests. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-11-11-kumikae-as3-design.md` (sections "Front end", "Godot API
bindings", "Language"), amended by `2026-11-11-performance-redesign.md`.

**Out of scope, left to stage 1c (types):** the type of any expression; member access through `.` on a
value; override signature compatibility; interface implementation completeness; assignment to `const`;
number literal validity (`09`); the member lists of built-in classes other than `Object` and the `Error`
family.

## Global Constraints

- Library code builds with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
- Diagnostics keep the stage 1a format: `<file>:<line>:<column>: error: <message>`.
- Exit status of `kas3c`: 0 no errors, 1 errors found, 2 bad usage or an unreadable file.
- No syntax tree is taller than about 512 levels (stage 1a), so passes may recurse over it. Loops over
  chains (member chains, class hierarchies) must still be iterative or cycle-safe.
- Deviations from AS3 are allowed only where the spec lists them. Where this plan accepts more or less than
  Adobe's compiler, the README's "Limits" section says so.
- No console vendor material. Stage files by name (`git add compiler docs`), never `git add -A`.

## Design decisions (rulings made while planning)

1. **Declarations without bodies are AS3 source.** A *declaration file* is an ordinary `.as` file whose
   classes and functions carry `native` and have no bodies. The built-in library is a set of declaration
   files in `compiler/builtins/`, embedded into the library at build time. Stage 2's generated `godot`
   package uses the same mechanism. `kas3c check --declarations <file>` adds one from disk. In a declaration
   file the one-definition-per-file and file-name rules don't apply. In any other file, `native` is an
   error.
2. **Package-level lookup follows AS3's open-namespace rule.** A simple name is looked up, all at once, in:
   the file's own helpers, the current package, the top-level package, every explicit import with that last
   part, and every wildcard import. Two different matches are an ambiguity error; nothing shadows anything
   at this level. Cost: `import godot.Object` beside the built-in `Object` is ambiguous. Stage 2 decides
   Godot's naming with that in mind.
3. **Helpers outside the package block count as members of the file's package** for `internal` visibility.
   Accepts slightly more than Adobe's compiler.
4. **Names are checked only when every file parses.** If any file has a syntax error, `check` reports syntax
   errors only.
5. **Two `var` declarations of one name in one function are allowed when their types are the same** (Adobe
   warns); with different types it's an error.
6. **Inside a class whose base class failed to resolve, undefined names aren't reported,** because the
   missing base may declare them.
7. **Results live on the tree.** `Name::symbol`, `Member::symbol` (when a dotted chain names a definition by
   its package), `TypeRef::symbol`, and `symbol` on each declaration, declarator, parameter and catch clause.

## Review Focus

1. A project checked one file at a time: every import fails. Expect one clear error per import, no cascade.
2. A class cycle (`A extends B`, `B extends A`) or interface cycle: one error, no hang or crash.
3. A name that is both a local and a class or package name: the innermost wins, silently.
4. A closure inside an instance method inside a static-looking context, and the reverse: `this` and instance
   members are judged by the enclosing method, not the closure.
5. A dotted chain 448 links long whose root is unknown: one error, no deep recursion.

Each has a test in the task that owns the code (Tasks 3, 3, 5, 5, 5).

---

### Task 1: Program model, built-ins and `kas3c check`

**Files:** Create `compiler/src/names.h`, `compiler/src/names.cpp`, `compiler/src/builtins.h`,
`compiler/builtins/toplevel.as`, `compiler/builtins/flash_utils.as`, `compiler/tests/test_names.cpp`.
Modify `compiler/CMakeLists.txt` (embed built-ins), `compiler/src/ast.h` (symbol fields),
`compiler/tools/kas3c.cpp`, `compiler/tests/test_cli.py`.

**Produces:**

```cpp
struct Unit { Source source; Arena arena; Diagnostics diagnostics; Program *tree; bool is_declaration;
              bool is_builtin; /* plus per-file tables */ };
class Compilation {
public:
	Compilation();                                   // loads the embedded built-ins
	Unit *add(std::string p_path, std::string p_text, bool p_is_declaration = false);  // parses
	void check();                                    // runs the passes; a no-op if any file failed to parse
	bool has_errors() const;                         // any unit, built-ins included
	std::string format_diagnostics() const;          // units in the order added
	std::string dump_names() const;                  // Task 6
};
```

- [ ] Tests first (`test_names.cpp`): an empty class checks clean; the built-ins themselves have no
  diagnostics; a syntax error in one of two files is reported and nothing else is.
- [ ] `test_cli.py`: `kas3c check` on a clean file exits 0; on a file with a syntax error exits 1; with no
  files exits 2; `--declarations` with a missing file exits 2.
- [ ] Implement, run, commit.

### Task 2: Declare pass

Collect each unit's package, its definitions (in the block and outside it), and each class's and
interface's members.

- [ ] Tests first, one per message:
  - `'Foo' is already defined at <where>` for two definitions of one name in one package, in one file and
    across files. `<where>` is `line N`, `<path>:N:C`, or `the built-in library`.
  - `a file can define only one class, interface, function or variable inside its package block`
  - `'Foo' must be defined in a file named Foo.as`
  - `dynamic classes are not supported`
  - `'native' is only allowed in declaration files` (class, function, method)
  - Members: `'x' is already defined at line N` for two fields, field and method, two getters, static and
    instance of one name. A getter and a setter of one name form one property and are not an error.
  - A method named after its class is the constructor; two of them are a duplicate.
- [ ] Implement, run, commit.

### Task 3: Imports, type names and the class hierarchy

- [ ] Tests first:
  - Imports: `can't find 'a.b.C'`, `can't find package 'a.b'`, `'a.b.C' is not public`. A project checked
    one file at a time gives exactly one error per import.
  - Type names, wherever a type is written (fields, parameters, return types, locals, `extends`,
    `implements`, catch clauses, `new <T>[]`, `Vector.<T>`): `unknown type 'X'`,
    `'X' is ambiguous: it could be a.X or b.X`, `'x' is not a type`, `only Vector takes a type parameter`.
  - Visibility: an `internal` class is found from its own package and from its file's helpers, and is
    unknown from another package. A file's helpers are unknown from another file.
  - Hierarchy: `a class can't extend the interface 'I'; use 'implements'`, `can't extend 'X': it is final`,
    `'X' is not an interface` (for `implements` and for an interface's `extends`),
    `'A' inherits from itself` for cycles of length 1, 2 and 3 in classes and in interfaces: exactly one
    error per cycle, and no hang. A class with no `extends` has base `Object`.
  - A class can extend a class from a `--declarations` file (a stand-in `godot.Node`).
- [ ] Implement, run, commit.

### Task 4: Override and inheritance rules

Runs after the hierarchy is known. Skipped for a class whose hierarchy has an error.

- [ ] Tests first:
  - `'x' overrides a method of Base and must be marked 'override'`
  - `'x' is marked 'override' but no base class has a method to override`
  - `can't override 'x': it is final in Base`
  - `can't override 'x': it is not a method in Base` (a field; a getter overridden by a plain method and
    the reverse)
  - `'x' conflicts with 'x' inherited from Base` (a field redeclaring an inherited member)
  - A private member of the base is invisible: redeclaring it is fine, and overriding it is "no base class
    has a method". Static members neither override nor conflict.
  - Overriding through two levels, and overriding a method of a declaration-file class.
- [ ] Implement, run, commit.

### Task 5: Function bodies

Resolve every `Name` in every function body, field initializer and parameter default.

Lookup order: the function's scopes from the inside out (catch variable, locals and parameters, the
function's own name for a named function expression, `arguments`), then each enclosing function, then the
class and its bases (skipping private members of bases, and internal ones from another package), then the
package level (ruling 2). `var` and nested function declarations are hoisted to their function.

- [ ] Tests first:
  - Resolution: local, parameter, rest parameter, hoisted `var` used before its declaration, `var` inside a
    nested block, `for` and `for each` variables, catch variable (and that it is unknown after the catch
    block), nested function declared after use, closure reading an outer local, named function expression
    calling itself, `arguments`, field, inherited field, static field of a base, method, package function,
    imported function, built-in (`trace`, `Math`, `NaN`).
  - Shadowing: a local over a field over a package name: the innermost wins, no error.
  - `undefined name 'x'`; not reported inside a class whose base failed to resolve (ruling 6).
  - `'x' is ambiguous: it could be a.x or b.x`
  - `'x' is private in Base`
  - `can't use the instance member 'x' in a static context`: static method, static field initializer, and a
    closure inside a static method. Not reported for a closure inside an instance method.
  - `'this' can't be used in a static context`; `'super' can only be used in an instance method`.
  - `duplicate parameter 'x'`; `'x' is already defined in this function with a different type (line N)`;
    the same type twice is fine.
  - Dotted names: `flash.utils.getTimer()` and `new a.b.C()` resolve to the definition; a 448-link chain
    with an unknown root gives one `undefined name` error.
  - Labels: `'break' is only allowed in a loop or switch`, `'continue' is only allowed in a loop`,
    `undefined label 'x'`, `'x' does not label a loop`. A label is not visible inside a nested function.
  - Assignment targets: `can't assign to this expression` for `1 = x`, `f() = x`, `a + b = c`, `x++` on a
    call, and a `for in` target that is a call. A name, a member and an index are accepted, also in
    parentheses.
  - Parser carry-over: a trailing comma in call arguments (`f(a,)`) is a syntax error.
  - A placeholder `Name` with empty text is skipped silently.
- [ ] Implement, run, commit.

### Task 6: Dump, golden tests, corpus, documents

- [ ] `kas3c check --dump-names`: one line per resolved reference, in source order, per user file:
  `<line>:<col> <name> -> <kind> <qualified name> <where>`, where `<where>` is `@<line>:<col>` in the same
  file, `@<path>:<line>:<col>` in another, or `(built in)`.
- [ ] `run_golden.py`: `golden/names/ok/X.as` with `X.names`; `golden/names/errors/X.as` with `X.diag`.
  Read every generated file before committing it.
- [ ] Corpus: all 14 files in `tests/corpus/` check clean together with a hand-written stand-in for the
  Godot classes they use, `tests/stubs/godot.as`, passed with `--declarations`. Each stub member is one the
  corpus needs.
- [ ] Sanitizer build passes. Every byte-prefix of each corpus file checks without crashing.
- [ ] Update `compiler/README.md` (the `check` command, the rulings above under "Limits"), `README.md`
  and `docs/roadmap.md` statuses, and the roadmap diagram.
- [ ] Final whole-branch review by a fresh reviewer; fix Critical and Important findings test-first.
