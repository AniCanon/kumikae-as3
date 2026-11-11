# Stage 0 Speed Spike Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure, on desktop, whether hand-written C++ in the shape the AS3 native backend would emit beats Godot C# on gameplay code, before any compiler is built.

**Architecture:** A throwaway GDExtension (`kas3_spike`) presents hand-translated "AS3-shaped" C++ classes to Godot as scripts attached to nodes, through the script-language extension interface. The same seven benchmarks are written in GDScript, C#, and AS3-shaped C++; a parity check proves they do the same work, and a runner script measures frame times in a release export and prints a gate verdict.

**Tech Stack:** Godot 4.7.2 (.NET edition, so one binary runs all three languages), godot-cpp, CMake, C++17 with `-O2 -fno-exceptions -fno-rtti`, .NET 10 SDK, Python 3 (standard library only).

**Spec:** `docs/superpowers/specs/2026-11-11-kumikae-as3-design.md` (sections "How AS3 appears to Godot", "Godot API bindings", "Memory", "Performance gate", "Stages" item 0).

## Global Constraints

- Everything lives under `spike/` and is throwaway. No file outside `spike/` and `docs/` is created. Keeping any of it is a new decision for the owner.
- Godot 4.7 is the baseline. All three languages run in the same Godot 4.7.2 .NET binary.
- No C++ exceptions and no RTTI in the extension: compile with `-fno-exceptions -fno-rtti`. No `-ffast-math`, no flags a normal release export would not use.
- No JIT, no platform-specific code in the runtime or the benchmark classes.
- AS3 code runs on the main thread only; reference counts are plain (non-atomic).
- No console vendor material in this repository: no SDK names, tool names, or device results.
- The spike's C++ must pay every cost real generated code will pay: reference-count updates, null checks, line bookkeeping for release stack traces, and error propagation without C++ exceptions. The "Translation rules" below are binding; a faster benchmark that breaks one is a failed task.
- Commit messages carry no attribution lines.
- Machine state on 2026-11-11: `godot` on PATH is 4.7.2 standard with its export templates installed; `/Applications/Godot_mono.app` is 4.6.2 (too old); `cmake`, `dotnet` 10 and `clang++` are present; `scons` and `ninja` are not.

## Translation rules

These define "the shape the compiler would emit". Every AS3-shaped C++ class in `spike/benches/` follows them, and each has its AS3 source beside it in `spike/as3/`.

1. **Objects.** Every AS3 class instance is a heap object starting with `as3::Object { uint32_t refcount; const as3::Class *cls; }`. Fields are typed C++ members.
2. **References.** `as3::Ref<T>` retains on copy and releases on destruction. Parameters are borrowed (`T *`, no retain) unless stored. A local that receives a new or returned object owns it.
3. **Null checks.** Every member access or call through a nullable reference is preceded by `AS3_NULLCHECK(p)`, except on `this` and on a reference constructed in the same block.
4. **Errors.** `throw` sets `as3::pending_error` and returns. After every call that can throw (any call to an AS3 function that is not provably non-throwing, and every null or bounds check), the caller tests `AS3_PENDING()` and returns. `try`/`catch` is a test of the same flag.
5. **Line bookkeeping.** Each AS3 function opens an `as3::Frame` on a shadow stack (function name, file name). Before each statement that contains an operation that can throw, the generated code stores the AS3 line number into the frame (`AS3_LINE(n)`).
6. **Numbers.** `Number` is `double`. `int` is `int32_t` with wrapping arithmetic, `uint` is `uint32_t`. Passing to a Godot integer parameter widens to `int64_t`.
7. **Collections.** `Vector.<T>` is `as3::Vector<T>` (contiguous, bounds-checked on every index, `RangeError` on failure). `Array` is `as3::Array` of tagged values. Both are AS3 objects with headers. They convert to Godot `PackedVector2Array` / `Array` only at an engine call.
8. **Godot objects.** Held as a raw `GDExtensionObjectPtr` with a null check only (GDScript's release behaviour). Godot value types (`Vector2`) are plain structs on the stack.
9. **Engine calls.** `object_method_bind_ptrcall` through a `GDExtensionMethodBindPtr` fetched once at library load with `classdb_get_method_bind` and the hash from `extension_api.json`. No godot-cpp wrapper classes on any measured path. Vararg methods (`emit_signal`) use `object_method_bind_call` with Variants.
10. **Engine calling in.** Arrives at the script instance's `call` function with a `StringName` and Variant arguments. The instance resolves the name through a per-class table keyed by the `StringName`'s interned pointer, unboxes arguments, and calls the typed C++ method directly.
11. **Strings** are Godot `String`.

## Benchmarks

All run with `--headless --fixed-fps 60`, which gives a constant delta of 1/60 s and exactly one physics tick per frame. Each benchmark is a script on a root `Node2D` and exposes `checksum() -> float`.

| Name | Group | Work per frame |
|---|---|---|
| `move_nodes` | gameplay | Root holds 20,000 `Node2D` children. Root's `_process` reads each child's `position`, adds `Vector2(60, 30) * delta`, wraps x at 1000, writes it back. Checksum: sum of all `position.x`. |
| `per_node_process` | gameplay | 20,000 `Node2D` children each carry a script whose `_process(delta)` does `rotation += delta` and `position.x += 10 * delta`. Checksum: sum of all `rotation` plus `position.x`. |
| `ray_queries` | gameplay | 64 `StaticBody2D` circles (radius 20) on an 8×8 grid spaced 100. In `_physics_process`, 2,000 `intersect_ray` calls on the direct space state, reusing one `PhysicsRayQueryParameters2D`, ray `i` from `(-50, i % 800)` to `(850, (i * 7) % 800)`. Checksum: number of hits plus sum of hit `position.x`. |
| `signal_traffic` | gameplay | Script declares signal `hit(amount: int, pos: Vector2)`, connects it to its own `on_hit`, and emits it 20,000 times in `_process` with `(i, Vector2(i, 1))`. `on_hit` adds `amount + pos.x` to a total. Checksum: the total. |
| `nbody` | compute | Five bodies as class instances with `Number` fields (the classic n-body initial values), 2,000 steps of dt 0.01 per frame. Checksum: system energy. |
| `sieve` | compute | Sieve of Eratosthenes below 200,000 in a `Vector.<int>` per frame, counting primes. Checksum: prime count (17,984) times frames run. |
| `array_conversion` | conversion | Per frame, 20 times: build a 1,000-point `Vector.<Vector2>` and pass it to `Line2D.set_points`; build a 1,000-int `Array` and pass it to `set_meta("data", ...)`. Checksum: `Line2D.get_point_count()` plus the meta array's size. |

If the fastest language finishes a benchmark's frame in under 1 ms, scale that benchmark's count up for all three languages equally and record the new count in the results.

## Review Focus

- **Benchmarks that silently do different work.** Three implementations of one benchmark must produce the same checksum; a C++ version that skips work would win unfairly. Pinned by the parity check (Tasks 2, 3, 6).
- **JIT warm-up counted as steady state.** C# starts slow and speeds up; the first frames must not be measured. Pinned by the warm-up assertion in Task 2.
- **Run-to-run noise mistaken for a win.** One run proves nothing on a laptop. Pinned by the five-repetition rule and the noise test in Task 7.
- **An AS3 error thrown inside a callback the engine invoked.** It must surface as an AS3 stack trace with file and line, once, and the game must keep running. Pinned in Task 5.
- **The engine calling a method the AS3 class does not have** (every node gets `_input`, `_notification` and others probed). It must report "not found" cheaply, not crash. Pinned in Task 5.

---

### Task 1: Skeleton that loads in Godot

**Files:**
- Create: `spike/README.md`, `spike/CMakeLists.txt`, `spike/host/register.cpp`, `spike/project/project.godot`, `spike/project/kas3_spike.gdextension`, `spike/tools/godot_bin.py`, `spike/tests/test_load.py`, `.gitignore`
- Create (submodule): `spike/third_party/godot-cpp`

**Interfaces:**
- Produces: CMake target `kas3_spike` writing `spike/project/bin/libkas3_spike.<platform>.<arch>.dylib`; extension entry symbol `kas3_spike_init`; `godot_bin.py` function `godot_path() -> str` returning `$KAS3_GODOT` or failing with the message below.

**Prerequisite for the owner (cannot be done by the worker without permission):** install the Godot 4.7.2 .NET editor and its export templates, and set `KAS3_GODOT` to its binary. The worker asks before downloading anything.

- [ ] **Step 1: Write the failing test** `spike/tests/test_load.py`: runs `godot_path()` with `--headless --path spike/project --quit` and asserts exit code 0, stdout contains `kas3_spike loaded`, and `--version` output starts with `4.7.` and contains `mono`. When `KAS3_GODOT` is unset it must fail with exactly: `Set KAS3_GODOT to a Godot 4.7 .NET editor binary`.
- [ ] **Step 2: Run** `python3 spike/tests/test_load.py`. Expected: FAIL (no project).
- [ ] **Step 3: Add godot-cpp.** Dump the API from the target binary (`$KAS3_GODOT --headless --dump-extension-api` and `--dump-gdextension-interface`) into `spike/api/`. Add godot-cpp as a submodule at its newest revision that builds against that dump using its custom-API-file CMake option; record the chosen commit and option name in `spike/README.md`. godot-cpp is used for class registration only (rule 9).
- [ ] **Step 4: Write `CMakeLists.txt` and `register.cpp`.** Flags per Global Constraints. `kas3_spike_init` registers at scene level and prints `kas3_spike loaded`. `.gitignore` excludes `spike/build/`, `spike/project/bin/`, `spike/project/.godot/`, `spike/out/`.
- [ ] **Step 5: Run** `cmake -S spike -B spike/build -DCMAKE_BUILD_TYPE=Release && cmake --build spike/build -j` then the test. Expected: PASS.
- [ ] **Step 6: Commit** `spike: add extension skeleton that loads in Godot 4.7`.

### Task 2: Runner and GDScript benchmarks

**Files:**
- Create: `spike/project/runner.gd`, `spike/project/runner.tscn`, `spike/project/benches/gd/{move_nodes,per_node_process,per_node_child,ray_queries,signal_traffic,nbody,sieve,array_conversion}.gd`, `spike/tools/run_bench.py`, `spike/tests/test_runner.py`

**Interfaces:**
- Produces: command line `godot --headless --fixed-fps 60 --path spike/project -- --bench=<name> --lang=<gd|cs|as3> --mode=<verify|measure>`.
  - `verify`: 10 frames, prints one line `{"bench","lang","checksum"}` as JSON, exits 0.
  - `measure`: 300 warm-up frames, then 600 measured frames; prints one JSON line `{"bench","lang","frames":600,"median_us","p99_us","max_us","checksum"}`. Frame time is the wall time between consecutive runner `_process` calls (`Time.get_ticks_usec()`).
  - An unknown bench or lang prints `unknown bench: <name>` or `unknown lang: <name>` to stderr and exits 2.
- Produces: `run_bench.py` functions `run(bench: str, lang: str, mode: str, binary: str | None = None) -> dict` and `parity(bench: str, langs: list[str]) -> None` (raises `AssertionError` naming both checksums when they differ by more than 1e-6 relative).
- The runner loads `res://benches/<lang>/<bench>.<ext>` for `gd` and `cs`; `as3` is wired in Task 6.

- [ ] **Step 1: Write the failing tests** in `spike/tests/test_runner.py`:
  - `test_verify_prints_checksum`: `run("sieve", "gd", "verify")["checksum"] == 17984 * 10`.
  - `test_measure_shape`: `run("sieve", "gd", "measure")` has exactly the keys above and `frames == 600`.
  - `test_warmup_excluded`: set env `KAS3_SLOW_FIRST_FRAMES=300` (the runner sleeps 5 ms in each warm-up frame when set); assert `max_us < 5000` for `sieve`.
  - `test_unknown_bench_exits_2`: `--bench=nope` exits 2 with `unknown bench: nope` on stderr.
  - `test_nbody_energy`: checksum after 10 frames equals the value computed by the plain-Python n-body reference inside the test, within 1e-9 relative.
- [ ] **Step 2: Run** `python3 -m unittest spike/tests/test_runner.py`. Expected: FAIL.
- [ ] **Step 3: Implement `runner.gd` and `run_bench.py`.**
- [ ] **Step 4: Implement the seven GDScript benchmarks** exactly as the Benchmarks table specifies, fully typed.
- [ ] **Step 5: Run the tests.** Expected: PASS.
- [ ] **Step 6: Commit** `spike: add benchmark runner and GDScript benchmarks`.

### Task 3: C# benchmarks

**Files:**
- Create: `spike/project/Spike.csproj`, `spike/project/benches/cs/{MoveNodes,PerNodeProcess,PerNodeChild,RayQueries,SignalTraffic,NBody,Sieve,ArrayConversion}.cs`, `spike/tests/test_parity_cs.py`

**Interfaces:**
- Consumes: runner command line and `parity()` from Task 2.
- Produces: C# classes loadable as `res://benches/cs/<Name>.cs` with `public double Checksum()`; the runner maps `move_nodes` to `MoveNodes.cs` and calls `Checksum` for `cs`.

- [ ] **Step 1: Write the failing test:** for each of the seven benchmarks, `parity(bench, ["gd", "cs"])`.
- [ ] **Step 2: Run it.** Expected: FAIL (no C# project).
- [ ] **Step 3: Write `Spike.csproj`** (`Godot.NET.Sdk` matching the editor's version, `net10.0` or the version that SDK requires, `Release` optimizations on) **and the seven classes.** Write them the way a competent C# Godot developer would: cache `StringName`s, reuse the ray parameters object, use `[Signal]` and the generated typed event for `signal_traffic`. The baseline must be good C#, not a strawman.
- [ ] **Step 4: Run** `dotnet build spike/project -c Release` then the test. Expected: PASS.
- [ ] **Step 5: Commit** `spike: add C# benchmarks with parity check`.

### Task 4: AS3-shaped runtime core

**Files:**
- Create: `spike/runtime/as3rt.h`, `spike/runtime/as3rt.cpp`, `spike/runtime/as3_collections.h`, `spike/tests/runtime_tests.cpp`
- Modify: `spike/CMakeLists.txt` (static library `as3rt`, test executable `as3rt_tests` registered with CTest; neither links Godot)

**Interfaces:**
- Produces, in namespace `as3`:
  - `struct Class { const char *name; void (*destroy)(Object *); };`
  - `struct Object { uint32_t refcount; const Class *cls; };`
  - `void retain(Object *)`, `void release(Object *)` (calls `cls->destroy` at zero); `template <class T> class Ref`.
  - `struct Error { const char *type; const char *message; }`; `extern Error *pending_error;` `void throw_error(const char *type, const char *message);` `void clear_error();`
  - `struct Frame { const char *function; const char *file; int line; Frame *up; }` with constructor and destructor pushing and popping `as3::top_frame`.
  - `int capture_stack(char *buffer, int capacity)`: writes `\tat <function>() [<file>:<line>]` per frame, innermost first; called by `throw_error` so the trace is that of the throw site.
  - `const char *pending_trace()`.
  - Macros `AS3_FRAME(fn, file)`, `AS3_LINE(n)`, `AS3_NULLCHECK(p)` (throws `TypeError` with message `Error #1009: Cannot access a property or method of a null object reference.` and returns the enclosing function's zero value), `AS3_PENDING()`.
  - `template <class T> struct Vector : Object` with `length`, `at(int32_t)` (bounds-checked, `RangeError`), `push`; `struct Array : Object` holding `as3::Value` (tagged: undefined, null, int, Number, Boolean, Object).

- [ ] **Step 1: Write the failing tests** in `runtime_tests.cpp` (plain `main` with assertions, no framework):
  - `ref_counts`: two `Ref` copies give refcount 2; destroying both calls `destroy` exactly once.
  - `null_check_sets_error`: a function doing `AS3_NULLCHECK(nullptr)` returns 0 and leaves `pending_error->type` equal to `"TypeError"`.
  - `error_propagates_through_frames`: `a()` calls `b()` calls `c()` which throws at line 7; after return, `pending_trace()` equals `"\tat c() [t.as:7]\n\tat b() [t.as:4]\n\tat a() [t.as:2]\n"`.
  - `vector_bounds`: `at(5)` on a 3-element vector sets a `RangeError`; `at(2)` does not.
  - `int_wraps`: `int32_t` add of `2147483647 + 1` through the runtime's `add_int` yields `-2147483648`.
- [ ] **Step 2: Run** `cmake --build spike/build && ctest --test-dir spike/build`. Expected: FAIL to compile.
- [ ] **Step 3: Implement the runtime.** Header-inline everything on a hot path (`retain`, `release`, the macros, `Vector::at`).
- [ ] **Step 4: Run the tests.** Expected: PASS.
- [ ] **Step 5: Commit** `spike: add AS3-shaped runtime core`.

### Task 5: Script host

**Files:**
- Create: `spike/host/spike_language.{h,cpp}`, `spike/host/spike_script.{h,cpp}`, `spike/host/script_instance.{h,cpp}`, `spike/host/class_registry.{h,cpp}`, `spike/runtime/godot_calls.{h,cpp}`, `spike/as3/Probe.as`, `spike/benches/probe.cpp`, `spike/project/tests/host_test.gd`, `spike/tests/test_host.py`
- Modify: `spike/host/register.cpp`, `spike/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Task 4.
- Produces:
  - `struct as3::MethodEntry { const char *name; void (*thunk)(as3::Object *self, const GDExtensionConstVariantPtr *args, int64_t argc, GDExtensionVariantPtr ret); };`
  - `struct as3::ScriptClass { const char *name; const char *godot_base; as3::Object *(*create)(GDExtensionObjectPtr owner); const MethodEntry *methods; int method_count; const char *const *signals; int signal_count; };`
  - `void as3::register_class(const ScriptClass *)`, `const ScriptClass *as3::find_class(const char *name)`.
  - Godot class `SpikeScript` (extends `ScriptExtension`) with static method `SpikeScript.for_class(name: StringName) -> SpikeScript`; Godot class `SpikeLanguage` (extends `ScriptLanguageExtension`), registered with the engine at init and removed at shutdown.
  - `godot_calls.h`: `GDExtensionMethodBindPtr as3::bind(const char *cls, const char *method, int64_t hash)` and inline typed `ptrcall` helpers; `as3::report_uncaught()` which prints `<Type>: <message>` then the trace through Godot's error printing, then clears the error.
  - The script instance's `call` resolves names through a per-class table keyed by interned `StringName` pointer (rule 10). After every thunk it tests `AS3_PENDING()` and calls `report_uncaught()`.
  - `Probe` (AS3 source in `spike/as3/Probe.as`, translation in `probe.cpp`): `_ready()` sets `count = 0`; `_process(delta)` increments `count`; `get_count():int`; `explode():void` throws `Error("boom")` at line 12 of `Probe.as`.

- [ ] **Step 1: Write the failing test.** `host_test.gd` attaches `SpikeScript.for_class("Probe")` to a `Node`, runs 5 frames, prints `count=<get_count()>`, calls `explode()`, runs 1 more frame, prints `alive`, then calls `node.call("no_such_method")` and prints `survived`. `test_host.py` asserts stdout has `count=5`, `alive`, `survived`; stderr has `Error: boom` followed by `\tat Probe/explode() [Probe.as:12]` exactly once; exit code 0.
- [ ] **Step 2: Run it.** Expected: FAIL (`SpikeScript` unknown).
- [ ] **Step 3: Implement the registry, language, script, and instance.** Use `script_instance_create3` with `GDExtensionScriptInstanceInfo3`; check the exact member names against `spike/api/gdextension_interface.h`. Implement only the virtuals Godot needs to attach a script and call methods; property and inspector support is out of scope.
- [ ] **Step 4: Write `Probe.as` and its translation** following every Translation rule.
- [ ] **Step 5: Run the test.** Expected: PASS.
- [ ] **Step 6: Commit** `spike: host AS3-shaped classes as Godot scripts`.

### Task 6: AS3-shaped benchmarks

**Files:**
- Create: `spike/as3/{MoveNodes,PerNodeProcess,PerNodeChild,RayQueries,SignalTraffic,NBody,Sieve,ArrayConversion}.as`, `spike/benches/{move_nodes,per_node_process,ray_queries,signal_traffic,nbody,sieve,array_conversion}.cpp`, `spike/benches/binds.{h,cpp}`, `spike/tests/test_parity_as3.py`, `spike/tests/check_rules.py`
- Modify: `spike/project/runner.gd` (for `as3`, attach `SpikeScript.for_class(<Name>)`), `spike/CMakeLists.txt`

**Interfaces:**
- Consumes: `ScriptClass`, `register_class`, `bind`, ptrcall helpers, runtime macros.
- Produces: registered classes `MoveNodes`, `PerNodeProcess`, `PerNodeChild`, `RayQueries`, `SignalTraffic`, `NBody`, `Sieve`, `ArrayConversion`, each with `checksum` in its method table; `binds.cpp` holding every method bind used, fetched once at init.

- [ ] **Step 1: Write the failing tests.**
  - `test_parity_as3.py`: for each of the seven benchmarks, `parity(bench, ["gd", "as3"])`.
  - `check_rules.py`: a textual check over `spike/benches/*.cpp` that fails if any file includes a godot-cpp class header (`godot_cpp/classes/`), uses `try`, `throw`, `dynamic_cast` or `std::atomic`, or contains a function with `AS3_FRAME` but no `AS3_LINE`.
- [ ] **Step 2: Run both.** Expected: parity FAILS (`unknown lang` path), rules check passes vacuously.
- [ ] **Step 3: Write the eight `.as` files** as an AS3 programmer would write them against the Godot API (`extends Node2D`, `override public function _process(delta:Number):void`), with `[Signal]` metadata in `SignalTraffic.as`.
- [ ] **Step 4: Translate each to C++ by the Translation rules,** keeping a `// Foo.as:<line>` comment on each `AS3_LINE`. Worked example of the shape, for the inner loop of `MoveNodes._process`:

```cpp
// MoveNodes.as:14   var child:Node2D = children[i];
AS3_LINE(14);
GDExtensionObjectPtr child = self->children->at(i);      // bounds-checked
if (AS3_PENDING()) return;
// MoveNodes.as:15   var p:Vector2 = child.position;
AS3_LINE(15);
AS3_NULLCHECK(child);
Vector2 p = ptrcall_ret<Vector2>(binds::Node2D_get_position, child);
// MoveNodes.as:16   p = p + velocity * delta;
p.x += self->velocity.x * (float)delta;
p.y += self->velocity.y * (float)delta;
// MoveNodes.as:17   if (p.x > 1000) p.x -= 1000;
if (p.x > 1000.0f) p.x -= 1000.0f;
// MoveNodes.as:18   child.position = p;
AS3_LINE(18);
ptrcall_void(binds::Node2D_set_position, child, &p);
```

  The null check before line 18 is omitted only because line 15 already proved `child` non-null in the same block (rule 3). Apply the same reasoning everywhere and no further.
- [ ] **Step 5: Run** the build, `check_rules.py`, and the parity test. Expected: PASS.
- [ ] **Step 6: Commit** `spike: add AS3-shaped benchmarks with parity check`.

### Task 7: Measure in a release export and record the verdict

**Files:**
- Create: `spike/project/export_presets.cfg`, `spike/tools/gate.py`, `spike/tests/test_gate.py`, `docs/results/2026-11-stage-0-desktop.md`
- Modify: `spike/tools/run_bench.py` (add `matrix(binary: str, repetitions: int = 5) -> list[dict]`), `spike/README.md`

**Interfaces:**
- Consumes: `run()` from Task 2.
- Produces: `gate.py` function `verdict(rows: list[dict]) -> dict` and a command `python3 spike/tools/gate.py` that exports, measures, writes `spike/out/results.json`, prints a table, and exits 0 on pass, 1 on fail.
- Gate rules (from the spec, exact):
  - For each gameplay benchmark: the median of the AS3 runs' `median_us` is at most the C# median divided by 1.10, **and** the slowest AS3 run is faster than the fastest C# run.
  - For each compute benchmark: the AS3 median is at most the C# median times 1.03 (3% is the noise allowance for "no worse").
  - `array_conversion` and all `p99_us`/`max_us` values are reported, not judged.
  - `verdict` returns `{"pass": bool, "benches": {name: {"group", "as3_us", "cs_us", "gd_us", "ratio_cs_over_as3", "pass"}}}`.

- [ ] **Step 1: Write the failing tests** in `test_gate.py`, on synthetic rows:
  - `test_clear_win_passes`: AS3 1000 µs, C# 1200 µs on all gameplay benches, equal on compute: `pass` is true.
  - `test_nine_percent_fails`: AS3 1000, C# 1090 on `move_nodes`: that bench and the verdict fail.
  - `test_overlap_fails`: medians 1000 vs 1200 but one AS3 run at 1250 and one C# run at 1150: fail.
  - `test_compute_within_noise_passes`: AS3 1020, C# 1000 on `nbody`: pass; AS3 1040: fail.
  - `test_conversion_not_judged`: AS3 ten times slower on `array_conversion` does not change `pass`.
- [ ] **Step 2: Run** `python3 -m unittest spike/tests/test_gate.py`. Expected: FAIL.
- [ ] **Step 3: Implement `verdict`, `matrix`, and the command.** The command exports with `--export-release "macOS" spike/out/spike.app`, runs every benchmark in every language five times in `measure` mode against the exported binary, interleaving languages (gd, cs, as3, gd, cs, as3, ...) so thermal drift spreads evenly.
- [ ] **Step 4: Run the tests.** Expected: PASS.
- [ ] **Step 5: Run the real measurement** on mains power with other applications closed: `python3 spike/tools/gate.py`. Run the parity checks against the exported binary first; a parity failure there stops the measurement.
- [ ] **Step 6: Write `docs/results/2026-11-stage-0-desktop.md`:** machine and OS, Godot and .NET versions, godot-cpp commit, compiler and flags, the full table (median, p99, max per benchmark and language, ratios), the verdict, and a short list of anything surprising. Say plainly what was not measured: mobile, console, allocation and collection pauses, and the interpreter.
- [ ] **Step 7: Commit** `spike: record desktop speed gate results`.

### Task 8 (only if Task 7 fails on `per_node_process` or `signal_traffic`): native callback registration

The spec says: on failure, first try registering hot callbacks natively, then stop and take the numbers to the owner.

**Files:**
- Create: `spike/benches/per_node_native.cpp`; Modify: `spike/project/runner.gd` (new lang `as3n`), `spike/tools/gate.py` (report `as3n` beside `as3`), the results document.

**Interfaces:**
- Produces: a GDExtension class `PerNodeNative` (extends `Node2D`) registered through godot-cpp whose `_process` virtual calls the same AS3-shaped method body as `PerNodeChild`; language key `as3n` for `per_node_process` only.

- [ ] **Step 1: Write the failing test:** `parity("per_node_process", ["gd", "as3n"])`.
- [ ] **Step 2: Implement, run parity, re-run `gate.py`.**
- [ ] **Step 3: Add the `as3n` column and a paragraph on what this approach costs** (no script attachment, divergence from the interpreter) **to the results document.**
- [ ] **Step 4: Commit** `spike: measure native callback registration`, then stop and report to the owner. Do not start stage 1.
