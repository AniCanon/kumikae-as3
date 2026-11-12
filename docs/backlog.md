# Backlog: deferred items

Every review leaves some findings that were judged minor and put off. They are not forgotten: this page
lists all of them, by stage, and the owner has said they are to be fixed (2026-11-12). An item leaves this
page only when it is fixed, with a test, or when the owner decides it stands.

The [roadmap](roadmap.md) says what is being built now. This page says what is owed.

## How this page is kept

- When a stage's review defers a finding, it is added here in the same branch.
- When an item is fixed, its line is removed and the commit says so.
- An item marked **decision** needs the owner's answer, not code.

## Decided by the owner, to build

Decided on 2026-11-12. Each is owed as code and tests.

| Decision | What to build | When |
|---|---|---|
| **64-bit integers are supported,** in whatever way is easiest to maintain in the long term | A real 64-bit integer type, `Int64`, with one rule in the generator: a Godot integer that Godot's description marks as 64-bit is an `Int64`. The hand-kept table of 31 exceptions goes away. An `Int64` converts to `int`, `uint` or `Number` where one is expected, with a check that raises a `RangeError` if the value does not fit, so nothing is ever cut off silently. It needs its own short design first. | Stage 2b-3 |
| **Static fields get their values on a class's first use,** as in Flash | Replace "before the program starts, in file order" with a first-use check; the compiler removes the check where it can prove the order. | The clean-up pass after stage 2b-1 |
| **A call on a null object evaluates its arguments first,** then raises the error, as in Flash | Move the null check after the arguments in the lowering. | The clean-up pass after stage 2b-1 |

## Stage 1d: Godot's API in AS3

- A script class named like a Godot class is ambiguous in the default package.
- Acronyms come out lower-case: `getAabb`, not `getAABB`.
- `print()` needs at least one argument.
- Four properties lose their element type.
- One wide flag enum of `RenderingServer` is declared `int`.
- The corpus prefix test loads Godot's API for one prefix in 293 only.
- Loading Godot's API costs about 30 ms on every `kas3c check`.
- The merged constructors accept calls no overload has: `new Vector3(1, 2)`.
- A singleton can't be passed as an object.
- 60 methods that take raw pointers are left out.

## Stage 1e: Godot's math types

- `Math.floor`, `Math.round`, `Math.abs`, `Math.max`, `parseInt` and `cond ? int : uint` count as fractions,
  so `cell * Math.floor(x)` is a `Vector2`, not a `Vector2i`. The error does not suggest `int(...)`.
- `v++`, `~v` and `v <<= 1` on a value type are not reported.
- A malformed tag in a declaration file (both `left` and `right`, two rules for one operator) is resolved
  silently.
- `for each` over a packed array does not check the variable's type.
- Godot's string-format operator (`"%s" % value`) is not available.

## Language server, first slice

- **Rider is not yet tried.** The LSP4IJ plugin is installed; adding the server in its settings is not.
- **VS Code has not been looked at by eye,** only through a log of its messages.
- **Windows is not supported:** text-mode reads and writes, and `/` assumed in paths.
- The same file reached through a symbolic link, or under a different letter case, or in a different
  Unicode form, counts as two files.
- A project over 20,000 files is cut off with a note on standard error only, not in the editor.
- A message with 200,000 keys blocks the server for 26 seconds (`Json::set` is linear).
- A `Content-Length` header with a trailing space ends the server silently.
- Symbol search lists a get/set property twice.
- Find references does not list overrides of a method.
- Symbol search covers the project only, not Godot's classes.
- `super(...)` goes nowhere on go to definition; `new X()` goes to the class, not the constructor.
- While any file has a syntax error, type errors are hidden everywhere. Silencing only the broken region
  would be kinder.
- A changed line is sent as a whole document; ranged changes are ignored.

## Stage 2a: interpreter core

- The debugger hook can't read a variable's value yet.
- A `while` condition has one statement marker, so a stop on that line happens once.
- Objects that refer to each other in a loop are never freed (the cycle collector is planned for 2d).
- `NaN`, `Infinity` and `int.MAX_VALUE` are refused with an unhelpful message.
- A string of 4 GiB or more is truncated; running out of memory while building a string aborts.
- No existing corpus program runs yet: each stops in the checker or is refused.
- There is one IR golden file; the plan called for more.
- A string's `length` counts characters, not UTF-16 units, and strings compare by character.
- An endless loop has no stop.
- After one refused construct, follow-on errors can appear on the same line.

## Stage 2b-1: AS3 inside Godot, first slice

- **The first editor scan of a project with the extension crashes as Godot exits,** after the scan has done
  its work. The speed spike's extension does the same. Cause unknown.
- A local of a Godot value type with no initializer is null, not a zero value.
- A script attached to a reference-counted Godot object would keep it alive for ever.
- godot-cpp is built as `template_release` and loaded by the editor binary; whether editor features need a
  debug build is unchecked.
- GDScript cannot set a script's field, only read it.

More will be added here when this slice's review reports.
