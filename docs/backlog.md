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

## Stage 1e: Godot's math types

- Godot's string-format operator (`"%s" % value`) is not available. **2c**

## Language server, first slice

- **Manual editor acceptance remains pending.** Automated Rider and free unified IDEA checks cover
  completion, Undo, diagnostics and navigation. The plugin supplies the tested Rider lifetime repairs
  while preserving split Problems; see [validation](results/2026-11-completion-followup.md). **LS-2**

## Stage 2b-2, part B: signals

- A loop that passes through a Godot collection (a `GodotArray` or `GodotDictionary` in a field, such as
  one that holds the loop's own listener's callable) is never freed: Godot tells an extension how many
  hold a reference-counted object, but not how many hold an Array or a Dictionary, which GDScript may
  share. **2d**

## Stage 2b-3, part A: singletons, constants, static methods

- Godot calling a script from another thread (a method given to `WorkerThreadPool.addTask`) is refused
  with an error, and so is a field read or written from another thread. Running AS3 off the main thread
  waits for the `async` research. **2d**

## C++ clean-up, November 2026

The limits of [the C++ guidelines](cpp-guidelines.md) hold and are checked. These are its aims, and what
the review of the clean-up put off.

- **decision** The interpreter holds each of Godot's values through a handle. Its memory is now pooled and
  the names Godot asks a script about are looked up once (the engine benchmark: 44.4 ms, from 90.9), so
  making and dropping handles is about 12% of a call into Godot, most of it Godot's own destroy of the
  value. Holding Godot's values inline, with no handle, is a change across `ir`, `vm` and the host; the
  choice is whether it is worth that now.

## Reload while running

- A game on a simulator reads its scripts, scenes and text resources from the folder it is told of
  (`KAS3_SOURCE_DIR`), but an edited picture (any file Godot imports) needs a new build: importing needs
  Godot's editor. A real phone or tablet cannot see that folder: it needs the edits sent to it. **2d**
- Not tried: a reload while a debugger is stopped at a breakpoint (the editor's play mode, and 2,034
  scripts, were). **2d**

## Stage 2c-3: `try`, `catch`, `finally`, labels



## Stage 2a: interpreter core

