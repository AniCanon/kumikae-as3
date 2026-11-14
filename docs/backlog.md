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

Decided on 2026-11-12. Each is owed as code and tests. (Two more decisions of that day are built: static
fields are set on a class's first use, and a call on a null object evaluates its arguments first.)

| Decision | What to build | When |
|---|---|---|
| **64-bit integers are supported,** in whatever way is easiest to maintain in the long term | A real 64-bit integer type, `Int64`, with one rule in the generator: a Godot integer that Godot's description marks as 64-bit is an `Int64`. The hand-kept table of 31 exceptions goes away. An `Int64` converts to `int`, `uint` or `Number` where one is expected, with a check that raises a `RangeError` if the value does not fit, so nothing is ever cut off silently. It needs its own short design first. | Stage 2b-3 |

## Stage 1d: Godot's API in AS3

- A script class named like a Godot class is ambiguous in the default package.
- Acronyms come out lower-case: `getAabb`, not `getAABB`.
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
- A malformed tag in a declaration file (both `left` and `right`, two rules for one operator) is resolved
  silently.
- `for each` over a packed array does not check the variable's type.
- Godot's string-format operator (`"%s" % value`) is not available.

## Language server, first slice

- **Rider is not yet tried.** The LSP4IJ plugin is installed; adding the server in its settings is not.
- **Windows is not supported:** text-mode reads and writes, and `/` assumed in paths.
- The same file reached through a symbolic link, or under a different letter case, or in a different
  Unicode form, counts as two files.
- Symbol search covers the project only, not Godot's classes.
- On go to definition, `new X()` goes to the class, not to its constructor.
- While any file has a syntax error, type errors are hidden everywhere. Silencing only the broken region
  would be kinder.

## Backlog pass, November 2026

Found by the review of the clean-up pass itself.

- A document that starts with a byte-order mark: a changed range on its first line lands one character early.
- In UTF-8 position mode, a position inside a multi-byte character splits the character.
- A Godot member reached through a missing script object (`other.getChild(note())`) is reported before the
  call's arguments are evaluated; for AS3's own members the arguments come first.
- A static call's arguments are evaluated before the class's static fields are set, and `x is C` does not
  set up `C`. Whether Flash does the same is unchecked.
- Find references does not link an interface's method to the methods that implement it.
- Tests are missing for: private and static members of the same name not counting as overrides, and the
  base-first order when a host constructs an object.

## Stage 2b-2, part A: Godot's values

- A `null` that is not the last argument, for a parameter whose default AS3 can't write
  (`color.clamp(null, max)`), is an error when the line runs. Giving it Godot's real default needs the
  defaults in the name table.
- A `Number` used as an index is cut to a whole number by Godot: `v[1.5]` reads element 1, and `v[NaN]`
  element 0.
- One part of a value that is stored nowhere (`a.normalized().x = 5`) is refused. So is `points[0].x = 5`
  on a packed array of vectors, though there the value is stored.
- A local that the right-hand side reassigns is read after it: `a.position.x = (a = b, 5)` writes to `b`.
  (A whole property behaves the same: `a.visible = (a = b, false)`.)
- A freed node is not `null` in `kept == null`, but equals a variable that holds null.
- `x >>>= vector` passes the checker and fails when it runs.
- Reading or writing a property of a freed node says Godot has no such property, not that the node is
  freed. A freed node passed as an argument gets Godot's own error, and no AS3 error.
- A whole number worked out from `int`s (`cell * (a * b)`) goes to Godot as a 32-bit `int`: a product past
  2^31 wraps. Goes away with `Int64`.
- **decision** `"" + this` in a script without `toString()` reads `[object Values]` (AS3's), while Godot's
  text for the same node reads `<Node2D#123>`. And a `toString()` that raises an error ends the caller
  through `"" + this`, but through Godot's text it is reported and Godot's text stands.

## Stage 2b-2, part B: signals

- Only a method of a script object can be a listener. A function written in place, a package function and
  a static method wait for closures (2c).
- A signal's own `numListeners` and `emit` can't follow `object[name]` as a property read: only its
  methods can (`timer[name].add(f)`).
- A singleton's signals (`Input.joyConnectionChanged`) wait for singletons (2b-3); a script's own signals
  wait for `[Signal]` (2d).
- Objects that hold each other in a loop are freed only when a listener is part of the loop, or reaches
  it. A loop of script objects with no listener in it stays (the cycle collector, 2d). So does a loop
  that passes through a Godot collection.
- The look for such loops walks everything the listeners without a Godot object of their own can reach.
  It is paced by its own cost (at most four times a second, about a fiftieth of the time), but one look
  over 100,000 objects takes about 30 ms in one go. The cycle collector (2d) should work in steps.
- When GDScript holds a copy of a listener's callable, the look moves that listener behind listeners added
  after it.
- A loop whose own field holds its listener's callable (in a Godot collection), or whose one callable is
  in two signals, is never freed.
- `other[name].add(f)` on another script object reaches only the Godot object's signals, not a signal the
  other script keeps in a public field. `this[name]` finds a subclass's field before a base class's
  private one of the same name.
- A listener given arguments that don't fit reports its AS3 error and tells Godot the call went well, so
  `callv` and `filter` see no failure.
- When the scene tree is taken down, static fields are emptied and listeners stop working, so that Godot
  objects are let go while they can still be freed. A script that runs after that sees null statics. If
  the main loop is not a scene tree, an object held by a static field with a GDScript listener on it
  still aborts Godot at exit.

## Stage 2a: interpreter core

- Objects that refer to each other in a loop are never freed (the cycle collector is planned for 2d).
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
- GDScript cannot set a script's field, only read it; a script's method and property lists are empty.
- A method that returns a script object can't hand it to Godot: the caller gets null, and an error says so.
- A script's private methods and fields can be reached from GDScript.
- A handle on a Godot object that has since been freed still counts as not null. Its text is Godot's
  `<Freed Object>`.
- `super.method()` on one of Godot's virtuals does nothing, which is right for Godot's own empty versions
  but skips a virtual that a C++ extension class implements.
