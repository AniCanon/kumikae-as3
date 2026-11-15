# Backlog: deferred items

Findings that earlier reviews put off. Since 2026-11-15 nothing is added here: the owner's rule is that
what is found is fixed at once, by anyone and any agent. This page only shrinks: an item leaves it when it
is fixed, with a test, or when the owner decides it stands.

The [roadmap](roadmap.md) says what is being built now. This page says what is owed, and when.

- **Stage 6, Simulator:** unblock and verify the [JetBrains build](../editors/jetbrains/), choose whether to adopt idb's private input interfaces, and check the panel, input and Godot report end to end; see the [results](results/2026-11-jetbrains-simulator.md).

## How this page is kept

- Nothing new is added (the owner's rule, 2026-11-15).
- When an item is fixed, its line is removed and the commit says so.
- An item marked **decision** needs the owner's answer, not code.

## How it is worked off

All of it now, on one branch, whatever the size of the item: nothing waits for a later stage (the owner,
2026-11-15). The mark after each item says only what part of the work it belongs to. **decision** items
need the owner's answer, not code.

## Decided by the owner, to build

Decided on 2026-11-12. Each is owed as code and tests. (Two more decisions of that day are built: static
fields are set on a class's first use, and a call on a null object evaluates its arguments first.)

| Decision | What to build | When |
|---|---|---|
| **64-bit integers are supported,** in whatever way is easiest to maintain in the long term | Built in stage 2b-3, part B: the `Int64` type, by one rule in the generator (approved 2026-11-14). See the design (`docs/superpowers/specs/2026-11-14-int64-design.md`). | Done |
| **A value read through `*` converts as AS3 converts** (2026-11-15): into `int` a non-number is 0 and a large integer wraps, with no error; into a class or one of Godot's types the type is checked, a `TypeError` if it is wrong. A typed read straight from Godot stays strict. | Built in stage 2c-2. | Done |

## Stage 1d: Godot's API in AS3

- A script class named like a Godot class is ambiguous in the default package. **clean-up**
- **decision** 60 methods that take raw pointers are left out. AS3 has nothing to pass them, so the
  proposal is that this stands.

## Stage 1e: Godot's math types

- Godot's string-format operator (`"%s" % value`) is not available. **2c**

## Language server, first slice

- **Manual Rider acceptance remains pending.** The native plugin passes sandbox completion and typing
  checks; a person still needs to accept the full editing and navigation workflow. **LS-2**

## Stage 2b-2, part A: Godot's values

- **decision** `"" + this` in a script without `toString()` reads `[object Values]` (AS3's), while Godot's
  text for the same node reads `<Node2D#123>`. And a `toString()` that raises an error ends the caller
  through `"" + this`, but through Godot's text it is reported and Godot's text stands.

## Stage 2b-2, part B: signals

- A signal's own `numListeners` and `emit` can't follow `object[name]` as a property read: only its
  methods can (`timer[name].add(f)`). **2c**
- A script's own signals wait for `[Signal]`. **2d**
- Objects that hold each other in a loop are freed only when a listener is part of the loop, or reaches
  it. A loop of script objects with no listener in it stays (the cycle collector). So does a loop
  that passes through a Godot collection. **2d**
- The look for such loops walks everything the listeners without a Godot object of their own can reach.
  It is paced by its own cost (at most four times a second, about a fiftieth of the time), but one look
  over 100,000 objects takes about 30 ms in one go. The cycle collector should work in steps. **2d**
- When GDScript holds a copy of a listener's callable, the look moves that listener behind listeners added
  after it. **2d**
- A loop whose own field holds its listener's callable (in a Godot collection), or whose one callable is
  in two signals, is never freed. **2d**
- `other[name].add(f)` on another script object reaches only the Godot object's signals, not a signal the
  other script keeps in a public field. `this[name]` finds a subclass's field before a base class's
  private one of the same name. **2c**
- When the scene tree is taken down, static fields are emptied and listeners stop working, so that Godot
  objects are let go while they can still be freed. A script that runs after that sees null statics. If
  the main loop is not a scene tree, an object held by a static field with a GDScript listener on it
  still aborts Godot at exit. **2d**

## Stage 2b-3, part A: singletons, constants, static methods

- Godot calling a script from another thread (a method given to `WorkerThreadPool.addTask`) is refused
  with an error. Running AS3 off the main thread waits for the `async` research. Only method calls are
  guarded: a property read or a notification from another thread is not. **2d**
- `removeAll()` on a singleton's signal removes every script's listeners, not only the caller's.
  **decision**

## Stage 2b-3, part B: `Int64`

- **decision** Godot cuts a wide integer off, without an error, inside more of its built-in types than
  the integer vectors and `PackedInt32Array`, which are guarded. The description gives these no width, so
  they are `Int64`, and a script gets what GDScript gets:
  `bytes.append(300)` stores 44; `items.insert(4294967297, 99)` inserts at 1; `Color.hex` keeps the low 32
  bits (it was a `uint` before); `PackedByteArray.encodeU32`, `encodeS32`, `encodeU16` and `encodeU8` cut
  their value; `Array`'s `removeAt`, `popAt`, `slice` and `find` cut an index; `Basis.getEuler` and
  `Quaternion.getEuler` cut the order; `Callable.unbind` its count. Guarding each needs a list kept by
  hand, which the owner chose against; the choice is whether a short one is worth it.
- `x.toString(radix)` on an `Int64` waits for the standard library. **2c**
- `Vector.<Int64>` and an `Int64` in an `Array` wait for those types. **2c**
- Every program's IR now carries the `RangeError` class, used or not. **clean-up**

## C++ clean-up, November 2026

The limits of [the C++ guidelines](cpp-guidelines.md) hold and are checked. These are its aims, and what
the review of the clean-up put off.

- 38 functions are over the 60-line aim (none over 120 but the three dispatch switches). The longest:
  `Lexer::lex_punctuation` 120, the resolver's `statement` 109, `Parser::parse_primary` 99. **clean-up**
- Ten files are over the 500-line aim (`names_bodies.cpp` 714, `parser_expr.cpp` 673, `lsp/server.cpp`
  671, `godot_host.cpp` 647) and three headers over 300 (`ast.h`, `lowerer.h`, `names.h`). **clean-up**
- Planned and not done: the listener sweep in a file of its own, `godot_values.cpp`, a table of handlers
  in the language server. **clean-up**
- The interpreter makes a new handle for every value Godot gives back, which is most of what a call into
  Godot now costs. A design change, for the native backend's stage. **clean-up**

## Stage 2b-3, part C: global functions and errors

- Godot's message for a listener that was not called names it `Node(Signals.as)::`, without the method.
  **clean-up**
- A freed Godot object still reads as `<Freed Object>` as text, as in GDScript, though it is `null` in
  every test. **decision**

From the review of part C (its five important findings were fixed with it: `print()` with no arguments, a
default `[]` shared between calls, a null answer taken silently as text, `-5` reaching Godot as `-5.0`, and
any Godot value passing as a condition):

- A freed object's text differs by path: `"" + x` gives `<Freed Object>`, `str(x)` and `print(x)` give
  `<null>`; `hash` and `isSame` of freed objects differ from GDScript's. Settled with the decision above.
  **decision**
- A freed object arriving from Godot for a `Node` parameter of a script's method is refused, not null.
  **2d**
- A Number variable that holds a whole value reaches a parameter that takes anything as a float
  (`str(x)` gives `2.0`, as GDScript does for a float). Only expressions that are whole by their form are
  sent as integers. **decision**
- New code that the guide would write otherwise: an empty string and an out-parameter where it asks for
  `std::optional`, `const std::string &` for `std::string_view`, two comments out of place. **clean-up**

## Reload while running

- A save that changes code that runs once starts the scene again, but only the scene that is running: an
  object kept elsewhere (an autoload, a static field) keeps what the old code did. A static field keeps
  its value when its first value is edited, and that edit starts nothing again. **2d**
- Whether code that runs once changed is told by comparing it, with what it calls by name or on the
  object itself; a method called on another object (`helper.go()` in `_ready`) is not followed, so an
  edit there needs the scene started again by hand. **2d**
- A second save in the one frame while a scene is being started again finds no scene, and says to start
  it by hand; the scene then has the first save's constructors with the second save's `_ready`. **2d**
- On the iOS Simulator the scene comes from the app's own copy: a scene file edited on the computer is not
  seen until the game is built again. **2d**
- Rider's hover was failing on code marked `actionscript` (fixed: the plugin names its own language); not
  yet seen working in Rider. **LS-2**
- Scripts are looked at five times a second by their time and size; two saves of the same size within
  one second are seen as one. **2d**
- A reload is refused when a class with a live object was removed or renamed. **2d**
- A game on a simulator reloads from the folder it is told of (`KAS3_SOURCE_DIR`); only scripts are read
  there, so an edited scene or picture needs a new build. A real phone or tablet cannot see that folder:
  it needs the edits sent to it. **2d**
- Not tried: a reload while a debugger is stopped, in the Godot editor's own play mode with the editor
  attached, or with more than a few hundred scripts. **2d**

## Stage 2c-3: `try`, `catch`, `finally`, labels

- An error inside a script method that Godot called is reported there: a `try` in AS3 around the Godot
  call that led to it does not see it. **2d**

## Stage 2c-2: the untyped value

- Members of an untyped value are refused: `x.name`, `x.method()`, `x[key]` wait for Object literals and
  dynamic access. **2c**
- A script object of a class that extends none of Godot's (in an untyped value or not) is an AS3 error when
  an AS3 call gives it to Godot; when Godot reads it (a return, a field), Godot gets null and an error is
  printed. Nothing converts it. (One of a class that extends a Godot class goes as its Godot object.)
  **decision**
- Godot's integers in an untyped value: one that fits an `int` is held as one, a larger one as an `Int64`;
  a whole float stays a float. My ruling, beside the Int64 rule the owner approved for typed values.
  **decision**

## Stage 2c-1: vectors, `switch` and `Math`

- A vector's methods that take a function (`forEach`, `map`, `filter`, `some`, `every`, `sort`) are refused:
  they wait for closures. **2c**
- `new Vector.<Vector2>(3)` holds three nulls, as a vector of objects does, not three zero vectors. **decision**
- A reload is refused when a vector that is alive has a type the new program no longer mentions, even if
  the vector is empty. **2d**

## Stage 2a: interpreter core

- Objects that refer to each other in a loop are never freed (the cycle collector). **2d**
- 4 of the 14 corpus programs do not run yet: `tests/run_corpus.py` prints what stops each, and two of
  the four are taken by the interpreter but wait for `[Signal]` (`tests/corpus/waits.txt`). **2c**
- An endless loop has no stop. **2d**

## Stage 2b-1: AS3 inside Godot, first slice

- **The first editor scan of a project with the extension crashes as Godot exits,** after the scan has done
  its work. The speed spike's extension does the same. Cause unknown. **2d**
- A script attached to a reference-counted Godot object would keep it alive for ever. **2d**
- godot-cpp is built as `template_release` and loaded by the editor binary; whether editor features need a
  debug build is unchecked. **2d**
- GDScript cannot set a script's field, only read it; a script's method and property lists are empty. **2d**
- A script's private methods and fields can be reached from GDScript. **2d**
- `super.method()` on one of Godot's virtuals does nothing, which is right for Godot's own empty versions
  but skips a virtual that a C++ extension class implements. **clean-up**
