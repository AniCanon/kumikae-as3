# Stage 1c-2: Type Errors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `kas3c check` reports a value that does not fit where it is used: initializers, assignments,
arguments, returns, overrides, interface implementations and constants.

**Architecture:** The types pass (stage 1c-1) already gives every expression a type. This stage adds one
question, "may a value of type A be used where B is expected?", and asks it at each place a value is
stored or passed. Class-level rules (override signatures, interfaces) run as their own pass over the
symbols, in `names_rules.cpp`.

**Tech stack:** C++17, no exceptions, no RTTI, CMake, CTest. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-11-11-kumikae-as3-design.md` ("Front end").

## Global Constraints

- Library code builds with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
- Diagnostics keep the format `<file>:<line>:<column>: error: <message>`.
- **An error is reported only where AS3's rule is confirmed.** Where it is not, the compiler stays silent
  and the README says so. A wrong rule here rejects valid programs.
- **The external corpus is the false-positive check.** After each task, `tests/run_external.py --check`
  must show no error of a kind this stage adds, unless the reported code is shown to be wrong.
- Unknown (`?`) and `*` never cause an error.
- No console vendor material. Stage files by name, never `git add -A`.

## Design decisions (rulings made while planning)

1. **The conversion rule.** A value of type A fits where B is expected when: either is `*` or unknown;
   A is B; B is `Boolean` or `Object`; both are numeric; A is `null` (silent even for numbers, where
   Adobe's compiler only warns); A is a class and B is one of its base classes or an interface it or a
   base implements; A is an interface and B is one it extends; both are `Vector` of the same element;
   A is a class object and B is `Class`. A `void` value fits only `*`.
2. **Two messages.** When B derives from A (a base class used where a subclass is expected, `Object`
   included) the message asks for a cast. Otherwise the types are unrelated.
3. **Checked places:** variable and field initializers, plain `=`, arguments of calls whose target is a
   known function, method or constructor, `super(...)`, `return`, and parameter defaults.
4. **Not checked,** because the rule is not confirmed: compound assignment, operands of operators and
   comparisons, conditions, `for each` variables, `throw`, calls through a `Function` value, and class
   names called as conversions (`int(x)`).
5. **Argument counts** are checked for the same known targets. A class with no constructor takes none.
6. **Returns:** a value returned from a `void` function, setter or constructor is an error. A function
   with a declared non-void type is reported as never returning a value only when its body has no
   `return` with a value and no `throw` at all. Paths are not analysed.
7. **Overrides** must repeat the base method's parameter types, optional parameters, rest parameter and
   return type exactly. Built-in bases stay exempt, as in stage 1b.
8. **Interfaces:** a class must have a public instance method (or accessor) for every method of every
   interface it implements, with the same signature. Inherited implementations count. Classes in
   declaration files are not checked.
9. **Constants and read-only names:** assigning (also `+=`, `++`) to a `const`, to a property with no
   setter, to a method, function or class is an error. An instance `const` may be assigned in its own
   class's constructor.
10. **Use of a definitely null value is not reported.** Adobe's rule here is not confirmed.
11. **Conversions are not recorded on the tree yet.** The IR lowering (stage 2) decides them from the
    same rule; recording them now would be untested.

## Review Focus

1. Valid AS3 rejected: any error of this stage on the external corpus.
2. A class whose base or interface failed to resolve: no cascade of type errors.
3. A method of `Vector.<T>` called with an element of the wrong type, and of an unknown type.
4. A closure inside a `void` method that returns a value: judged by the closure, not the method.
5. An override or interface implementation whose types are spelled differently but are the same type
   (`a.b.C` and `C`).

Each has a test in the task that owns the code (Tasks 1, 1, 2, 3, 4).

---

### Task 1: The conversion rule; initializers and assignments

**Files:** modify `compiler/src/names_types.cpp`; create `compiler/tests/marked.h`,
`compiler/tests/test_type_errors.cpp`.

- [ ] Tests first: one case per line of ruling 1, accepted and rejected; both messages; fields, locals,
  package variables and `=`; nothing reported for unknown types or a broken hierarchy.
- [ ] Implement `fits(from, to)` and `store(value, to, offset)` in the typer. Run. Corpus. Commit.

Messages: `can't use a value of type 'A' where 'B' is expected` and
`a value of type 'A' may not be a 'B'; use 'as' or a cast`.

### Task 2: Calls

- [ ] Tests first: too few, too many, rest, defaults; methods, package functions, nested functions,
  static methods, constructors, `super(...)`, classes with no constructor; argument types; Vector methods.
- [ ] Implement. Run. Corpus. Commit.

Messages: `'f' takes N arguments but M were given` (with `at least N`, `at most N`, `no arguments`,
`1 argument`).

### Task 3: Returns and parameter defaults

- [ ] Tests first: each line of ruling 6; closures; getters; `*` and untyped functions.
- [ ] Implement. Run. Corpus. Commit.

Messages: `a function declared 'void' can't return a value`, `a constructor can't return a value`,
`'f' is declared to return 'T' but never returns a value`.

### Task 4: Overrides and interfaces

**Files:** create `compiler/src/names_rules.cpp`; modify `compiler/src/names.h`, `names.cpp`.

- [ ] Tests first: each difference of ruling 7; each line of ruling 8.
- [ ] Implement. Run. Corpus. Commit.

Messages: `'f' must have the same parameters and return type as 'f' in Base`,
`'Main' must implement 'f' from Shape`, `'f' must be public to implement Shape`,
`'f' must have the same parameters and return type as 'f' in Shape`.

### Task 5: Constants and read-only names

- [ ] Tests first: each line of ruling 9.
- [ ] Implement. Run. Corpus. Commit.

Messages: `can't assign to the constant 'x'`, `'x' is read-only`, `can't assign to the method 'f'`,
`can't assign to the function 'f'`, `can't assign to the class 'C'`.

### Task 6: Golden tests, documents, review

- [ ] `golden/types/errors/` cases. Read every generated file.
- [ ] Sanitizer build. Update `compiler/README.md`, `README.md`, `docs/roadmap.md`, the roadmap diagram.
- [ ] Final whole-branch review by a fresh reviewer; fix Critical and Important findings test-first.
