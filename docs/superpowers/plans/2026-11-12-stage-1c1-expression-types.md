# Stage 1c, part 1: Expression Types Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** every expression has a static type, and `a.b` resolves to a member, with AS3's access rules.

**Architecture:** a fourth pass, `types`, runs after the bodies pass. It walks every function body, field
initializer and default value, and writes `Expr::type` and `Member::member`. Types are small interned
objects owned by the `Compilation`. The built-in classes get their full member lists as declaration files.

**Spec:** `docs/superpowers/specs/2026-11-11-kumikae-as3-design.md` ("Front end", "Typed IR", "Language").

**Out of scope, left to part 2 (1c-2):** whether a value fits where it is used (assignments, arguments,
returns, argument counts); recording implicit conversions; override signatures; interface completeness;
assignment to `const`; definite null misuse; number literal validity (`09`). **Left to part 3 (1c-3):**
arithmetic on Godot's math types.

## Global Constraints

Those of the stage 1b plan, unchanged: no exceptions or RTTI, `-Werror`, the diagnostic format, exit codes,
iterative walks over anything the parser builds in a loop, stage by name.

## Design decisions (rulings made while planning)

1. **Unknown is not `*`.** A failed lookup gives the internal type *unknown*, printed `?`. It never causes a
   further error, and part 2 must not treat it as dynamic typing.
2. **The resolved member lives in `Member::member`,** a new field. `Member::symbol` keeps its meaning (a
   definition named by its package).
3. **`dynamic` is allowed in declaration files.** An unknown member on exactly a dynamic class (`Object`,
   `Array`, `Date`, `RegExp`, `Error`, `Dictionary`, `Vector`) has type `*`. `dynamic` is not inherited.
4. **`Vector`'s element type is written `T`** in the built-in file and nowhere else.
5. **"has no member" is reported only on a sealed class whose whole hierarchy is known.**
6. **Arithmetic on two numbers has type `Number`,** as in AS3: `int + int` is computed without wrapping and
   wraps only when stored to an `int`. Bitwise operators give `int`, `>>>` gives `uint`. `+` with a `String`
   gives `String`. Arithmetic involving a class type gives *unknown*, with no error, until part 3.
7. **A built-in member may be redefined with or without `override`** (amended after review). Which one AS3
   wants depends on whether the member is in the `AS3` namespace, on the prototype, or public, which this
   compiler does not model. Cost: accepts redefinitions that Adobe's compiler rejects.
8. **`protected` is reachable through any instance** from code in the declaring class or a subclass. Adobe's
   reference doesn't say; this is the behaviour AS3 programmers report. Cost if wrong: accepts more.
9. **Statics are not inherited:** `Sub.staticOfBase` is an error (Adobe's reference, "Static class members
   are not inherited").
10. **`this` inside a nested function has type `*`,** because AS3 binds it at call time.

## Types

`*`, `void`, `null`, *unknown*, a class or interface instance, `Vector.<T>`, and the class object itself
(the type of the expression `Math` or `Player`, printed `Class.<Player>`).

`kas3c check --dump-types` prints, for each expression statement and each initializer, in source order:
`<line>:<col> <type>`.

## Review Focus

1. A member chain 447 links long on an unknown root: no error, no deep recursion.
2. A member that exists but is private, protected or internal: one clear error naming the owner.
3. A class extending a declaration-file class with a partial member list (the Godot stand-in): members that
   exist resolve; missing ones are errors, since the hierarchy is known.
4. `Vector.<Vector.<int>>` indexing and methods: element types flow through two levels.
5. A name whose resolution failed in the names pass: no second error from this pass.

---

### Task 1: Built-in member lists

- [ ] Allow `dynamic` in declaration files; `T` inside the built-in `Vector`; exempt built-in members from
  the override rules (test: an `Array` subclass defining `push` without `override` is clean).
- [ ] Declare the members of `Object`, `String`, `Array`, `Vector`, `Math`, `Number`, `int`, `uint`,
  `Boolean`, `Date`, `RegExp`, `JSON`, `Function`, `Error`, `Dictionary` from Adobe's reference. Remove
  `[Incomplete]`.

### Task 2: The type pass

- [ ] Tests first, asserting on `--dump-types` lines: literals (including `int`/`uint`/`Number` by value),
  names of every symbol kind, `this` (method, closure, static), operators (ruling 6, comparisons, logical,
  conditional, `as`, `is`, `typeof`, assignment), calls (methods, functions, casts, `super`), `new`,
  array/object/vector literals, indexing, member access on instances, class objects, interfaces, dynamic
  classes, `Vector` two levels deep, `Function` values.
- [ ] Errors, one test each: `'Foo' has no member 'x'`, `'Foo' has no static member 'x'`,
  `'x' is private in Foo`, `'x' is protected in Foo`, `'x' in Foo is not visible from this package`,
  `'x' is a static member of Foo; write Foo.x`, `'x' is an instance member of Foo; it needs an object`.
- [ ] Implement, run, commit.

### Task 3: Golden tests, corpus, documents

- [ ] `golden/types/ok/X.as` with `X.types`; `golden/types/errors/X.as` with `X.diag`.
- [ ] The corpus checks clean; fill `tests/stubs/godot.as` with the members it names.
- [ ] Sanitizer build; README, roadmap and diagram (show the 1c split).
- [ ] Final whole-branch review by a fresh reviewer; fix Critical and Important findings test-first.
