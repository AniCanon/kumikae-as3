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
| proven | read | 30 / 19 / 7 / 35 / 9 / 0 | 26 / 16 / 6 / 36 / 10 / 5 |
| proven | write | 30 / 16 / 11 / 19 / 25 / 0 | 32 / 15 / 6 / 26 / 16 / 5 |
| proven | call | 36 / 22 / 9 / 9 / 24 / 0 | 37 / 18 / 8 / 11 / 25 / 0 |
| typed | read | 77 / 0 / 0 / 10 / 12 / 0 | 0 / 0 / 0 / 0 / 88 / 12 |
| typed | write | 63 / 0 / 0 / 2 / 35 / 0 | 0 / 0 / 0 / 0 / 93 / 6 |
| typed | call | 63 / 0 / 0 / 3 / 34 / 0 | 0 / 0 / 0 / 0 / 96 / 4 |
| mixed | read | 33 / 17 / 7 / 32 / 10 / 0 | 24 / 16 / 9 / 35 / 12 / 4 |
| mixed | write | 34 / 17 / 11 / 21 / 18 / 0 | 28 / 14 / 10 / 25 / 17 / 5 |
| mixed | call | 36 / 17 / 10 / 14 / 23 / 0 | 33 / 19 / 10 / 13 / 24 / 1 |

On the plain node, the native read takes 4.21 ms per 20,000 operations and the interpreter
takes 3.71 ms. Generated code enters a thunk, builds an interpreter frame, runs the dynamic
instruction, and converts its result back to Vector2. In the native read sample, 562 of the
796 samples cross the thunk from one instruction in the generated function. On Vector2 the
thunk and conversion dominate even though reading x requires only one component. Script
members still enter a frame to search the script class and move a field or call a function.
See the separate operation timings and ranges in the benchmark gate.

## Representation and guards

The front end checks your source with its declared types. A representation proof examines
function-local declarations after that check, follows writes by symbol identity, and requires
a dominating initializer. Compound assignments, loop targets, captures with unproven values
and reads before initialization keep dynamic storage. A typed register holds a proven value
where its representation preserves the dynamic tag. Operations that observe the tag, including
`typeof`, `is`, `as` and `delete`, keep their dynamic meaning. Function parameters and the
`arguments` array retain their declared calling convention.

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
fingerprint. Generated C++ keeps a cache at each site and checks its program generation before
using it. Reload gets a fresh generation. A cache hit still checks for a freed object.

The native path passes its operands to the same guarded operation directly. Only an operation
the guard declines enters the interpreter thunk. This removes the extra frame, argument copies
and result handoff visible in the native samples. The timing gate compares both execution modes
for every operation so a change in either path is visible.
