# Stage 0b: re-measurement under the version 2 rules

Measured 2026-11-11. Design and rules: `docs/superpowers/specs/2026-11-11-performance-redesign.md`.
Raw rows: `2026-11-stage-0b-desktop.json` beside this file.

## Verdict: the gate fails, narrowly

Seven of the nine judged benchmarks pass. The two that fail are ties with C#, not losses: version 2 is 1%
ahead on the median in both, but the runs overlap, and the gate requires no overlap.

The gate: version 2 must be ahead of C# on every judged benchmark, with its slowest run faster than C#'s
fastest. This definition was set by the designer after the first spike's numbers and has not been confirmed
by the owner.

| Benchmark | Version 2 | C# | C# ÷ v2 | v2 runs | C# runs | Gate |
|---|---|---|---|---|---|---|
| `move_nodes` | 615 | 686 | 1.12 | 599–623 | 670–999 | pass |
| `per_node_process` | 1,002 | 1,682 | 1.68 | 934–1,031 | 1,677–1,822 | pass |
| `ray_queries` | 1,339 | 1,354 | 1.01 | 1,296–1,348 | 1,314–1,364 | **fail** (overlap) |
| `signal_traffic` | 510 | 1,062 | 2.08 | 499–512 | 1,056–1,436 | pass |
| `nbody` | 60 | 63 | 1.05 | 59–60 | 63–64 | pass |
| `sieve` | 338 | 369 | 1.09 | 332–351 | 369–390 | pass |
| `saxpy` (holdout) | 207 | 291 | 1.41 | 202–209 | 281–292 | pass |
| `shapes` (holdout) | 106 | 107 | 1.01 | 102–106 | 97–123 | **fail** (overlap) |
| `alloc` (holdout) | 48 | 74 | 1.54 | 45–48 | 70–75 | pass |

Median frame time in microseconds: the median over five runs of each run's median over 600 frames. "Runs" is
the lowest and highest of the five run medians.

## Against the first translation, the floor, and GDScript

Same run, same units.

| Benchmark | Version 2 | Version 1 | Floor | GDScript |
|---|---|---|---|---|
| `move_nodes` | 615 | 613 | 602 | 1,405 |
| `per_node_process` | 1,002 | 1,046 | 990 | 2,253 |
| `ray_queries` | 1,339 | 2,636 | 1,331 | 1,513 |
| `signal_traffic` | 510 | 738 | 729 | 1,799 |
| `nbody` | 60 | 68 | 57 | 9,753 |
| `sieve` | 338 | 407 | 342 | 7,129 |
| `saxpy` | 207 | | | 11,629 |
| `shapes` | 106 | | | 8,217 |
| `alloc` | 48 | | | 6,397 |

- Version 2 sits within a few percent of the floor everywhere it has one. The AS3 semantics (null checks,
  bounds checks, error propagation, stack traces, reference counting) now cost almost nothing on these
  benchmarks.
- `signal_traffic` is 30% faster than the floor. That is not skipped work: version 2 receives signals through
  a native callable, and the floor was written before that existed and still connects by method name. The
  floor is not a floor for that benchmark.
- The three holdouts have no version 1 and no floor.

## Slow frames

Microseconds. p99 is the median of the five runs' 99th-percentile frames; worst is the slowest single frame.

| Benchmark | v2 p99 | v2 worst | C# p99 | C# worst |
|---|---|---|---|---|
| `move_nodes` | 665 | 833 | 853 | 1,624 |
| `per_node_process` | 1,069 | 1,488 | 1,815 | 2,461 |
| `ray_queries` | 1,392 | 1,439 | 15,957 | 18,030 |
| `signal_traffic` | 537 | 620 | 1,204 | 1,944 |
| `nbody` | 69 | 83 | 75 | 91 |
| `sieve` | 362 | 387 | 411 | 469 |
| `saxpy` | 230 | 1,299 | 314 | 620 |
| `shapes` | 125 | 139 | 122 | 150 |
| `alloc` | 57 | 71 | 400 | 433 |

On the two benchmarks that create garbage (`ray_queries`, `alloc`), C#'s slow frames are 5 to 12 times its
median; version 2's stay within a few percent of its median. The gate judges medians, so this does not count
toward the verdict. `saxpy` had one slow version 2 frame (1.3 ms) of unknown cause.

## The two failures

- **`ray_queries`.** Version 2 is at the floor (1,339 against 1,331), so there is nothing left to remove on
  this route. C# is at the same level because the engine's own work dominates: it builds a six-entry
  dictionary for every ray, and both languages then read one key from it. The engine has an internal query
  that fills a plain struct instead; code linked into the engine can call it, a plugin cannot.
- **`shapes`.** Calling an overridable method through the class's method table costs the same as C#'s virtual
  call. The version 2 rules do not include devirtualisation. A compiler that sees the whole program knows
  `Shape` has exactly three final subclasses and could test the class and run the method body inline; that
  optimisation was listed in the research but not put in the rules.

## What changed since the first spike

Rules, flags and gate were fixed in the redesign document before this run. The holdouts were specified, and
their GDScript and C# versions committed, before any version 2 code existed.

Measurement protocol, as specified there: all five languages in one run; language order rotated each
repetition; a fixed standalone workload timed before and after every repetition (baseline 386.6 µs; no
repetition drifted more than 5%, none discarded); warm-up of at least 300 frames and two seconds.

Absolute times are about 25% lower than in the first spike's document for every language. The cause is
unknown; the calibration workload was stable throughout this run, and all comparisons here are within the run.

## What was not measured

- Mobile and console.
- The cycle collector, and garbage with cycles in it. `alloc` creates acyclic garbage only.
- The interpreter.
- A floor for the holdouts, or one that uses native callables.
- Any of the reserve levers (linking into the engine, a per-frame driver, native classes).
- `array_conversion` and `empty` under version 2; neither is judged.

## Setting

Apple M5 Pro, macOS 27.2, mains power. Godot 4.7.2 .NET, release export, `--headless --fixed-fps 60`.
.NET SDK 10.0.107. Version 2 and floor: Apple clang 21,
`-O2 -fno-exceptions -fno-rtti -ffp-contract=off -fno-unroll-loops`. Version 1 keeps its original flags
(no `-fno-unroll-loops`). Every version of every benchmark produces the same checksum to within one part in
a million, checked on the exported build before measuring.

## Reproduce

```sh
export KAS3_GODOT=/path/to/Godot_mono.app/Contents/MacOS/Godot KAS3_REQUIRE_DOTNET=1
cmake -S spike -B spike/build -DCMAKE_BUILD_TYPE=Release && cmake --build spike/build -j
dotnet build spike/project/Spike.csproj -c Release
python3 spike/tools/gate.py
```
