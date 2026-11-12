# Kumikae AS3: additions to the language

Design, 2026-11-12. Status: **parked. A list of candidates, not a plan.**

## Decision

The owner decided on 2026-11-12: **no new keywords or syntax yet. First get things working.** Until a
script runs in Godot (stage 2), the language is AS3 as Adobe defined it, and every valid AS3 program is
accepted.

**What is needed to make things work is allowed** (the owner, same day): metadata such as `[Export]` and
`[Signal]`, and anything else a running script can't do without. What is parked is the optional kind.

ActionScript 3 (2006) is the last version of the language. Adobe cancelled its successor and ECMAScript 4
was abandoned, so any later version would be this project's to define. This page keeps the candidates in
one place so that they are decided together, not one at a time as they come up.

## What an addition costs

| Kind | Example | Cost |
|---|---|---|
| A new check | Reporting a handler whose parameters don't match the signal | None for valid programs. |
| Metadata or a library type | `[Export]`, `[Signal]`, `Signal` | None: metadata is already AS3 syntax. |
| New meaning for valid syntax | `position.x = 5` writes the vector back; `a + b` on `Vector2` | Other AS3 tools accept the file but don't know the meaning. |
| New syntax or a keyword | `await`, `enum`, `String?` | The file is no longer AS3. JetBrains' own ActionScript support flags it until this project's plugin replaces it. |

The first three kinds are allowed now. The fourth is what the decision parks.

## Already in the design

- `position.x = 5` on a Godot property (naming design, section 6).
- Arithmetic on Godot's math types (stage 1e).
- `[Export]` and `[Signal]`, and signals as properties.
- AS3's collections convert when they are handed to the engine.

## Candidates

| Candidate | Why | Needs new syntax? |
|---|---|---|
| Waiting for a signal inside a function, as GDScript's `await` | Sequences without callback chains. Already listed as "later" in the roadmap. | Yes, or a library form built on closures. |
| A 64-bit integer type | Godot integers are 64-bit, and object ids don't fit a `Number`. Stage 2 must settle this anyway. | No: a library type would do. |
| Real enums | A Godot enum is an `int` today, so a wrong constant is not reported. | No, if done as a check using the enum recorded in the declarations. Yes, for scripts to declare their own. |
| Types that can't be `null` | Null errors found before running. | Yes. |
| Typed function types | A signal handler's parameters checked against the signal. | No, as a check on `connect`. Yes, as a written type. |
| `getNode` without the `as` cast | `getNode("Hitbox") as Area2D` is the most repeated line in a script. | No: metadata on a field, as SwiftGodot's `@Node`. |
| Abstract classes and private constructors | Apache Royale's AS3 compiler offers both. | Yes. |

## When to return to this

After stage 2, when scripts run and real use shows which of these hurts most. The candidates that need no
new syntax can be proposed earlier, each as its own small design.
