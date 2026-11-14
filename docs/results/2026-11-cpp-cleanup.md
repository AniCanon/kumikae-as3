# C++ clean-up: speed before and after

Measured with `compiler/tests/bench/run_bench.py` on the development machine (Apple silicon, macOS, release
build, headless Godot 4.7.2). Each figure is the median of 7 runs; the spread is the slowest run minus
the fastest. A change counts only if the median moved by more than the spread.

| Benchmark | What it times | Before (`main`, a7ef7be) | After the splits | After the speed changes so far |
|---|---|---|---|---|
| `check` | A full check of a generated project of 100,000 lines | 64.7 ms (spread 12.1) | 63.2 ms (1.2) | not affected |
| `recheck` | The language server checking it again after one edit | 20.3 ms (0.8) | 20.0 ms (0.3) | not affected |
| `run` | A 3-million-step loop with calls and fields in the interpreter | 209.9 ms (6.6) | 206.8 ms (6.5) | not affected |
| `engine` | 160,000 calls from AS3 into Godot | 50.3 ms (0.9) | 50.5 ms (2.7) | 42.3 ms (0.8) |

## What the figures say

- **The splits changed nothing,** as they should: the code moved, unedited.
- **Splitting `vm::Host` into three interfaces cost nothing measurable.** The engine benchmark is the one
  that would show an extra pointer hop per call.
- **Keeping Godot's names made calls into Godot about 16% faster.** The extension built a Godot string
  and looked its name up again on every call; it now does so once per name.
- **Gathering a call's arguments on the stack changed nothing measurable** (42.3 ms before and after). It
  stays because it replaced three copies of the same code with one.

## What the review caught, and the final figures

The table above was made by timing one build after the other. The independent review timed `main` and
the branch turn and turn about and found the interpreter's loop 3 to 4% slower, from one tidy-up commit:
a helper in a header for writing a Boolean result. The difference was inside the spread of the runs
above, so those runs could not see it. The helper is now a local function inside the loop, and
`run_bench.py --against` makes the turn-about comparison.

Branch against `main` (a7ef7be), 21 runs of each in turn, medians:

| Benchmark | `main` | Branch | Difference |
|---|---|---|---|
| `check` (41 runs) | 89.9 ms | 90.5 ms | +0.6%, inside the spread of 7.8 ms |
| `run` | 301.4 ms | 299.2 ms | -0.7%, inside the spread of 8.4 ms |
| `engine` (one after the other; 7 runs) | 50.3 ms | 42.1 ms | 16% faster |

(The machine was busier for these runs than for the first table, which is why the same benchmarks read
higher. Only figures taken together compare.)

## What the clean-up did not do

- `Place::part` in the lowerer is still a `std::shared_ptr`. A `Place` is copied, and its copies must
  share the value that was read once, so there is more than one owner.
- `engine_call` still takes a `std::function`, used only for `timer[name].add(f)`. It is not on a path
  that runs while a game runs.
- The interpreter makes a new handle for every value the engine gives back. That is where most of the
  time of a call into Godot now goes; changing it is a design change, for the native backend's stage.
