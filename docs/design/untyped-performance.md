# Untyped member access

When you write `target.position` on a value held as `*`, the receiver determines what the name
means. A script can supply a field or a getter. A foreign script can intercept a Godot property.
A freed node raises an error. A Vector2 held untyped is a copy, and writing its parts raises an
error even when its initializer tells the compiler that it holds a Vector2.

## Cost of dynamic dispatch

The following measurements use headless Godot 4.7.2 on arm64 and the benchmark in
`compiler/tests/bench/by_name`. Each cell gives percentages of main-thread samples inside the
outer AS3 call, in the order **dispatch / name lookup / script check / conversion / Godot / other**.
Each mode and operation has its own one-second sample at a requested one-millisecond interval.
The reference implementation resolves names through its shared class table and runs native
dynamic operations through interpreter thunks.

A sample counts once. The innermost recognizable lookup, script check or conversion takes its
descendants, including work inside Godot. Conversion includes copying and releasing handles.
The Godot column counts other samples that enter the engine. The dispatch column includes the
interpreter loop, thunk and property machinery. Inlining and stripped engine symbols limit the
separation of these costs. In particular, time inside Godot includes validation and bridging
as well as the member itself.

| Receiver | Operation | Interpreter | Native |
|---|---|---|---|
| node | read | 34 / 17 / 7 / 32 / 10 / 0 | 24 / 13 / 7 / 40 / 12 / 3 |
| node | write | 35 / 17 / 10 / 19 / 19 / 0 | 29 / 17 / 7 / 25 / 15 / 6 |
| node | call | 38 / 19 / 11 / 10 / 22 / 0 | 37 / 19 / 9 / 13 / 21 / 0 |
| inherited | read | 29 / 28 / 8 / 29 / 6 / 0 | 24 / 26 / 6 / 31 / 7 / 6 |
| inherited | write | 29 / 28 / 11 / 18 / 15 / 0 | 25 / 26 / 9 / 22 / 13 / 5 |
| inherited | call | 30 / 36 / 9 / 7 / 18 / 0 | 33 / 31 / 10 / 10 / 16 / 0 |
| gdscript | read | 29 / 14 / 5 / 26 / 25 / 0 | 22 / 16 / 6 / 30 / 23 / 3 |
| gdscript | write | 26 / 19 / 6 / 17 / 32 / 0 | 27 / 12 / 5 / 20 / 31 / 4 |
| gdscript | call | 31 / 15 / 6 / 7 / 40 / 0 | 32 / 14 / 6 / 10 / 38 / 0 |
| script | read | 62 / 3 / 20 / 15 / 0 / 0 | 49 / 2 / 21 / 27 / 0 / 2 |
| script | write | 55 / 3 / 21 / 21 / 0 / 0 | 49 / 3 / 21 / 25 / 0 / 2 |
| script | call | 59 / 8 / 15 / 18 / 0 / 0 | 60 / 5 / 14 / 20 / 0 / 1 |
| value | read | 52 / 10 / 0 / 34 / 3 / 0 | 51 / 10 / 0 / 33 / 4 / 2 |
| value | call | 57 / 9 / 0 / 31 / 3 / 0 | 53 / 9 / 0 / 35 / 3 / 1 |
| parameter alias | read | 30 / 19 / 7 / 35 / 9 / 0 | 26 / 16 / 6 / 36 / 10 / 5 |
| parameter alias | write | 30 / 16 / 11 / 19 / 25 / 0 | 32 / 15 / 6 / 26 / 16 / 5 |
| parameter alias | call | 36 / 22 / 9 / 9 / 24 / 0 | 37 / 18 / 8 / 11 / 25 / 0 |
| typed parameter alias | read | 77 / 0 / 0 / 10 / 12 / 0 | 0 / 0 / 0 / 0 / 88 / 12 |
| typed parameter alias | write | 63 / 0 / 0 / 2 / 35 / 0 | 0 / 0 / 0 / 0 / 93 / 6 |
| typed parameter alias | call | 63 / 0 / 0 / 3 / 34 / 0 | 0 / 0 / 0 / 0 / 96 / 4 |
| mixed | read | 33 / 17 / 7 / 32 / 10 / 0 | 24 / 16 / 9 / 35 / 12 / 4 |
| mixed | write | 34 / 17 / 11 / 21 / 18 / 0 | 28 / 14 / 10 / 25 / 17 / 5 |
| mixed | call | 36 / 17 / 10 / 14 / 23 / 0 | 33 / 19 / 10 / 13 / 24 / 1 |

On the plain node, the native read takes 4.21 ms per 20,000 operations and the interpreter
takes 3.71 ms. Generated code enters a thunk, builds an interpreter frame, runs the dynamic
instruction, and converts its result back to Vector2. In the native read sample, 562 of the
796 samples cross the thunk from one instruction in the generated function. On Vector2 the
thunk and conversion dominate even though reading x requires only one component. Script
members still enter a frame to search the script class and move a field or call a function.
On Vector2, the reference read takes 1.57 ms interpreted and 1.81 ms native. Calling its
method takes 1.66 ms and 1.93 ms. On the scripted node, writing the script field takes 1.24 ms
and 1.53 ms. Each pair uses the same 20,000-operation workload.

## Representation and guards

The front end checks your source with its declared types, then follows each local by symbol.
It considers an initializer or a standalone first assignment in a statement block. Reads must
follow that write in the same straight-line region, which can include nested blocks. A write
inside an `if`, loop or `try` block proves only the part of that block after the write.
Every later assignment must have the same checked type. Closure capture and indirect writes
such as `for each` keep the local dynamic.

The proof recognizes literals, construction, typed parameters and locals, fields, getters and
typed call results. You can return the local or copy it to a declaration of the same type.
A public member can bind when its result goes into an exactly typed declaration or return, or
when you discard a method result. Writes and method arguments need an exact value. Member access
on an AS3 class needs a known concrete construction or a final class. Writes to the parts
of an untyped Godot math value stay dynamic. The IR fixtures compare Numbers, Booleans, strings,
AS3 objects, Vector2, Color and a newly constructed Godot resource with explicit declarations.

A freshly constructed engine object can bind a public member while it is known to be detached.
For example, `var g:* = new Gradient(); return g.getPointCount();` can bind in a function returning
`int`. The proof treats an unfamiliar engine operation as capable of publishing the receiver
or attaching a script. It accepts that operation as the last, non-repeating access. A local
sequence or loop of Node2D transforms can stay detached across accesses. Calling a callback
between construction and access or publishing the object prevents the proof.

A parameter declared as `Node2D` can already carry a foreign script. Its untyped alias must
dispatch through that script even for a built-in property. The parameter-alias fixture overrides
`position`, counts reads and writes, and changes the type on the fifth read. Both execution modes
observe the override once per access and raise the conversion error.

Operations that observe the dynamic tag, including `typeof`, `is`, `as` and `delete`, keep their
dynamic semantics. Parameters and the `arguments` array keep their declared calling convention.

A receiver's storage type alone does not prove which script is attached. At a dynamic site the
runtime validates the live receiver and checks its script before selecting a field or engine
binding. Scalar fields with matching tags can move directly. A setter, a callable returned by
a getter, a conversion with side effects, or an argument that the entry cannot accept takes
the ordinary path. A refusal happens before any observable operation so a setter or call runs once.

## Site lifetime

Each site has room for four receiver shapes. A shape identifies the Godot class or value type
and the script class. An entry holds resolution metadata. It takes its receiver and arguments
from the current operation. Additional shapes use the shared lookup table. An interpreter owns
its side table separately from the immutable module, so warming a site cannot change the module
fingerprint. The host retains names resolved in its declaration table. A computed miss returns a
shared missing-member description without retaining the key. Generated C++ keeps a cache at each site and checks its program generation before
using it. AS3 execution and instance registration run on Godot's main thread. A worker call is
refused before it enters AS3, and a worker free is queued for the main thread. The instance
lookup, host bindings and interpreter overflow table follow the same thread contract. A native
site uses static storage in the program image so dyld can unload it when Play closes it.
Reload gets a fresh generation. A cache hit still checks for a freed object.

For an engine receiver, generated code passes its operands to `named_access`, which calls the host interpreter's `access_named`. A decline continues through
the ordinary generated dynamic operation and its thunk where needed. A handled operation avoids the extra frame, argument copies
and result handoff visible in the reference samples. The timing gate compares both execution modes
for every operation so a change in either path is visible.

## Cost after a site warms

With the same fixtures and the unfused interpreter, GDScript read has 816 samples, mixed read
has 824, and mixed write has 809. The six-category breakdowns are 51 / 0 / 4 / 18 / 27 / 0,
60 / 0 / 12 / 19 / 8 / 0 and 52 / 0 / 16 / 10 / 22 / 0 respectively. The additional conversion
work on reads includes copying the getter result into a typed local.

The mixed fixture passes eight receivers from six engine classes through one instruction. For
each of read, write and call, counters record four inline fills and four overflow fills during
warmup. The next 20,000 accesses divide evenly between the inline cache and the shared table.
They create zero entries and zero name strings. Each overflowing site keeps the address of its
shared table and probes it with numeric identities.

The interpreter keeps the current frame and register base across scalar instructions. A named
math read followed by a single-use conversion can write directly into the typed destination
when its existing handle and the actual result have the expected type. Native code uses the
same instruction analysis. A different result goes through ordinary AS3 conversion using the
value the getter already returned. Foreign property reads still call Godot's named getter once.
A math value passed directly to a setter can borrow its handle for that call.

The table uses the six sampling categories above. The last two columns include Godot frames
nested inside receiver checks and conversions. Each AS3 cell comes from a separate sample of
the operation, with 709 to 757 samples inside the outer AS3 call. Percentages are rounded.
C# uses its by-name API on the same receivers.

| Receiver | Operation | Interpreter | Native | In Godot, interpreter / native (%) | C# in Godot |
|---|---|---|---|---:|---:|
| node | read | 66 / 0 / 24 / 0 / 11 / 0 | 39 / 0 / 32 / 0 / 14 / 15 | 22 / 30 | 32% |
| node | write | 49 / 0 / 20 / 1 / 30 / 0 | 28 / 0 / 24 / 0 / 36 / 12 | 39 / 48 | 41% |
| node | call | 41 / 0 / 18 / 4 / 37 / 0 | 23 / 0 / 25 / 0 / 43 / 10 | 45 / 56 | 33% |
| inherited | read | 79 / 0 / 7 / 0 / 15 / 0 | 50 / 0 / 11 / 0 / 17 / 22 | 16 / 19 | 44% |
| inherited | write | 54 / 0 / 6 / 1 / 39 / 0 | 34 / 0 / 8 / 0 / 44 / 13 | 39 / 46 | 53% |
| inherited | call | 48 / 0 / 5 / 6 / 40 / 0 | 26 / 0 / 6 / 0 / 55 / 13 | 41 / 56 | 34% |
| gdscript | read | 45 / 0 / 5 / 18 / 33 / 0 | 17 / 0 / 7 / 22 / 42 / 12 | 38 / 52 | 40% |
| gdscript | write | 37 / 0 / 6 / 1 / 56 / 0 | 18 / 0 / 6 / 0 / 64 / 13 | 58 / 64 | 50% |
| gdscript | call | 40 / 3 / 3 / 7 / 47 / 0 | 26 / 5 / 3 / 5 / 57 / 4 | 48 / 58 | 35% |
| script | read | 81 / 0 / 8 / 11 / 0 / 0 | 32 / 0 / 22 / 10 / 0 / 36 | 2 / 4 | 40% |
| script | write | 79 / 0 / 12 / 9 / 0 / 0 | 35 / 0 / 25 / 0 / 0 / 40 | 2 / 6 | 39% |
| script | call | 79 / 0 / 5 / 16 / 0 / 0 | 23 / 0 / 17 / 10 / 0 / 49 | 1 / 2 | 49% |
| value | read | 67 / 0 / 3 / 23 / 7 / 0 | 41 / 0 / 5 / 28 / 10 / 17 | 9 / 15 | n/a |
| value | call | 68 / 0 / 4 / 22 / 6 / 0 | 40 / 0 / 4 / 28 / 11 / 18 | 8 / 16 | n/a |
| alias | read | 65 / 0 / 24 / 0 / 11 / 0 | 39 / 0 / 29 / 0 / 15 / 16 | 24 / 29 | 26% |
| alias | write | 46 / 0 / 19 / 1 / 33 / 0 | 28 / 0 / 24 / 0 / 34 / 14 | 40 / 46 | 43% |
| alias | call | 42 / 0 / 18 / 6 / 34 / 0 | 22 / 0 / 24 / 0 / 41 / 12 | 42 / 52 | 36% |
| aliasTyped | read | 61 / 0 / 0 / 13 / 26 / 0 | 0 / 0 / 0 / 0 / 86 / 14 | 34 / 86 | 32% |
| aliasTyped | write | 46 / 0 / 0 / 0 / 54 / 0 | 0 / 0 / 0 / 0 / 94 / 6 | 54 / 94 | 41% |
| aliasTyped | call | 46 / 0 / 0 / 0 / 54 / 0 | 0 / 0 / 0 / 0 / 97 / 3 | 54 / 97 | 33% |
| proven | read | 70 / 0 / 0 / 10 / 20 / 0 | 0 / 0 / 0 / 0 / 84 / 16 | 26 / 84 | 32% |
| proven | write | 60 / 0 / 0 / 0 / 40 / 0 | 0 / 0 / 0 / 0 / 95 / 5 | 40 / 95 | 40% |
| proven | call | 52 / 0 / 0 / 0 / 48 / 0 | 0 / 0 / 0 / 0 / 97 / 3 | 48 / 97 | 31% |
| typed | read | 66 / 0 / 0 / 10 / 24 / 0 | 0 / 0 / 0 / 0 / 82 / 18 | 30 / 82 | 32% |
| typed | write | 61 / 0 / 0 / 0 / 39 / 0 | 0 / 0 / 0 / 0 / 92 / 8 | 39 / 92 | 40% |
| typed | call | 53 / 0 / 0 / 0 / 47 / 0 | 0 / 0 / 0 / 0 / 94 / 5 | 47 / 94 | 32% |
| mixed | read | 59 / 0 / 18 / 11 / 12 / 0 | 38 / 0 / 21 / 12 / 16 / 13 | 26 / 29 | 47% |
| mixed | write | 51 / 0 / 17 / 7 / 25 / 0 | 29 / 0 / 23 / 7 / 28 / 13 | 35 / 41 | 50% |
| mixed | call | 47 / 1 / 14 / 9 / 29 / 0 | 25 / 0 / 17 / 9 / 40 / 9 | 38 / 52 | 35% |

On a foreign getter, the interpreter can move Godot's returned Variant into an existing handle
of the expected math type. A bare setter with one exact math argument passes that value directly
to the validated method bind. A foreign setter still calls Godot's named setter. The gate measures
GDScript reads and mixed writes separately so both paths stay visible.

Rebuild generated native libraries for runtime layout `0x4b41533300000012`.
The loader checks the layout before using a site's runtime entry points.
