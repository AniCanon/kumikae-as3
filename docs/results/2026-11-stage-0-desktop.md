# Stage 0 speed spike: desktop results

Measured 2026-11-11. Plan: `docs/superpowers/plans/2026-11-11-stage-0-speed-spike.md`.

## Verdict: the gate fails

Three of the six judged benchmarks pass. The translation rules were not tuned after seeing the numbers.

| Benchmark | Group | AS3-shaped | C# | GDScript | C# ÷ AS3 | Gate |
|---|---|---|---|---|---|---|
| `move_nodes` | gameplay | 807 | 860 | 2,036 | 1.07 | **fail** (needs 1.10) |
| `per_node_process` | gameplay | 1,434 | 2,424 | 3,307 | 1.69 | pass |
| `ray_queries` | gameplay | 3,974 | 2,004 | 2,220 | 0.50 | **fail** |
| `signal_traffic` | gameplay | 1,115 | 1,583 | 2,672 | 1.42 | pass |
| `nbody` | compute | 101 | 240 | 14,637 | 2.38 | pass |
| `sieve` | compute | 630 | 592 | 10,773 | 0.94 | **fail** (6% slower, 3% allowed) |
| `array_conversion` | conversion | 511 | 457 | 1,051 | 0.89 | not judged |
| `empty` | baseline | 2 | 3 | 2 | | not judged |

Numbers are the median frame time in microseconds: the median over five runs of each run's median over 600
frames. A ratio above 1 means the AS3-shaped code is faster than C#.

Gate rules: on gameplay benchmarks AS3 must be at least 10% ahead of C# and its slowest run must beat C#'s
fastest; on compute benchmarks AS3 must be no more than 3% behind.

## Spread and worst frames

Microseconds. "Range" is the lowest and highest of the five run medians.

| Benchmark | AS3 range | AS3 p99 | AS3 worst | C# range | C# p99 | C# worst |
|---|---|---|---|---|---|---|
| `move_nodes` | 762–936 | 932 | 3,399 | 815–1,336 | 993 | 1,958 |
| `per_node_process` | 1,430–1,451 | 1,772 | 1,957 | 2,328–2,577 | 2,687 | 2,904 |
| `ray_queries` | 3,880–4,052 | 4,065 | 4,450 | 1,976–2,048 | 23,906 | 26,872 |
| `signal_traffic` | 1,089–1,124 | 1,139 | 1,205 | 1,579–1,586 | 1,763 | 1,813 |
| `nbody` | 98–106 | 109 | 123 | 234–241 | 250 | 585 |
| `sieve` | 629–636 | 648 | 722 | 586–594 | 641 | 975 |
| `array_conversion` | 509–512 | 526 | 598 | 456–460 | 1,257 | 2,882 |

`move_nodes` also fails the noise rule: the AS3 and C# ranges overlap.

## What the numbers say

Measured facts:

- **Engine calling into scripts is where the design wins clearly.** 20,000 per-node `_process` calls are 1.69
  times faster than C#, and signal delivery is 1.42 times faster.
- **Scripts calling into the engine is close to a tie.** Reading and writing 20,000 positions is only 7%
  ahead of C#. The design document assumed a clear win here; the measurement does not support that.
- **Converting engine collections eagerly is expensive.** `ray_queries` takes twice as long as C# and is
  slower than GDScript. Every ray result is a Godot dictionary that the translation rules copy in full into an
  AS3 object before one field is read.
- **C# pays in pauses instead.** On `ray_queries` C#'s 99th-percentile frame is 23.9 ms against a 2.0 ms
  median; the AS3-shaped code's is 4.1 ms against 4.0 ms. The same pattern, smaller, shows on
  `array_conversion`. The gate judges medians, so this does not count toward the verdict.
- **Pure math splits.** `nbody` (object fields, floating point) is 2.4 times faster than C#. `sieve` (a tight
  loop over an integer vector) is 6% slower.

Likely causes, not verified by experiment:

- `move_nodes`: most of each frame is probably the engine's own work in `set_position`, which no scripting
  language can reduce, and C#'s call overhead is smaller than the design assumed.
- `sieve`: the rules store a line number and check bounds on every element access. C#'s compiler removes
  bounds checks in loops bounded by the array's length and keeps no line bookkeeping.
- C#'s `ray_queries` pauses: garbage collection of 2,000 dictionary wrappers per frame.

## What was not measured

- Mobile and console. Desktop only.
- Allocation-heavy code and the cycle collector. No cycle collector exists in the spike.
- The interpreter backend.
- Native registration of hot callbacks (the plan's Task 8). It applies only if `per_node_process` fails, and
  that benchmark passed.
- Whether counts under 1 ms per frame (`nbody`, `sieve` in the fast languages) add noise. Their run-to-run
  ranges above are tight.

## Setting

- Apple M5 Pro, 18 cores, 64 GB, macOS 27.2, on mains power.
- Godot 4.7.2 stable, .NET edition, release export for macOS (universal template, arm64 slice), run with
  `--headless --fixed-fps 60`. .NET SDK 10.0.107, project targeting `net10.0`.
- godot-cpp 10.0.0-stable (507ed9d), Godot 4.7 API.
- AS3-shaped code: Apple clang 21, `-O2 -fno-exceptions -fno-rtti -ffp-contract=off`. Glue code around
  godot-cpp is built with RTTI on, because godot-cpp's casts need it.
- 300 warm-up frames, then 600 measured frames; five runs per benchmark and language, languages interleaved.
- All three versions of every benchmark produce the same checksum, checked against the exported build before
  measuring.
- The exported app is re-signed ad hoc after export. The exporter's hardened-runtime signature makes macOS
  kill the .NET build at launch.

## Reproduce

```sh
export KAS3_GODOT=/path/to/Godot_mono.app/Contents/MacOS/Godot   # Godot 4.7.2 .NET, with its export templates
cmake -S spike -B spike/build -DCMAKE_BUILD_TYPE=Release && cmake --build spike/build -j
dotnet build spike/project/Spike.csproj
python3 spike/tools/gate.py
```
