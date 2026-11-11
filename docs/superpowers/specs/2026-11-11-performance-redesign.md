# Kumikae AS3: performance redesign

2026-11-11. Amends `2026-11-11-kumikae-as3-design.md`. Where the two disagree, this document wins.

Lines marked **(owner)** are the owner's words. **(call)** marks the designer's decisions. **(measured)** marks
numbers from this repository. **(research)** marks findings from the sources listed at the end.

## Why this exists

The stage 0 spike failed its gate: against Godot C# on desktop, AS3-shaped code won clearly on two of six
benchmarks and was level or behind on four (`docs/results/2026-11-stage-0-desktop.md`).

The owner then decided **(owner)**: beating C# on desktop is a hard requirement, and the design is free to
change: "Research, use best practices, papers to implement the best in class. We're not stuck with ideas."

## The gate, restated

**AS3 must be ahead of C# on every judged benchmark, with no overlap between the two languages' runs (call).**
"No overlap" means the slowest of AS3's five runs is faster than the fastest of C#'s five. The earlier 10%
margin and the 3% "no worse" allowance were the designer's inventions and are dropped: a fixed margin is
unreachable where the engine's own work dominates, and "no worse" is weaker than what the owner asked for.

**This gate was set after the first numbers were seen, and the owner has not confirmed it.** Under it,
`move_nodes` can pass with a lead of a few percent, which is all the plugin route allows (see the floor
below). If the owner wants a larger margin there, the reserve levers at the end are the way to get it.

### Guarding against fitting the rules to the benchmarks

The rules below were written after studying the six original benchmarks, so passing those six proves less
than it seems. Three **holdout benchmarks** are therefore added. They were specified here before any
version 2 code or number existed, they are judged by the same gate, and a holdout failure is a gate failure.

| Name | What it stresses | Work per frame |
|---|---|---|
| `saxpy` | A unit-stride numeric loop, where unrolling and vectorising help | Two `Vector.<Number>` of 100,000, filled once (`x[i] = i * 0.5`, `y[i] = i`). Ten passes of `y[i] = a * x[i] + y[i]` with `a = 0.000001`, then one pass summing `y`. Checksum: the last sum. |
| `shapes` | Objects and overridable method calls | 9,000 objects, one third each of `Circle`, `Rect`, `Tri`, all extending `Shape`, interleaved in one `Vector.<Shape>`. Each has overrides of `area():Number` and `grow(d:Number):void`. Ten passes calling `grow(0.001)` then adding `area()` to a total. Checksum: the total. |
| `alloc` | Allocation every frame | A `Vector.<Particle>` of 20,000 slots. Each frame every slot is replaced by a new `Particle(x, y, vx, vy)` built from the slot index and frame number, then one pass sums `p.x + p.vy`. Checksum: the sum. |

Containers and classes, pinned for all languages: `saxpy` uses GDScript `PackedFloat64Array`, C# `double[]`;
`shapes` uses GDScript classes with typed methods, C# an abstract base with `sealed` subclasses; `alloc` uses
a GDScript `Array[Particle]`, C# `Particle[]` with a `sealed class Particle`.

### Measurement protocol for the re-run

- The first spike's translation (version 1) stays as its own language. Version 1, version 2, the floor, C#,
  and GDScript are measured in the same run.
- The order of languages rotates with each repetition.
- Each repetition also times a fixed standalone calibration loop. A repetition whose calibration differs by
  more than 5% from the first is discarded and repeated. (Between two earlier runs every language's math
  benchmarks sped up about 1.5 times for no known reason; this detects that.)
- The floor is built with the version 2 flags. A version 2 result more than 3% faster than the floor is
  flagged in the report, because it suggests skipped work. The holdouts have no floor.
- Five repetitions; warm-up of at least 300 frames and two seconds, as before.

## What the evidence says

**The floor (measured).** The same benchmarks written as plain C++ through the plugin interface, with no AS3
semantics at all, against C# (`docs/results/2026-11-stage-0-floor.json`, median microseconds, three runs):

| Benchmark | Floor | C# | C# ÷ floor |
|---|---|---|---|
| `move_nodes` | 797 | 828 | 1.04 |
| `per_node_process` | 972 | 2,038 | 2.10 |
| `ray_queries` | 1,297 | 1,463 | 1.13 |
| `signal_traffic` | 720 | 1,087 | 1.51 |
| `nbody` | 58 | 63 | 1.09 |
| `sieve` | 416 | 388 | 0.93 |

So the losses in the first spike came from costs the first design chose to pay, not from the approach.

**Where the first design paid (measured and research).**

- It stored a source line before every statement that could throw. IL2CPP and Haxe's C++ target both treat
  that shadow-stack bookkeeping as an overhead to avoid in release builds.
- It tested an error flag after every engine call, although errors never cross the engine boundary.
- It re-read a vector's length and data pointer from the object on every access. Stores through an `int`
  pointer may alias those fields, so the C++ compiler must reload them and cannot remove bounds checks or
  vectorise.
- It retained and released a reference for every local that received an object.
- It copied every engine collection into an AS3 one.
- The C++ compiler unrolled loops whose trip counts are short. With unrolling off, plain C++ runs the sieve
  in 335 µs against 388 µs with it on and 369 µs for C#, all measured outside the engine.

**What the engine fixes for everyone (research, from the Godot 4.7.2 source).** C# calls the engine through the
same method-bind call a plugin uses. `Node2D.set_position` itself rebuilds a transform with four trigonometric
calls, notifies the renderer, and propagates the change; that is most of a `move_nodes` frame, and no
scripting language can remove it through that call. Hence the 4% ceiling there.

## Design changes

### 1. Stack traces cost nothing until an error happens

No line bookkeeping during normal execution. A trace is assembled on the error path **(call)**:

- A `throw` records its own site (function, file, line) into the trace.
- Every place that propagates an error out of a function appends that call's site as it returns.
- Sites are static constants, so the only cost in normal running is the propagation branch that already
  exists.

This is how Zig builds error return traces, and it needs no unwinder, frame pointers, or debug tables, so it
works identically on every platform including consoles. The trace covers the frames between the throw and
whatever catches it, which is exactly what an uncaught error prints.

Where a platform does offer native unwinding, generated C++ also carries `#line` markers so native debuggers
and crash reporters see `.as` files. That is an addition, not the mechanism.

### 2. Error checks only where an error can arrive

The out-of-band pending-error flag stays (no C++ exceptions). What changes is where it is tested **(call)**:

- **After a call to an AS3 function, only if that function can throw.** The compiler computes "can throw" for
  every function over the whole program (as Nim does), treating dynamic calls and overridable methods as able
  to throw.
- **Never after an engine call.** An AS3 error raised inside a callback the engine invoked is reported and
  cleared at that callback's boundary; it never returns through engine code into another AS3 function. This
  matches how Godot treats errors in GDScript and C# callbacks.
- This is sound only if **every** entry point from the engine into AS3 reports and clears: script method
  calls and native callables alike. A test must throw inside a signal handler and show the emitter carrying
  on.
- **Language consequence:** `try`/`catch` cannot catch an error thrown on the far side of an engine call, for
  example inside a handler of a signal the `try` block emitted. Flash would have delivered it. This is a
  listed deviation.
- The test is marked unlikely and its body kept out of line.

### 3. Null checks: explicit, with redundant ones removed in the compiler

Hardware-trap null checks are not available to portable C++ on consoles, so checks stay explicit (as IL2CPP
does). The compiler removes a check when **(call)**:

- the reference is `this`, or was constructed in the function;
- an earlier check of the same value dominates it. A local keeps its proof for its whole life, because only
  an assignment changes a local;
- a field's value has been copied to a temporary: the temporary is checked once.

A field must be re-read and re-checked after any call that could write it. Engine calls count as able to
write fields, because the engine can call back into AS3. A call to an AS3 function the compiler can prove
does not write the field does not.

### 4. Bounds checks: hoisted, and loops versioned

- **Length and data in locals (call).** Within a region where nothing can resize or replace a vector, its
  length and data pointer are loaded once into locals. This applies to every element type.
- **Loop versioning in our compiler (call).** For a counted loop over a vector, the compiler emits a guard
  that proves every index is in range, then a check-free loop, with the fully checked loop as the else branch.
  The guard covers non-unit strides (`j += i`): stride positive, start non-negative, and no 32-bit wrap. The
  .NET JIT clones only unit-stride loops, and the C++ compiler cannot prove the stride case.
- Behaviour is unchanged: when the guard fails, the checked loop throws at the same iteration as before.
- Where neither applies, the check stays, as one inline branch to the error exit.

### 5. Object fields live in locals across call-free regions

Fields of `this` and of other objects are read into locals at the start of a call-free region and written
back at its exits **(call)**. This removes the aliasing reloads that cost the first spike its math benchmarks.

### 6. Reference counting with ownership decided at compile time

Reference counting stays (flat frame times, no collector pauses), with the standard ownership rules of
Lobster, Swift, and Nim **(call)**:

- Parameters are borrowed; returns are owned.
- A local that aliases a value held by a field, a container, or another local **borrows** it (no retain or
  release) when nothing in the local's lifetime can release the owner's reference. A call-free lifetime always
  qualifies; so does one whose calls are proven not to write the owner.
- The last use of an owned local moves instead of copying.

Cycles: Bacon–Rajan trial deletion, run at the end of the frame under a time budget, with classes that
cannot form cycles exempted by static analysis. Unbounded cascades of frees go through a budgeted queue.
Neither is exercised by the gate; both remain stage work.

### 7. Engine collections are not converted

This reverses the first design's decision.

- **A `Dictionary` or `Array` that comes from the engine stays a Godot one (call).** AS3 code holds it as a
  value of the Godot type and uses the Godot API on it (`is_empty()`, `has()`, indexing). Reading one key
  reads one key.
- Indexing with a constant key uses a key built once.
- Coercing an element to a typed variable checks the element's type and throws `TypeError` on mismatch.
- AS3's own `Array`, `Vector.<T>`, `Object`, and `Dictionary` (the `flash.utils` one) remain AS3 types with
  AS3 behaviour. Passing one to the engine still converts.
- **To investigate later (call):** backing `Vector.<T>` with Godot's packed arrays when `T` is a packed element
  type, so that passing it to the engine copies nothing. Not part of this gate; `array_conversion` stays
  unjudged.

Cost: values from the engine behave like Godot collections, not Flash ones. That matters for the deferred SWF
goal, where a compatibility layer would own its collections anyway.

### 8. Method closures are native callables

Connecting a signal to an AS3 method (`connect("hit", on_hit)`) creates a native callable holding the object
and a direct entry point **(call)**. The engine then calls straight into it, with no object lookup and no
dispatch by name. The callable carries the owner's instance id, so the engine disconnects it when the owner
is freed, and the entry point checks a tombstone that the object's destruction sets, so a stale callable
fails cleanly instead of touching freed memory. The language needs this for method closures regardless; it also removes most of the
receiving cost of a signal.

### 9. Compiler flags for generated code

`-O2 -fno-exceptions -fno-rtti -ffp-contract=off -fno-unroll-loops` **(call)**.

Unrolling is off because an ahead-of-time compiler has no trip-count profile, and unrolling a loop that
usually runs a few times costs more than it saves. GCC makes the same choice at `-O2`. Profile-guided builds
can turn it back on where a profile supports it.

### 10. A size-class allocator

AS3 objects are allocated from per-size free lists in chunks, not from the system allocator **(call)**. This
is standard practice for reference-counted runtimes (mimalloc was built for exactly this use). Main thread
only, so the lists need no locks. Fixed here, before `alloc` is measured.

### 11. What does not change

- AS3 classes still appear to Godot as scripts attached to objects, in both backends.
- One IR, two backends, and the conformance suite that keeps them identical.
- Main thread only; non-atomic counts.
- Strings are Godot strings.

### Levers kept in reserve

These are known, faster, and not needed to pass the gate. Each has a cost that should be paid only on
evidence.

- **Linking into the engine instead of the plugin interface.** Direct C++ calls save the indirections
  (estimated 3 to 5 ns of a 19 ns position round trip) and reach struct-returning internals such as the ray
  query that never builds a dictionary. Console builds link this way already. On desktop it means release
  exports use a custom engine build.
- **One driver per frame instead of one callback per node.** The engine offers a per-frame hook; AS3 objects
  could be stepped from a native array. It changes the order of processing relative to other languages' nodes.
- **Registering AS3 classes as native engine classes** for the cheapest engine-to-script call. It diverges
  from the interpreter and loses script attachment.

## Translation rules, version 2

These replace the rules in the stage 0 plan for the re-measurement. They are fixed here, before the run.

1. **Objects.** Unchanged: header of count and class pointer; typed fields.
2. **References.** Parameters borrowed. Returns owned. A local aliasing a field, container element, or local
   borrows when its lifetime contains no call that could release the owner; otherwise it owns.
3. **Null checks.** Explicit. Omitted on `this`, on a value constructed in the function, and on a local or
   temporary already checked on every path to this point. A field is re-read and re-checked after any engine
   call and after any AS3 call not proven to leave it alone.
4. **Errors.** `throw` sets the pending error and records its site. After a call to an AS3 function that can
   throw, the caller tests the flag; if set, it appends the call site to the trace and returns. No test after
   engine calls or after AS3 functions proven not to throw.
5. **Line bookkeeping.** None in normal execution. Sites are static constants used only on the error path.
6. **Numbers.** Unchanged.
7. **Collections.** `Vector.<T>`: length and data in locals across regions with no resize; counted loops are
   versioned (guard, check-free loop, checked loop). An engine call counts as able to resize or replace any
   vector, exactly as it counts as able to write any field, so nothing is hoisted across one. Engine `Dictionary` and `Array` values are used in place
   through the engine's own accessors, with constant keys built once and a type check on typed coercion.
   AS3-owned collections convert when passed to the engine.
8. **Godot objects.** Unchanged.
9. **Engine calls.** Unchanged: direct typed calls through method binds fetched at load; vararg methods through
   the boxed call.
10. **Engine calling in.** Unchanged for script methods. A method closure connected to a signal is a native
    callable with a direct entry point.
11. **Strings.** Unchanged.
12. **Fields in locals.** Fields are read into locals at the start of a call-free region and written back at
    every exit from it, error exits included.
13. **Flags.** As in section 9.
14. **Allocation.** AS3 objects come from the size-class allocator.
15. **Virtual calls.** An overridable method is called through the class's method table. The call is tested
    for a pending error only if some override in the program can throw.
16. **Evidence in the code.** Every check the rules allow to be omitted is marked at that spot with the rule
    that allows it.

The floor and the version 2 translation are compiled with the same flags. Version 1 keeps its original flags.

The AS3 source of each benchmark is unchanged except `RayQueries.as`, which now holds the query result as a
Godot `Dictionary` and tests `is_empty()`, the same shape as the GDScript and C# versions.

## Not addressed here

- Mobile and console measurement.
- The cycle collector and allocation-heavy code.
- The interpreter.
- Whether the reserve levers are worth their costs.

## Sources

Research was gathered by four independent passes; findings they marked unverified are not relied on above.

- Godot 4.7.2 source (local fork, upstream-identical core): `scene/2d/node_2d.cpp`, `core/object/make_virtuals.py`,
  `core/object/script_language_extension.h`, `core/object/object.cpp`, `modules/mono/csharp_script.cpp`,
  `modules/mono/glue/runtime_interop.cpp`, `servers/physics_2d/physics_server_2d.{h,cpp}`,
  `core/extension/gdextension_interface.cpp`.
- Zig error return traces: https://ziglang.org/documentation/master/#Error-Return-Traces
- Unity IL2CPP stack traces and runtime checks:
  https://docs.unity.com/en-us/engine/6000.0/manual/programming-environment/scripting-backends/il2cpp/il2cpp-managed-stack-traces
- Nim's goto-based exceptions (`canRaise`): https://github.com/nim-lang/Nim/blob/devel/compiler/ccgcalls.nim
- Swift error handling rationale: https://github.com/swiftlang/swift/blob/main/docs/ErrorHandlingRationale.md
- B. Craig, "Error speed benchmarking", P1886R0, 2019: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1886r0.html
- T. Neumann, "C++ exceptions are becoming more and more problematic", P2544R0, 2022:
  https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2544r0.html
- R. Bodik, R. Gupta, V. Sarkar, "ABCD: eliminating array bounds checks on demand", PLDI 2000:
  https://homes.cs.washington.edu/~bodik/ucb/research/pldi00.html
- .NET JIT loop cloning: https://github.com/dotnet/runtime/blob/main/src/coreclr/jit/loopcloning.h
- W. van Oortmerssen, Lobster memory management: https://aardappel.github.io/lobster/memory_management.html
- Nim ORC: https://nim-lang.org/blog/2020/12/08/introducing-orc.html
- Swift parameter ownership (SE-0377): https://github.com/swiftlang/swift-evolution/blob/main/proposals/0377-parameter-ownership-modifiers.md
- D. Bacon, V. T. Rajan, "Concurrent cycle collection in reference counted systems", ECOOP 2001:
  https://pages.cs.wisc.edu/~cymen/misc/interests/Bacon01Concurrent.pdf
- S. Ullrich, L. de Moura, "Counting immutable beans", 2019: https://arxiv.org/abs/1908.05647
- GCC optimisation options (unrolling is not part of `-O2`): https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
