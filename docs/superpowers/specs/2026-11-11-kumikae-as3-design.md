# Kumikae AS3: ActionScript 3 for Godot

Design, 2026-11-11. Status: awaiting owner approval.

Lines marked **(owner)** are what the owner asked for. Lines marked **(call)** are the designer's decisions,
made on the owner's behalf and open to being overruled. Lines marked **(unverified)** are believed true but
were not checked; the stage that depends on one must check it first.

## Intent

Make ActionScript 3 a first-class scripting language for Godot 4.

The owner finds GDScript unclean. C# is pleasant but weak where it matters to them: consoles and mobile. Consoles and
iOS forbid a JIT, and Godot's C# support on mobile is thin. (Android does allow a JIT, so C# is a stronger
opponent there.) AS3 is a clean, typed, class-based language that many game
developers already know. The aim is the language that is nice to write, fast, and behaves the same on every
platform Godot reaches, including Nintendo Switch through the Kumikae toolchain.

The original idea also included running old SWF games inside Godot. The owner has no Flash games to run and
chose the language first. SWF playback is deferred, and this design keeps it possible (see "Keeping SWF open").

## Goals

1. **First-class in Godot (owner).** AS3 scripts extend Godot's own classes (`class Player extends
   CharacterBody2D`), attach to nodes like GDScript and C# scripts, export properties to the inspector, and
   declare and connect signals.
2. **One codebase, every platform (owner).** The same AS3 source runs on iOS, Android, Nintendo Switch (via the
   custom Kumikae Nintendo build), and desktop, with no per-platform code.
3. **Faster than Godot C# (owner).** Faster on gameplay code (code dominated by calls into the engine) on every
   platform, and at least level on pure computation on desktop. This is a measured gate, not a promise: see
   "Performance gate".
4. **Easy to use (owner).** Write a script, press play. No build step visible during editing.
5. **Easy to debug (owner).** Breakpoints, stepping, and variable inspection in the editor and on a real
   device, from JetBrains IDEs (Rider first). Readable AS3 stack traces in exported builds.

## Non-goals for this design

- Running `.swf` files, and the Flash API (`flash.display.Sprite`, `MovieClip`, `Event.ENTER_FRAME`).
- Importing art, timelines, or sounds from SWF or FLA files.
- Web export. Nothing here should block it, but no stage delivers it.
- Replacing GDScript or C# inside a project. AS3 scripts must coexist with both and call across to them
  through Godot's normal dynamic calls.

## Anti-goals

These make the project a failure even if everything technically works.

- **Editor and exported game behave differently.** There are two execution backends (below). A bug that
  appears only on a device, where debugging is hardest, is the worst outcome. Every language behaviour must be
  defined once and tested against both backends.
- **A JIT, anywhere.** iOS and consoles forbid it. A design that needs one on any platform has failed goal 2.
- **A private dialect.** Code that looks like AS3 but surprises AS3 programmers. Deviations from AS3 are
  allowed only where this document lists them (the "Language" section, string indexing under "Godot API
  bindings", and main-thread-only under "Memory"), and each must be documented and, where the compiler can
  see it, reported clearly.
- **Console vendor material in this repository.** No SDK names, tool names, device results, or adapter
  internals. Console-specific work lives in the console fork and the private adapter.

## Constraints

- **Godot 4.7 is the baseline (call).** The owner's console fork is Godot 4.7.2, and it sets the floor.
- **The console build cannot load dynamic libraries.** In the owner's console fork the dynamic-loading
  functions are stubs that always fail, the platform is a static library, and the fork keeps a rule that its
  diff against upstream in `core`, `scene`, `servers`, `main`, `modules`, `editor` stays empty. Whatever ships
  on console must be linked statically and registered from the platform layer or as an out-of-tree module,
  without touching upstream directories.
- **The console toolchain compiles C and C++ only.** The runtime and all generated code must be portable C++
  with no platform-specific dependencies and no dependence on C++ exceptions or RTTI **(call)**. Godot
  builds with C++ exceptions disabled (checked in the console fork's build script). Consequence: AS3
  `throw`/`catch` cannot be C++ exceptions. Generated code propagates errors explicitly through return paths,
  and that cost is part of what the benchmarks measure.
- **No Rust in the shipped runtime (call).** It would need its own console target. The editor-side compiler
  could in principle be anything, but one language across compiler and runtime is simpler.

## Shape

One front end, one shared intermediate form, two backends.

```
 .as source ──► front end ──► typed IR ──┬──► interpreter      (editor, debug runs, hot reload)
              (parse, check)             └──► C++ generator ──► platform C++ compiler ──► native code (exports)
                                    ▲
 (later) SWF bytecode reader ───────┘
```

### Front end

Parses AS3, resolves packages and imports, type-checks, and lowers to the typed IR. Written from scratch in
C++ **(call)**. It is a library with three hosts: the Godot editor plugin, a command-line tool for CI and
export, and the language server.

Existing compilers were considered and rejected **(call)**: Apache Royale's and Adobe's are Java, which would
put a JVM inside the Godot editor.

### Typed IR

The single definition of program meaning. Both backends consume it and nothing else. It is register-based
and carries static types, so typed code lowers to typed operations (a `Number` add is a double add) and
untyped code lowers to dynamic operations on a tagged value.

The IR is our own format, not Flash's AVM2 bytecode **(call)**. AVM2 bytecode is stack-based and loosely typed,
which makes it a poor source for fast native code and a poor fit for Godot's types. Cost of this choice: SWF
support later needs a reader that lifts AVM2 bytecode into the IR, rather than running it directly.

### Interpreter backend

Runs the IR directly. Used for running the game from the editor and for debug builds on every platform,
devices included **(call)**. Provides hot reload (changing a script while the game runs) and the debugger
hooks. It needs no C++ toolchain on the developer's machine, which is what makes "write a script, press play"
true.

A build is entirely one or the other: debug builds carry the interpreter and precompiled IR, release builds
carry native code and no interpreter. Interpreted and native AS3 classes never mix in one process, and the
front end never ships in a game. Loading AS3 that was not known at build time (mods, downloaded patches) is
out of scope.

It is not a performance target. It should be in GDScript's range.

### Native backend

Translates the IR to C++, which the target platform's own C++ compiler turns into machine code. Used for
every release export on every platform. A release export therefore requires a C++ toolchain for the target, as C#
exports require the .NET SDK. Typed AS3 becomes plain C++: classes become structs with direct member access,
calls between AS3 classes become direct C++ calls, `int` and `Number` become machine integers and doubles.

Precedent: Adobe AIR compiled AS3 ahead of time for iOS, Unity compiles C# through C++ (IL2CPP), Haxe has a
C++ target.

On desktop and Android the game's generated code is compiled into its own library, loaded beside the runtime
extension. On iOS and console it is linked statically into the application with the runtime.

### How AS3 appears to Godot

Both backends present AS3 classes to Godot as **scripts attached to objects**, through Godot's script-language
extension interface **(call)**. This is what gives attach-to-node, the inspector, and signals, and it keeps the
two backends identical from the engine's point of view.

What this means for speed, stated plainly:

- **Engine calling into AS3** (`_process`, `_physics_process`, signal handlers) arrives as a dynamic call by
  name with boxed arguments, the same way it does for C# and GDScript. The runtime should resolve the name to
  a native entry once and cache it.
- **AS3 calling into the engine** goes through Godot's typed direct-call path for bound methods, with no boxed
  values. Methods that take a variable number of arguments (`emit_signal`, `call`, `call_deferred`, `rpc`)
  have only the boxed path, so signal emission pays for boxing as it does in every Godot language.
- **AS3 calling AS3** never touches the engine: a direct C++ call in native builds.

The alternative, registering each AS3 class as a native engine class, would make engine-to-AS3 calls faster
but would diverge from the interpreter and lose script attachment. Rejected for now; the benchmark stage may
reopen it for hot callbacks only.

### Godot API bindings

Generated from the API description Godot itself exports (`extension_api.json`), never written by hand. The
generator produces AS3 declarations for the front end and type-checker, call stubs for the interpreter, and
C++ call code for the native backend.

Mapping rules **(call)**:

- Godot classes become AS3 classes in a `godot` package. Method and property names keep Godot's spelling
  (`move_and_slide`), so Godot's documentation applies unchanged.
- Godot's built-in value types (`Vector2`, `Vector3`, `Color`, `Transform2D`, and the rest) are value types in
  AS3 too: copied on assignment, never null, never heap-allocated in native builds.
- **Strings are Godot's own strings underneath (call).** Engine calls are string-heavy, and converting on
  every call would cost the speed goal. Known difference from Flash: indexing is by Unicode code point, not
  UTF-16 unit, which only shows on characters outside the basic plane. The compiler documents it.
- **`Array`, `Vector.<T>`, `Dictionary`, and `Object` are AS3's own types (call)**, so they keep AS3
  behaviour (sparse arrays, enumeration order, typed vectors stored flat) and stay fast inside AS3 code. They
  convert to Godot's `Array`, packed arrays, and `Dictionary` only when passed to or returned from the engine.
  Stage 0 measures that conversion.
- A plain AS3 object (one that does not extend a Godot class) crosses to the engine as an opaque
  reference-counted handle. GDScript can hold it and pass it back but not look inside it.
- Godot integers are 64-bit and AS3's `int` is 32-bit. The mapping must not truncate silently. How (a 64-bit
  integer type, or range checks) is left to the builder, under that rule.

### Packaging

- **Desktop and mobile:** a GDExtension that works with stock Godot 4.7. Nobody needs a custom engine to try
  it. On iOS, GDExtensions are already linked statically.
- **Console:** the same runtime and the game's generated C++ are linked statically into the engine build.
  Registration happens from the console platform layer or as an out-of-tree module, so the fork's
  empty-upstream-diff rule holds **(unverified:** that Godot 4.7's extension loading can be satisfied from a
  static table without changes under `core`; the console stage starts by proving this, and falls back to an
  out-of-tree engine module using the same runtime sources if not).

To keep that fallback cheap, the runtime's dependence on Godot goes through one narrow internal interface
rather than being spread through the code **(call)**.

## Language

Target: ActionScript 3 as programmers know it from Flash and AIR.

**In the first version:** packages and imports; classes, interfaces, inheritance; `public`, `private`,
`protected`, `internal`; static and instance members; getters and setters; `const`; functions as values and
closures; default and rest parameters; `int`, `uint`, `Number`, `Boolean`, `String`; `Array`, `Vector.<T>`,
`Object` literals, `Dictionary`; `is` and `as`; `for`, `for in`, `for each`, `switch`, labelled loops;
`try`/`catch`/`finally` and `throw`; untyped values (`*`); metadata tags.

**Standard library in the first version (call):** the top-level functions (`trace`, `parseInt`, `parseFloat`,
`isNaN`, and the rest), `Math`, the full `String`, `Array`, and `Vector` method sets, the `Error` classes,
`JSON`, `Date`, and `RegExp` including literal syntax. From `flash.utils`, only `Dictionary` and `getTimer`,
kept under their original package names so existing habits and code carry over. `ByteArray` and the rest of
the `flash.*` packages are not included; Godot's own classes cover that ground.

**Left out of the first version, with a clear compiler error (call):** E4X (inline XML), the `with` statement,
changing a class's `prototype` at run time, user-defined namespaces, `Proxy`, and dynamic classes.

**Deliberate additions (call).** Each is a place where pure AS3 would be painful in Godot:

- **Metadata for Godot concepts.** `[Export]` on a field shows it in the inspector; `[Signal]` declares a
  signal; `[Tool]` marks an editor script. Metadata is already AS3 syntax, so nothing new is invented.
- **Arithmetic operators on Godot's math value types.** AS3 has no operator overloading, so strict AS3 would
  force `position.add(velocity.multiply(delta))`. The compiler allows `+ - * /` and comparison on the built-in
  math types only. User classes cannot define operators.

**Not decided here:** waiting on a signal in the middle of a function (GDScript's `await`). AS3 has no such
construct. The first version connects signals to callback functions. Whether to add an await form is a later
design.

### Memory

AS3 programmers expect garbage collection: objects referencing each other in a loop still get freed.

- **Godot objects follow Godot's rules.** Nodes are freed explicitly (`queue_free`); reference-counted
  resources are counted. AS3 holds them the same way GDScript does.
- **AS3 objects use reference counting plus a cycle collector (call).** Reference counting frees most objects
  at once with no pauses. A small incremental collector, with a fixed time budget per frame, finds and frees
  loops that counting alone would leak. This avoids the stop-everything pauses of a tracing collector, which
  is a place C# is weak in games.
- **AS3 code runs on the main thread only in the first version (call).** Reference counts are therefore plain,
  not atomic, which is cheaper on every assignment. If Godot calls an AS3 script from another thread, the call
  fails with a clear error rather than corrupting memory. Threading is a later design.
- Weak references are available for the cases where the programmer wants to avoid a loop outright.

## Debugging

- **In the editor:** the interpreter serves breakpoints, stepping, call stack, and variables, shown as AS3
  values. It speaks to Godot's own script debugger and to external tools.
- **External tools:** one debug adapter speaking the Debug Adapter Protocol, and one language server speaking
  the Language Server Protocol, so each IDE needs only a thin client **(call)**.
- **JetBrains IDEs (owner):** one IntelliJ-platform plugin covering Rider and the other IDEs. It supplies
  file-type recognition and wires up the language server and debug adapter. **(unverified:** which JetBrains
  editions expose LSP and DAP clients to plugins. If DAP is not usable, the plugin implements the platform's
  native debugger interface against the same adapter back end.)
- **On device:** a debug build runs the interpreter, so the same debug adapter attaches over the network and
  the experience matches the editor. This is the main route for on-device debugging and needs no native
  debugger. **(unverified:** that a console unit can accept this network connection from the development
  machine; Godot's own remote debugger faces the same question there.)
- **On device, release builds:** generated C++ carries line markers pointing at the `.as` source, so a native
  debugger steps through AS3 lines. Variables need debugger formatters to read as AS3 values rather than
  runtime internals; these ship with the project. **(unverified:** native debugging on the console device has
  not yet been proven by the owner's toolchain. Only this bullet depends on it. It matters for
  bugs that appear in native builds alone, which the conformance suite exists to prevent.)
- **Everywhere:** an uncaught error prints an AS3 stack trace with file and line, in release builds too.
- **Before running:** the type checker reports type errors and definite null misuse at compile time.

## Performance gate

Goal 3 is checked, not assumed.

A benchmark suite compares AS3 native builds against Godot C# and GDScript on the same machine and engine
version, in three groups: engine-call-heavy gameplay code (moving many nodes, physics queries, signal
traffic), pure computation, and allocation-heavy code (to expose collection pauses, measured as worst frame
time). It runs on desktop, one Android device, and one iOS device; console runs are recorded privately.

The C# baseline is Godot's official C# support where it exists: desktop, Android (JIT), and iOS (ahead of
time) **(unverified:** the state of Godot 4.7's C# on Android and iOS; the mobile stage confirms what the
baseline actually is before measuring). Stock Godot has no C# on console, so there the numbers are recorded against GDScript and against the
same test on desktop, with no pass or fail.

Pass means **(call)**: at least 10% ahead of C# on the gameplay group on every platform that has a baseline,
beyond run-to-run noise, and no worse than C# on pure computation on desktop.

If the gate fails: first try registering hot callbacks natively (see "How AS3 appears to Godot"). If it still
fails, stop and take the numbers to the owner before building further. The same rule applies to stage 0.

## Keeping the two backends identical

- One conformance suite of AS3 programs with expected output. Every test runs on the interpreter and on a
  native build; a difference between them is a failing test even if both are "reasonable".
- The suite covers the awkward corners deliberately: number-to-integer conversion, integer overflow, string
  conversion of numbers, evaluation order, property enumeration order, uncaught errors.
- A feature is not done until it passes on both backends.

## Keeping SWF open

Nothing in this design delivers SWF playback, but these choices preserve it:

- The language follows real AS3 semantics rather than a simplified dialect, so code compiled by Adobe's tools
  means the same thing.
- A future reader can lift a SWF's AVM2 bytecode into the IR. From there the code gets both backends: the
  interpreter for SWFs loaded at run time, and native export for SWFs known at build time.
- The features left out of the first version (E4X, `with`, prototypes, namespaces, dynamic classes) are used
  by real Flash content. The IR's dynamic operations must be designed so they can be added without rework.
- The Flash API would be a library on top of Godot nodes, written largely in AS3 itself.

## Stages

Each stage ends in something that can be run and shown. Each gets its own plan.

0. **Speed spike (throwaway).** Hand-written C++ in the shape the native backend would emit, attached to
   nodes as a script, benchmarked against C# and GDScript on desktop, using the gameplay group and the
   collection-conversion cost. Purpose: learn whether goal 3 is reachable with the script-instance approach
   before building a compiler around it. The spike's C++ must pay every cost real generated code will pay:
   reference-count updates, null checks, line bookkeeping for release stack traces, and error propagation
   without C++ exceptions. A spike without them would flatter the design. Exit: the performance gate's
   desktop numbers, pass or fail.
1. **Front end.** Parse and type-check the first-version language; command-line tool that reports errors
   with file and line; conformance tests for the checker.
2. **Interpreter in Godot.** IR, interpreter, generated bindings, script-language integration. First
   milestone: an AS3 script on a `CharacterBody2D` moves a character in the editor. Then exports, signals,
   hot reload.
3. **Native export on desktop.** C++ generator, export integration, the conformance suite on both backends,
   the benchmark suite and the performance gate.
4. **Mobile.** iOS and Android, both build kinds: interpreted debug builds and native release builds. The
   performance gate on devices.
5. **Console.** Static linking and registration in the owner's console build; the same test game running on
   hardware, as an interpreted debug build and as a native release build. Work that touches vendor material stays out of this repository.
6. **Tooling.** Language server, debug adapter, JetBrains plugin, attaching the debugger to debug builds on
   devices, and source-level debugging of release builds. Basic editor
   highlighting and error reporting arrive with stage 2; this stage is the full experience.

Later, separately designed: an await form for signals, the Flash API library, SWF loading, web export.

## Left to the builder

- Internal structure of the front end, IR encoding, and interpreter dispatch technique.
- Build system and test framework.
- The 64-bit integer mapping, within the no-silent-truncation rule.
- The cycle collector algorithm, within the per-frame budget rule.
- Whether script metadata tag names need a prefix to avoid clashing with Flex-era tags.
- The file extension is `.as`.

## Open question for the owner

- **Is this repository public and open source,** like the Kumikae toolkit (MIT, under the AniCanon
  organisation), or private? No licence file or remote is created until this is answered.
