# `tests/`

Every group runs under `ctest`. What each is, in more words, is in the
[compiler's README](../README.md#tests).

| Where | What it tests | How to add one |
|---|---|---|
| `test_*.cpp`, `check.h` | One part each, in C++ | A function with `CHECK` / `CHECK_EQ`, called from `main` |
| `golden/` (`ok`, `errors`, `names`, `types`) | The front end: a `.as` file and the exact output expected | A new pair; `run_golden.py` finds it |
| `run/` | Whole programs under `kas3c run`: `.as`, `.out`, and `.ir` where the IR is pinned | A new pair; `err_*` files expect an error |
| `godot_project/` | AS3 inside Godot, compared with GDScript | A method in a `.as` script, a `show(name, ask(...), <what GDScript gives>)` line in its `.gd` driver, the name in `run_godot.py` |
| `corpus/`, `external/` | Real AS3 from elsewhere must still parse (`external/` is fetched, not committed). `run_corpus.py` says which corpus programs the interpreter takes and what stops the others; `run_godot.py` runs the ones in `corpus/runs.txt` inside Godot | A program that starts to run: its name in `corpus/runs.txt`, or in `corpus/waits.txt` with what it waits for |
| `bench/` | Timings, not pass or fail | `run_bench.py <build> [--godot tests] [--against <other build>]` |
| `test_cli.py`, `test_lsp_stdio.py` | `kas3c` as a program | |
| `build_ios.sh`, `build_ios_simulator_template.sh` | The extension for iOS, and a Simulator template from Godot's source | |

## Rules

- **The test is written first and seen to fail** for the reason it is meant to.
- **A Godot test states what GDScript gives,** worked out by GDScript in the driver, not typed in by hand,
  wherever GDScript can do the same thing.
- **A clean-up changes no file here.** If a golden file changes, behaviour changed.
- **Nothing here may hang:** a driver that waits for the engine quits on a frame count.
- No project, device or personal identifier in a test project (`export_presets.cfg` holds a placeholder team).

## Completion checks and timings

`test_query` covers lexical scopes, imports, chained types and non-code contexts. `test_lsp` covers
capability negotiation, snippets, insertion/replacement ranges (including Unicode whitespace), and
argument help. `test_lsp_stdio.py` repeats protocol cases against the executable. `test_workspace`
checks cache counts across repeated requests, forty changed compilations, and document close.

For alternating completion timings, build the unchanged revision separately before editing, then run:

```sh
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-baseline --runs 101
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-baseline --seconds 600
```

The driver reports cold initialization/first completion, warm requests, edit/request pairs and bursts
of twenty changes, with Godot loaded, in small and generated 100,000-line projects. The edited fixture
has 5,003 lines. Run without concurrent builds for comparable timings. RSS samples describe process
pages; the workspace tests provide separate evidence about live cache ownership. Keep check/run
controls from `bench/run_bench.py --against` alongside these results.
