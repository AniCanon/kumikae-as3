# Stage 1e: Godot's Math Types Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** arithmetic, comparison and indexing on Godot's value types have types and are checked:
`position + velocity * delta` is a `Vector2`, and `position + node` is an error.

**Architecture:** Godot's API description lists every operator of every built-in type. The generator writes
them into `godot.as` as `[Operator(...)]` tags on the class, and what indexing gives as an `[Indexed(...)]`
tag. The types pass reads the tags of declaration classes and looks an operator up by its operand types.
The compiler holds no list of Godot types.

**Tech stack:** Python 3 (generator), C++17 (types pass), CMake, CTest.

**Spec:** `docs/superpowers/specs/2026-11-11-kumikae-as3-design.md` (value types, "operators work on
them") and `docs/superpowers/specs/2026-11-12-godot-naming-design.md` section 6.

## Global Constraints

- Library code builds with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
- No new syntax: a tag is metadata, which AS3 already has.
- An error is reported only where the rule is certain. Unknown (`?`) and `*` never cause an error.
- The external corpus must show no error of a kind this stage adds.
- No console vendor material. Stage files by name, never `git add -A`.

## Design decisions (rulings made while planning)

1. **Which operators.** `+ - * / %`, unary `-` and `+`, and `< > <= >=`. Godot's `==` and `!=` exist for
   every type and are not recorded. Godot's `**`, `and`, `or`, `xor`, `not` and `in` are not AS3 operators
   on these types. Bitwise operators exist only on `int`.
2. **How they are written.** On the class of the left operand:
   `[Operator(op="*", right="Number", returns="Vector2")]`; a unary one has no `right`. Where the left
   operand is a number (`2 * v`), on the class of the right operand: `[Operator(op="*", left="Number",
   returns="Vector2")]`. A Godot `int` is written `int` and a `float` is written `Number`.
3. **Which number.** An `int` or `uint` operand looks for the `int` form, a `Number` operand for the
   `Number` form. So `Vector2i * 2` is a `Vector2i` and `Vector2i * 0.5` is a `Vector2`, as in Godot.
   **Changed while building:** an operand is whole if it is an `int` or `uint` or a sum, difference,
   product or remainder of whole operands, so `cell * (i + 1)` is a `Vector2i` although AS3 types `i + 1`
   as a `Number`. A whole operand may use a `Number` form when there is no `int` form; the reverse is an
   error (`Vector2i % 0.5`), as in Godot. Cost: `Math.floor(x)` is a `Number`, so
   `cell * Math.floor(x)` is a `Vector2`; write `int(Math.floor(x))`.
4. **When it is an error.** One operand's class has `[Operator]` tags, both operand types are known and
   neither is `*`, `Object` or `null`, and no tag matches: `can't use '+' on 'Vector2' and 'Node'`.
   **Changed after review:** every built-in type carries an `[Operators]` tag saying its list is complete,
   so a type with no operators at all (`GodotDictionary`, `Signal`) is checked too. A class with no such
   tag (a script's own class, `Node`) is left as before: the type is unknown and nothing is said.
5. **`+` with a `String`** stays a `String`: AS3's rule comes first.
6. **Compound assignment** (`position += step`) uses the same lookup. **Changed while building:** its type
   is the target's, and a result that does not fit the target is an error (`cell *= 0.5` gives a
   `Vector2`). Value types don't convert to each other, so the error is certain.
7. **Indexing.** `[Indexed(element="Vector2")]` on a class: a number key gives the element type. A
   `[Indexed(element="*", keyed="true")]` class gives the element for any key. Any other key gives `*`,
   because AS3 lets `v["x"]` name a property. Storing into `packed[i]` is checked like any store.
8. **Equality** needs no type rule: `a == b` is already a `Boolean`. That it compares values, not
   identities, is the interpreter's business.

## Review Focus

1. A valid expression rejected: number widths (`uint`, literals), `*` or unknown operands, operands that
   are subclasses, a script class in a package named `godot`.
2. `2 * v` against `v * 2`, and operators that are not symmetric (`Transform2D * Vector2`).
3. Compound assignments and unary minus on value types.
4. A tag that names a type the program doesn't have (`--no-godot` with a user's own declarations).
5. Cost: the tags are read once, not on every expression.

---

### Task 1: Operators and indexing in the declarations

**Files:** `compiler/godot/generate.py`, `compiler/godot/test_generate.py`, `compiler/godot/fixture_api.json`,
`compiler/godot/fixture.as`, `compiler/godot/godot.as`.

- [ ] Tests first (fixture): `Vector2` gets `[Operator(op="+", right="Vector2", returns="Vector2")]`,
  `[Operator(op="-", returns="Vector2")]`, `[Operator(op="*", left="Number", returns="Vector2")]` from the
  `float` entry, `[Indexed(element="Number")]`; `GodotDictionary` gets
  `[Indexed(element="*", keyed="true")]`; `==`, `in` and `**` are not written; an operator with an
  undeclared operand type is skipped; an unknown operator name is refused.
- [ ] Implement; regenerate both outputs; real-API test: counts of tags, and the file still checks clean.
- [ ] Commit.

### Task 2: Operators in the types pass

**Files:** `compiler/src/names_types.cpp`, `compiler/tests/test_types.cpp` (types),
`compiler/tests/test_type_errors.cpp` (errors).

**Consumes:** the tags of Task 1. **Produces:** types for binary, unary and compound-assignment
expressions on tagged classes; the error of ruling 4.

- [ ] Tests first, against Godot's real declarations: the type of `a + b`, `v * 2`, `2 * v`, `v * 0.5`,
  `vi * 2`, `vi * 0.5`, `-v`, `t * v`, `v * t`, `c * 2`, `a < b`, `v += w`, `position + velocity * delta`;
  stores that now check (`var p:Vector2 = a + b`, `var n:Number = a + b` is an error); errors for
  `v + node`, `v % w`, `c < d` (Color has none), `-rect`; silence for `v + any`, `v + unknown`, `rid + 1`,
  a script class `+`; `v + "s"` is a `String`.
- [ ] Implement: one table per types pass, built on first use from the tags; lookup by operator and
  operand types.
- [ ] External corpus unchanged; commit.

### Task 3: Indexing

**Files:** `compiler/src/names_types.cpp`, tests as in Task 2.

- [ ] Tests first: `packed[0]` is a `Vector2`; `v[0]` is a `Number`; `array[0]` and `dict["k"]` are `*`;
  `transform[0]` is a `Vector2`; `packed[0] = "s"` is an error; `v["x"]` is `*`; `rect[0]` is `*`.
- [ ] Implement; commit.

### Task 4: Documents

- [ ] `compiler/README.md` (how expressions are typed, Godot's API), `README.md` status row,
  `docs/roadmap.md`, `docs/diagrams/build.py`; commit; push.
