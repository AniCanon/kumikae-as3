# `tests/`

Every group runs under `ctest`. What each is, in more words, is in the
[compiler's README](../README.md#tests).

| Where | What it tests | How to add one |
|---|---|---|
| `test_*.cpp`, `check.h` | One part each, in C++ | A function with `CHECK` / `CHECK_EQ`, called from `main` |
| `golden/` (`ok`, `errors`, `names`, `types`) | The front end: a `.as` file and the exact output expected | A new pair; `run_golden.py` finds it |
| `run/` | Whole programs under `kas3c run`: `.as`, `.out`, and `.ir` where the IR is pinned | A new pair; `err_*` files expect an error |
| `run_native.py`, `native_skipped.txt` | The programs of `run/` as native code: `kas3c cpp`, the system's C++ compiler, and the same `.out` and `.err` | Nothing to add: a program in `run/` is run both ways. A program the backend refuses goes on the list with `--update` |
| `godot_native/` | A script run in Godot by the interpreter and as native code (`run_godot.py`'s last part): the two must print the same. The test project's `Places.as`, `Emits.as` and `EmitsChild.as` are run with them: `places.gd` and `emits.gd` hold them to GDScript under the interpreter, the probe to the interpreter as native code | A line in `Probe.as`'s methods; every operation the backend does inline on one of Godot's values belongs there |
| `play_godot.py`, `test_play_cache.cpp` | Play building a project's program library by itself: the rules with no Godot (a build's key, its files' names, what is taken away, the command, the switch, the compiler found), and in Godot a project played twice (a build, then the library), edited between runs, with the switch off, with no compiler, with one that fails and one that writes junk, in the editor, with old builds lying about, with a build another run began (waited for while its builder is alive). `run_godot.py` runs `reload.gd` and `restart.gd` from a native start too, a reload asked for in the middle of native code (`native_guard.gd`), and a game from a pack. Every other run of Godot here has the build off (`KAS3_NATIVE_PLAY=0`, set by `run_godot.py`) | A method of `play_godot.Runner`, called from `play()` |
| `godot_project/` | AS3 inside Godot, compared with GDScript | A method in a `.as` script, a `show(name, ask(...), <what GDScript gives>)` line in its `.gd` driver, the name in `run_godot.py` |
| `corpus/`, `external/` | Real AS3 from elsewhere must still parse (`external/` is fetched, not committed). `run_corpus.py` says which corpus programs the interpreter takes and what stops the others; `run_godot.py` runs the ones in `corpus/runs.txt` inside Godot | A program that starts to run: its name in `corpus/runs.txt`, or in `corpus/waits.txt` with what it waits for |
| `bench/` | Timings, not pass or fail | `run_bench.py <build> [--godot tests] [--against <other build>]`; `run_native_bench.py <build> [--against <other build>]` for native code against the interpreter; `languages/run.py [--against <other extension project>]` for the same workloads in Godot C#, GDScript and AS3; `recheck.py <build> --against <other> --lines 100000 1000000`; `completion.py` |
| `test_incremental.cpp`, `test_incremental_named.cpp`, `test_server_incremental.cpp`, `oracle_project.h`, `oracle_edits.h`, `oracle_answers.h` | Incremental checking against a fresh whole check | A named sequence in `test_directed`, or an edit kind in `oracle_edits.h` |
| `test_cli.py`, `test_lsp_stdio.py` | `kas3c` as a program | |
| `build_ios.sh`, `build_ios_simulator_template.sh` | The extension for iOS, and a Simulator template from Godot's source | |

## Rules

- **The test is written first and seen to fail** for the reason it is meant to.
- **A Godot test states what GDScript gives,** worked out by GDScript in the driver, not typed in by hand,
  wherever GDScript can do the same thing.
- **A clean-up changes no file here.** If a golden file changes, behaviour changed.
- **Nothing here may hang:** a driver that waits for the engine quits on a frame count.
- No project, device or personal identifier in a test project (`export_presets.cfg` holds a placeholder team).

## The extension under the sanitizers

`build-san` builds no Godot extension. One can be built, into a folder of its own so that it does not take
the place of the one the test project loads:

```sh
cmake -S compiler -B compiler/build-san-godot -DKAS3_SANITIZE=ON -DKAS3_GODOT_CPP=<godot-cpp checkout> \
    -DKAS3_GODOT_OUTPUT=$PWD/compiler/build-san-godot/out
cmake --build compiler/build-san-godot --target kas3_godot
```

Godot must then load the sanitizer's runtime before the extension, and the Godot that is distributed
refuses it ("Sanitizer load violates platform policy"). What works on macOS:

1. A copy of Godot's app, signed again ad hoc with an entitlements file that has
   `com.apple.security.cs.allow-dyld-environment-variables` and
   `com.apple.security.cs.disable-library-validation` (and the three the app already has for its own
   use: `allow-jit`, `allow-unsigned-executable-memory`, `disable-executable-page-protection`):
   `codesign --force --deep -s - --entitlements <file> <copy of Godot.app>`. The installed app is left alone.
2. A small script that starts that copy, and sets the variables itself (`nice`, `sh` and the system's
   Python lose `DYLD_` variables on the way): `DYLD_INSERT_LIBRARIES` to what
   `/usr/bin/c++ -print-file-name=libclang_rt.asan_osx_dynamic.dylib` gives (the runtime of the compiler
   that built the extension), `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:detect_container_overflow=0`
   (Godot itself is not instrumented), `UBSAN_OPTIONS=print_stacktrace=1`, each with a `log_path`.
3. A copy of `compiler/tests` beside copies of `runtime`, `vm`, `ir`, `src` and `godot` (what generated
   code is built against), with the sanitized library in its `godot_project/bin`, and
   `KAS3_GODOT=<the script> python3 <copy>/tests/run_godot.py <copy>/tests compiler/build/kas3c`.

It is not part of `ctest`. It runs every driver, the reloads and the native probe with the extension's own
code checked: handles and their heads, the look for the object behind one, vectors handed to Godot in one
copy, listeners called. What it does not check: Godot's own code, and a program library's (generated
code is built without the sanitizers, and reads a handle's head itself); a handle's memory is kept for
the next handle when it is freed, so a handle used after it was let go of is not something the sanitizer
sees either: the tests of freed objects are what hold that.

## Completion checks and timings

`test_query` covers lexical scopes, imports, chained types and non-code contexts. `test_lsp` covers
capability negotiation, snippets, insertion/replacement ranges (including Unicode whitespace), and
argument help. `test_lsp_stdio.py` repeats protocol cases against the executable. `test_workspace`
checks cache counts across repeated requests, forty changed compilations, and document close.
Override wire tests compare the complete insertion text, including the closing brace and final caret,
with snippet support enabled/disabled and with CRLF source.
`test_statement_completion` checks language-block text, fresh compilation of generated defaults,
ordered/mirrored tab-stop spans, partial/full keywords, nested statements, expression/header exclusions,
empty recovery gaps, CRLF and suffix replacement. The stdio group negotiates language templates with
snippet support and completion-list defaults independently. Real client insertion, Tab navigation,
mirrored-name editing and immediate Undo run in the JetBrains sandbox probe.

`test_override_completion` covers qualified default constants, inaccessible references, independent
getter/setter overrides and final modifiers in either declaration order. It also covers inherited
accessor slots, shadowing during error recovery and empty-name completion. Accepted edits are checked
with a fresh compiler. `query_completion.h` shares completion fixtures and edit application with
`test_query`; it contains no production rules.
Empty-name cases include following methods, fields, modifiers and metadata with comments and CRLF.
`test_parser` checks that recovery retains those declarations and accepts valid multiline names.

## The incremental oracle

`test_incremental` applies random, seeded edits (words replaced, lines deleted or pasted, characters typed,
modifiers, `extends` and `implements`, imports, statements, undo, files copied and removed) to one
long-lived `Workspace`, over generated projects (three packages, inheritance, interfaces, accessors, names
that repeat across packages), over `corpus/` with Godot's API, and, one sequence in three, over a project
folder on disk edited both in the editor and on disk. Between edits the workspace is left part-checked: a
check that stops, a question about one file, or nothing. After them every file's errors, `dump_names`,
`dump_types`, and completion, hover, definition and argument help at sampled places must equal a fresh,
fully checked workspace's, with no rebuild at all: files copied in and removed, files moved between
packages, accessor halves overridden along a chain and a declaration file with mistaken tags are among the
edits. In `test_incremental_named`, named sequences cover what random edits seldom reach (an interface cycle cut elsewhere, the first
definition of a name changing, halves across files and along a chain, more than 100 errors, broken files
repaired, override defaults), and named tests a chain of 70 and of 1,000 classes, packages released, a
declaration file's tag errors said once, and a tree checked twice. `test_scope` pins how
many files an edit checks again. `test_server_incremental` does the same through `Server`: batches of edits
and requests, checks that stop at random, and what the editor ends up holding per file.

The suite runs a short version. Longer, and under `build-san` too:

```sh
KAS3_ORACLE_SEEDS=600 KAS3_ORACLE_EDITS=200 compiler/build/test_incremental
KAS3_ORACLE_SEEDS=300 KAS3_ORACLE_EDITS=200 compiler/build/test_server_incremental
KAS3_ORACLE_CORPUS=compiler/tests/external/flixel KAS3_ORACLE_SEEDS=6 compiler/build/test_incremental
```

A failure prints the seed and the last edits; `KAS3_ORACLE_FIRST_SEED=<seed> KAS3_ORACLE_SEEDS=1` repeats it
and `KAS3_ORACLE_DUMP=<folder>` keeps the files and both answers.

For alternating completion timings, build the unchanged revision separately before editing, then run:

```sh
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-baseline --runs 101
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-baseline --seconds 600
python3 compiler/tests/bench/completion.py compiler/build --against compiler/build-baseline --mode hub --lines 100000 1000000
python3 compiler/tests/bench/recheck.py compiler/build --against compiler/build-baseline --lines 100000 1000000
```

The driver reports cold initialization/first completion, warm requests, edit/request pairs and bursts
of twenty changes, with Godot loaded, in small and generated 100,000-line projects. The edited fixture
has 5,003 lines. Run without concurrent builds for comparable timings. RSS samples describe process
pages; the workspace tests provide separate evidence about live cache ownership. Keep check/run
controls from `bench/run_bench.py --against` alongside these results.

The completion driver also accepts `--mode body` (or `--valid-body`): it changes a comment inside a valid
method while requesting members at a dot; `--mode signature` changes a method's parameters in a generated
file another calls, and `--mode hub` the method every generated file calls. Its `program` timing follows
the edit with find references, which waits for every file. The baseline and candidate always receive
identical source and edit sequences.

Follow-up regressions cover fresh-check equivalence after body edits, imports moved between functions,
cross-file batch ordering, released parse snapshots, generated edits, stale document versions, queued
cancellation and partial-frame responsiveness. Windows URI fixtures run on the host platform; executing
the Windows runtime branches remains a separate validation requirement.
Diagnostic clearing tests reproduce an error followed by successive clean versions: even if the client
discards an earlier clear, the next version supplies another. In-process checks also cover unchanged
text at a new version, duplicate suppression, an unrelated file's check, close/reopen and clients
without version support. The stdio test waits for each actual publication before sending the next edit.
