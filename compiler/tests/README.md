# `tests/`: tests and benchmarks

This page says how to run each kind of test, what each suite holds, and how to add a test.

## Run the tests

Every suite is a `ctest` group. From the repository's root:

```sh
ctest --test-dir compiler/build --output-on-failure
ctest --test-dir compiler/build -R golden --output-on-failure
```

The second command runs one group. You can also run a suite's script directly:

```sh
python3 compiler/tests/run_golden.py compiler/build/kas3c compiler/tests
python3 compiler/tests/run_programs.py compiler/build/kas3c compiler/tests
```

## The suites

| Where | What it tests | Group |
|---|---|---|
| `test_*.cpp`, with `check.h` | One part of the compiler each, in C++ | One group for each file |
| `golden/ok`, `golden/errors` | The parser: a `.as` file, and the tree (`.ast`) or the errors (`.diag`) it must give | `golden` |
| `golden/names`, `golden/types` | Name resolution and types: a `.as` file, and the exact output of `kas3c check` with `--dump-names` or `--dump-types` | `golden` |
| `run/` | Whole programs on the interpreter: a `.as` file with its expected output (`.out`), its expected errors (`.err`), and its IR (`.ir`) where the IR is pinned | `programs` |
| `run/refused/` | One program for each construct the interpreter doesn't run yet, with the error it must give | `programs` |
| `run/stopped/` | Programs that never end. Each runs with a time limit of a tenth of a second and must be stopped by Error #1502, on the interpreter and as native code | `programs`, `native` |
| `run_native.py` | The programs of `run/` as native code: written by `kas3c cpp`, built with the system's C++ compiler, and held to the same `.out` and `.err` | `native` |
| `corpus/` | Realistic programs that must parse and check with no errors. `runs.txt` lists the ones that must also lower for Godot | `golden`, `corpus` |
| `godot_project/`, `godot_native/` | AS3 inside Godot. See [Tests inside Godot](#tests-inside-godot) | `godot_extension` |
| `test_cli.py`, `test_lsp_stdio.py` | `kas3c` as a program: arguments, exit codes, and the language server's framing | `cli`, `lsp_stdio` |
| `test_generated.py` | The shape of the C++ that `kas3c cpp` writes, where the shape is its speed | `generated` |
| `../godot/test_*.py` | The generator of Godot's API | `godot` |
| `../tools/check_style.py` | The size limits | `style`, `style_self_test` |
| `bench/` | Timings, not pass or fail. See [Benchmarks](#benchmarks) | |

Three properties hold across the suites:

- **Both back ends agree.** A program in `run/` runs on the interpreter and as native code, and must print
  the same output both ways.
- **Nothing leaks.** Every program must leave no object or string alive when it ends. Each program also
  runs a second time with hot reload's record of constructor arguments turned on, and must do the same. A
  set of programs is built as native code with `--remember` and run that way too.
- **Nothing crashes.** The parser test parses every byte-prefix of each corpus file. The query test runs
  each corpus file with one byte missing through every editor query.

Expected output for numbers, strings and evaluation order was produced by Node.js from a line-by-line
JavaScript copy of each program, because AS3 and JavaScript share those rules.

## Add a test

Write the test first, and watch it fail for the reason you expect.

| To test | Add |
|---|---|
| One function or rule | A function with `CHECK` or `CHECK_EQ` in the matching `test_*.cpp`, called from its `main` |
| The parser, names or types | A `.as` file under `golden/`. Then run `run_golden.py` with `--update` and read the generated file before you commit it |
| What a program does | A `.as` file in `run/` with its `.out`. A file named `err_*` expects an error |
| Native code | Nothing. A program in `run/` runs both ways. If the backend refuses it, `run_native.py --update` adds it to `native_skipped.txt` |
| Something inside Godot | A method in a `.as` script in `godot_project/`, a `show(name, ask(...), <what GDScript gives>)` line in its `.gd` driver, and the name in `run_godot.py` |
| An operation the backend does inline on a Godot value | A line in `godot_native/Probe.as` |
| Incremental checking | A named sequence in `test_incremental_named.cpp`, or a kind of edit in `oracle_edits.h` |

To update the golden files:

```sh
python3 compiler/tests/run_golden.py compiler/build/kas3c compiler/tests --update
```

Run `python3 compiler/tests/inference_controls.py` to build the local inference controls in a
temporary directory. Each run changes one compiler rule. Disabling a proof must change the
typed IR. Choosing the wrong storage must change the standalone program's output.

A golden file that records a bug is worse than no test. Read what `--update` wrote.

## Rules

- **A Godot test states what GDScript gives,** worked out by GDScript in the driver and not typed in by
  hand, wherever GDScript can do the same thing.
- **A clean-up changes no file here.** If a golden file changes, behavior changed.
- **Nothing here can hang.** A driver that waits for the engine quits on a frame count. No test waits on
  the wall clock where it can be given a clock of its own.
- **No project, device or personal identifier in a test project.** `export_presets.cfg` holds a placeholder
  team.

## Tests inside Godot

These need the extension library and Godot 4.7. Without either, the group skips itself and says so. To
build the library, give CMake a godot-cpp checkout, as
[the compiler's README](../README.md#build-and-test) shows.

```sh
python3 compiler/tests/run_godot.py compiler/tests compiler/build/kas3c
```

Godot is found on the `PATH`, or through `KAS3_GODOT`. The group runs alone, one Godot at a time, because
every run shares the test project.

- **`godot_project/`** holds AS3 scripts and GDScript drivers. A driver prints what the AS3 script gave
  beside what GDScript gives for the same thing, and the two must agree.
- **`script_editor.gd`** opens scripts in the headless editor and checks highlighting, completion across
  unsaved tabs, discarding edits, and saving from the editor and a worker. It edits a temporary copy of
  the test project.
- **`godot_native/`** holds a script that Godot runs on the interpreter and as native code. The two must
  print the same.
- **`play_godot.py`** tests the extension building a program library on Play: a project played twice,
  edited between runs, with the setting off, with no compiler, and with a compiler that fails.
- **`timeout.gd`** runs on the interpreter and from a native start: a script stuck in a loop is stopped.
- **Reload and restart** run from an interpreted start and from a native start.

Every other run turns the Play build off with `KAS3_NATIVE_PLAY=0`.

### The first editor scan

The first time Godot's editor scans a project that has an extension, and is told to quit at once
(`--import`, or `--editor --quit`), Godot can crash as it exits, after the scan has done its work. The
fault is in Godot: loading an extension that registers a class starts a documentation thread, and quitting
in the same frame frees what that thread still uses. Any extension that registers a class does the same.

`run_godot.py` avoids it by scanning with `godot_project/first_scan.gd`, which waits for that thread before
it quits. Later runs are clean.

## Sanitizers

Run the whole suite under the address and undefined-behavior sanitizers before a merge:

```sh
cmake -S compiler -B compiler/build-san -DKAS3_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build compiler/build-san -j && ctest --test-dir compiler/build-san --output-on-failure
```

In this build, the `native` group also builds the generated programs with the sanitizers, as one file each.

### The extension under the sanitizers

`build-san` builds no Godot extension. Build one into a folder of its own, so that it doesn't replace the
one the test project loads:

```sh
cmake -S compiler -B compiler/build-san-godot -DKAS3_SANITIZE=ON -DKAS3_GODOT_CPP=<path to godot-cpp> \
    -DKAS3_GODOT_OUTPUT=$PWD/compiler/build-san-godot/out
cmake --build compiler/build-san-godot --target kas3_godot kas3_godot_counts
```

Godot must load the sanitizer's runtime before the extension, and the Godot that is distributed refuses
to. On macOS, this works:

1. Copy Godot's app and sign the copy again, ad hoc, with an entitlements file that adds
   `com.apple.security.cs.allow-dyld-environment-variables` and
   `com.apple.security.cs.disable-library-validation` to the entitlements the app already has:

   ```sh
   codesign --force --deep -s - --entitlements <file> <copy of Godot.app>
   ```

2. Write a small script that starts the copy and sets these variables itself:

   | Variable | Value |
   |---|---|
   | `DYLD_INSERT_LIBRARIES` | The output of `/usr/bin/c++ -print-file-name=libclang_rt.asan_osx_dynamic.dylib` |
   | `ASAN_OPTIONS` | `detect_leaks=0:halt_on_error=1:detect_container_overflow=0`, because Godot itself isn't instrumented |
   | `UBSAN_OPTIONS` | `print_stacktrace=1` |

3. Copy `compiler/tests` beside copies of `runtime`, `vm`, `ir`, `src` and `godot`. Copy the
   sanitized extension from `out` to the scratch project's `godot_project/bin`. Copy the extension
   from `out/test_counts` to `godot_project/bin/test_counts` for the counter checks. Run the suite
   with the scratch Godot executable.

   ```sh
   KAS3_GODOT=<the script> python3 <copy>/tests/run_godot.py <copy>/tests compiler/build/kas3c
   ```

This run isn't part of `ctest`. It checks the extension's own code. It doesn't check Godot's code or a
program library's, which is built without the sanitizers.

## The incremental oracle

`test_incremental` applies random, seeded edits to one long-lived `Workspace`: words replaced, lines
deleted or pasted, characters typed, imports, `extends` clauses, files copied and removed. It runs over
generated projects, over `corpus/` with Godot's API, and over a project folder on disk.

After the edits, every file's errors, names and types, and the answers to completion, hover, definition
and signature help at sampled places, must equal those of a fresh, fully checked workspace.

- `test_incremental_named` holds named sequences for what random edits seldom reach, such as a cycle of
  interfaces or a chain of 1,000 classes.
- `test_server_incremental` does the same through the language server: batches of edits and requests, and
  checks that stop at random.

The suite runs a short version. To run longer:

```sh
KAS3_ORACLE_SEEDS=600 KAS3_ORACLE_EDITS=200 compiler/build/test_incremental
KAS3_ORACLE_SEEDS=300 KAS3_ORACLE_EDITS=200 compiler/build/test_server_incremental
```

A failure prints the seed and the last edits. `KAS3_ORACLE_FIRST_SEED=<seed> KAS3_ORACLE_SEEDS=1` repeats
it, and `KAS3_ORACLE_DUMP=<folder>` keeps the files and both answers. `KAS3_ORACLE_CORPUS=<folder>` runs
the oracle over another folder of AS3.

## The external corpus

`fetch_external.sh` downloads six open-source AS3 projects, about 190,000 lines, into `tests/external/`,
which git ignores. They're pinned by commit and aren't part of this repository.

```sh
compiler/tests/fetch_external.sh
python3 -I compiler/tests/run_external.py compiler/build/kas3c --check
```

The second command parses and checks them and prints the errors counted by message. A crash or a hang
fails. Errors don't, because these projects use Flash's library, namespaces and XML syntax.

## Benchmarks

Benchmarks print timings and don't pass or fail. A change made for speed comes with numbers measured
before and after, in alternating runs: `--against` runs two builds in turn so that load on the machine
hits both.

| Script | Measures |
|---|---|
| `bench/run_bench.py <build> [--against <other build>]` | The interpreter on the programs in `bench/` |
| `bench/web_bundle.py <export> [--against <other export>] [--brotli]` | Raw and compressed bytes for each file and the complete Web export |
| `bench/run_native_bench.py <build> [--against <other build>]` | Native code against the interpreter |
| `bench/languages/run.py` | The same workloads in Godot, in C#, GDScript and AS3. See [Performance](../../docs/performance.md#measure-it-yourself) |
| `bench/recheck.py <build> --against <other build>` | A re-check after an edit, in generated projects of 100,000 and 1,000,000 lines |
| `bench/completion.py <build> --against <other build>` | Completion in the language server: start-up, warm requests, and requests that follow an edit |

`bench/generate_project.py` writes the generated projects. `build/bench_recheck <folder>` times a re-check
in one build.

For by-name access in headless Godot, run either AS3 timing script with
`<build-godot> --untyped [--against <other build-godot>] [--runs N]`. Each build uses its own
`kas3c`, runtime headers and extension in the adjacent test project. The driver checks the result
after warm-up and after 20,000 rounds. Node2D and the attached script read, write and call by name.
Vector2 measures a read and a call together, and times the rejected write separately.
Use `--script-only` with a build that supports only the script case. The existing .NET export
runs the same two nodes through C#'s Get, Set and Call with `bench/languages/run.py --untyped-cs`.

To check each by-name operation separately, set `KAS3_GODOT_DOTNET` to the Godot 4.7 .NET
executable and run this command from the repository root:

```sh
python3 compiler/tests/bench/by_name_gate.py compiler/build-godot --against <main-build-godot> --gate
```

The gate alternates interpreted AS3, native AS3 and optimized C# on the same receiver fixtures.
A pilot measures each build and mode and sizes short batches for two milliseconds. Both sides
of an inferred/typed pair use the same iteration count, as do all builds and modes. A timed
window below half a millisecond is rejected. If a native control pair takes less than 0.05 ms
per 20,000 operations in the pilot, the gate builds a second image with the two functions in
opposite positions. Each round averages their times across both layouts. Longer rows keep the
original single-layout comparison. The gate checks the result of every batch. Each process
times four windows per row, twice in each direction. Short native control pairs alternate
individual batches in ABBA order within each window.
Each window warms for at least 20 ms and collects at least 40 ms of timed batches.
The process takes the median batch time in each window, then the median of the four windows,
normalized to 20,000 operations. The report gives the median and range across seven rounds.
Alternate rounds reverse build, mode and row order. The gate fails if a candidate median exceeds its
reference median by more than 3%. Each AS3 mode compares with the same row and mode on main.
Native compares with the interpreter, each untyped row compares with C# by name where
the API exists, and each inferred row compares with its explicitly typed control. The controls cover
a fresh Node2D and Timer, Number, Boolean, String, an AS3 class, Vector2 and Color.
The parameter-alias and typed-alias rows show the cost when a caller can supply a scripted node.
Timing stays outside CTest. The report prints main timings only when you pass its build
with `--against <main-build-godot>`. Add `--before-script <build-godot>` to interleave another
build for the script members. Its script enters through an AS3 wrapper that passes `this`.
Current builds receive the scripted node from Godot. C# uses `Get`, `Set` and `Call`.
Its Vector2 struct has no corresponding by-name operations.

The Timer control reads a freshly constructed object's wait time. The foreign-only rows read a
GDScript field and call its method. The computed reads cycle through two or eight GDScript fields,
or through `position`, `modulate` and `rotation` on a plain Node2D and one carrying GDScript.
AS3 indexes a String vector and C# indexes the corresponding interned StringNames. One row alternates
two engine method names and calls the last bound method. Another reads one name on alternating bare
and scripted nodes at the same site. The saved-call row reads a node's rotation method once, calls it
in a loop and checks the final rotation.

Four rows compare string operations with main. They join three strings, append a piece with and
without reading the growing length, and join two short strings into a 65,536-byte result. The custom
getter fixture reads String, Vector2 and Object fields and calls a script helper in the same loop.
Its call row alternates the helper and a node method. The main build probes each operation and reports
an unsupported row if AS3 raises the missing-property or missing-method error. On macOS the comparison
counts how many of eight distinct program images each build keeps loaded after close. Run
`ctest -R by_name_gate_rules` to exercise the timing rules without timing a run.

The tests also hold the benchmarks to the number of times native code falls back to the library
(`native_counts.txt`), so a change that loses a fast path fails a test.

## iOS

| Script | What it does |
|---|---|
| `test_web_export.py [godot]` | Exports a Web pack and verifies that only selected scripts enter its compiled program |
| `build_web.sh <path to godot-cpp>` | Builds the Web extension and runs the wasm32 pointer, vector storage and stack checks with the Emscripten SDK's Node.js |
| `build_ios.sh <path to godot-cpp>` | Builds the extension for iOS, for a device and the Simulator, as an `.xcframework` beside the test project. It needs Xcode, and signs nothing |
| `test_ios_native.py <booted test simulator UDID> <exported test .app>` | Installs and launches the test game, checks the execution mode in a fresh log and requires every scene check to pass. Use a disposable simulator. `--source-dir <project>` exercises Rider's hot reload setup and `--interpreted` checks an interpreter build |
| `test_ios_export_modes.py <godot>` | Exports the test game and checks device and Simulator objects, linker settings and legacy preset values. Requires the built extension, iOS runtime and installed export templates |
| `build_ios_simulator_template.sh <godot source> <out.zip>` | Builds a Godot iOS export template whose Simulator library runs on Apple silicon. Godot's published template has a Simulator library for Intel only |
