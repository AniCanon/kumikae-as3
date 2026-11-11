# Stage 0 speed spike: desktop results

Measured 2026-11-11. Plan: `docs/superpowers/plans/2026-11-11-stage-0-speed-spike.md`.

## Verdict: the gate fails

Two of the six judged benchmarks pass. The translation rules were not tuned after seeing the numbers.

| Benchmark | Group | AS3-shaped | C# | GDScript | C# ÷ AS3 | Gate |
|---|---|---|---|---|---|---|
| `move_nodes` | gameplay | 758 | 816 | 2,004 | 1.08 | **fail** (needs 1.10) |
| `per_node_process` | gameplay | 1,409 | 2,467 | 3,353 | 1.75 | pass |
| `ray_queries` | gameplay | 3,987 | 1,988 | 2,198 | 0.50 | **fail** |
| `signal_traffic` | gameplay | 1,089 | 1,590 | 2,665 | 1.46 | pass |
| `nbody` | compute | 103 | 95 | 14,687 | 0.92 | **fail** (8% slower, 3% allowed) |
| `sieve` | compute | 634 | 590 | 10,748 | 0.93 | **fail** (7% slower, 3% allowed) |
| `array_conversion` | conversion | 512 | 456 | 1,106 | 0.89 | not judged |
| `empty` | baseline | 2 | 2 | 2 | | not judged |

Numbers are the median frame time in microseconds: the median over five runs of each run's median over 600
frames. A ratio above 1 means the AS3-shaped code is faster than C#.

Gate rules: on gameplay benchmarks AS3 must be at least 10% ahead of C# and its slowest run must beat C#'s
fastest; on compute benchmarks AS3 must be no more than 3% behind.

**The one-line reading:** the engine calling into scripts is a clear win over C#. Everything else is roughly
level with C#, and converting engine collections eagerly is a real cost.

## Spread and worst frames

Microseconds. "Range" is the lowest and highest of the five run medians. "p99" is the median of the five
runs' 99th-percentile frames. "Worst" is the single slowest frame in any run.

| Benchmark | AS3 range | AS3 p99 | AS3 worst | C# range | C# p99 | C# worst | GDScript p99 | GDScript worst |
|---|---|---|---|---|---|---|---|---|
| `move_nodes` | 754–768 | 851 | 1,037 | 808–818 | 898 | 925 | 2,150 | 2,922 |
| `per_node_process` | 1,368–1,422 | 1,567 | 1,928 | 2,451–2,545 | 2,631 | 3,527 | 3,680 | 4,559 |
| `ray_queries` | 3,890–4,034 | 4,082 | 4,285 | 1,971–2,030 | 23,811 | 26,929 | 2,290 | 2,455 |
| `signal_traffic` | 1,055–1,119 | 1,107 | 1,156 | 1,582–1,652 | 1,785 | 1,892 | 2,746 | 2,826 |
| `nbody` | 101–107 | 112 | 120 | 95–95 | 103 | 121 | 15,159 | 16,009 |
| `sieve` | 628–639 | 651 | 715 | 583–591 | 623 | 747 | 10,935 | 12,164 |
| `array_conversion` | 510–514 | 547 | 598 | 454–458 | 1,286 | 1,352 | 1,242 | 1,516 |

## What the numbers say

Measured:

- **Engine calling into scripts is where the design wins clearly.** 20,000 per-node `_process` calls are 1.75
  times faster than C#, and signal delivery is 1.46 times faster. Both margins are far outside the noise.
- **Scripts calling into the engine is between a tie and 10% ahead.** Reading and writing 20,000 positions
  is 8% ahead of C# in this run, with no overlap between the two ranges. The design document assumed a clear
  win here; the measurement does not support that.
- **`ray_queries` takes twice as long as C#** and is slower than GDScript. Every ray result is a Godot
  dictionary that the translation rules copy in full into an AS3 object before one field is read.
- **C# pays in pauses instead.** On `ray_queries` C#'s p99 frame is 23.8 ms against a 2.0 ms median; the
  AS3-shaped code's is 4.1 ms against 4.0 ms. A smaller version of the same pattern shows on
  `array_conversion`. The gate judges medians, so this does not count toward the verdict.
- **Pure math is slightly behind C#:** 8% on `nbody`, 7% on `sieve`. Both are roughly 100 to 150 times
  faster than GDScript.

Cautions on how far to trust each number:

- **`ray_queries`: the direction is robust, the size is not.** The conversion in the spike is naive: it
  builds a key list, copies each value twice, allocates a boxed object for every vector, and inserts into a
  hash map. A careful implementation would cost less. But C# (1,988) is close to GDScript (2,198), which
  suggests the engine's own ray work dominates, so eager conversion in any form is unlikely to reach the 10%
  target here. The AS3 source also does two lookups (`in`, then index) where C# and GDScript do one.
- **`sieve` and `nbody` are sensitive to emission details the rules do not pin.** The independent reviewer
  compiled the sieve loop outside the engine and saw 620 µs as written and 531 µs when the vector's length
  and data pointer were held in locals with every check and line store kept (reviewer's figures, not
  re-measured here). That alone would turn the sieve fail into a pass. Which form a real compiler emits is a
  design decision, so these two are best read as "within a few percent of C#, either side".
- **`move_nodes` has no engine floor.** The gate judges whole-frame time. If most of the frame is the
  engine's own work in `set_position`, no scripting language can be 10% ahead. That floor was not measured.

Likely causes, not verified by experiment:

- C#'s `ray_queries` pauses: garbage collection of 2,000 dictionary wrappers per frame.
- `sieve` and `nbody`: the rules store a line number and check bounds or null on every access. C#'s compiler
  removes bounds checks in loops bounded by the array's length and keeps no line bookkeeping.

## A correction made during review

The first measurement warmed up for 300 frames only. For C# `nbody` that was about 72 ms, too short for
.NET to finish optimising, and C# measured 240 µs: AS3 looked 2.4 times faster. An independent reviewer
caught it. Warm-up is now at least 300 frames **and** at least two seconds, and every number above comes
from the re-run. No other benchmark moved by more than a few percent.

## What was not measured

- Mobile and console. Desktop only.
- Allocation-heavy code and the cycle collector. No cycle collector exists in the spike.
- The interpreter backend.
- An engine floor (the same engine calls with no language overhead at all).
- Native registration of hot callbacks (the plan's Task 8). It applies only if `per_node_process` fails, and
  that benchmark passed.
- A less naive collection conversion, or the alternative of not converting.

Four benchmarks run under 1 ms per frame in the fastest language (`move_nodes`, `nbody`, `sieve`,
`array_conversion`). The plan said to scale those up; they were left as planned because their run-to-run
ranges are tight and the empty-frame overhead is 2 µs.

## Setting

- Apple M5 Pro, 18 cores, 64 GB, macOS 27.2, on mains power.
- Godot 4.7.2 stable, .NET edition, release export for macOS (universal template, arm64 slice), run with
  `--headless --fixed-fps 60`. .NET SDK 10.0.107, project targeting `net10.0`.
- godot-cpp 10.0.0-stable (507ed9d), Godot 4.7 API.
- AS3-shaped code: Apple clang 21, `-O2 -fno-exceptions -fno-rtti -ffp-contract=off`. Glue code around
  godot-cpp is built with RTTI on, because godot-cpp's casts need it.
- Warm-up of at least 300 frames and at least two seconds, then 600 measured frames; five runs per benchmark and language, languages interleaved.
- All three versions of every benchmark produce the same checksum to within one part in a million (C#'s
  single-precision arithmetic differs in the eighth digit on one benchmark), checked against the exported
  build before measuring.
- The exported app is re-signed ad hoc after export. The exporter's hardened-runtime signature makes macOS
  kill the .NET build at launch.

## Reproduce

```sh
export KAS3_GODOT=/path/to/Godot_mono.app/Contents/MacOS/Godot   # Godot 4.7.2 .NET, with its export templates
cmake -S spike -B spike/build -DCMAKE_BUILD_TYPE=Release && cmake --build spike/build -j
dotnet build spike/project/Spike.csproj -c Release
python3 spike/tools/gate.py
```
