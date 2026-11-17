---
name: kas3-compiler-engineering
description: Use when changing anything under compiler/ in kumikae-as3 (the AS3 front end, the IR and lowerer, the interpreter, the Godot extension, the language server, the API generator or their tests) — layering, C++17 rules, size limits, test-first workflow, what each change must run before it is done.
---

# Changing the AS3 compiler

Read `AGENTS.md`, `compiler/README.md` and the README of the folder you are changing first. The full rules
are in `docs/cpp-guidelines.md`; this is what gets broken most.

## The layers, and what each may include

```
src/ (front end)  ->  ir/ (typed IR, lowerer)  ->  vm/ (interpreter)  <-  godot_ext/ (Godot's side)
                           |                       lsp/ uses src/ only
                           +->  backend_cpp/ (the IR as C++ text)      runtime/ (what that C++ is built against) -> vm/
```

- `src/` knows nothing of the IR, the interpreter or Godot's headers. Godot's API reaches it as AS3
  declarations (`godot/godot.as`), like any other source.
- `ir/` includes `src/`. It includes no Godot header. Godot's names reach it through the name table
  (`godot/godot_names.txt`), by `godot_name(...)`.
- `vm/` includes `ir/ir.h`. It includes no Godot header and nothing of `src/`. It knows the engine only
  through the three interfaces in `vm/host.h`: `Output`, `EngineHandles`, `EngineSource`.
- `backend_cpp/` includes `ir/ir.h` only, like `vm/`. It writes C++ as text and includes no header of
  `vm/`, `runtime/` or Godot.
- `runtime/` includes `vm/` and `ir/`: the headers generated code is written against, and the host with no
  engine. Generated code includes `runtime/kas3.h` and nothing else of ours.
- `godot_ext/` is the only place that includes godot-cpp.
- A rule the checker and the lowerer must agree on is written once, in `src/` (see `whole_numbers.h`).

If a change needs an include that crosses these, the design is wrong: add an entry to the interface the
lower layer owns.

## Where a new thing goes

| New thing | Goes |
|---|---|
| A syntax form | `src/parser_*.cpp` by kind, its node in `src/ast.h`, its walk in `ast_walk.h`, its print in `ast_printer.cpp` |
| A type rule or a diagnostic | `src/typer_*.cpp` by what is typed; the message once, with `quoted()` |
| An IR operation | One line in the table in `ir/ir_ops.h` (name and shape letters), its case in `vm/interpreter_run.cpp`, its lowering in the `ir/lower_*.cpp` that owns that kind of code |
| An operation in native code | `backend_cpp/patterns.h` if it is one C++ statement, else the `function_*.cpp` that owns that kind; until then it runs by thunk. A fast path keeps the thunk for what it does not handle, and comes with a number from `tests/bench/run_native_bench.py` |
| Something asked of Godot | An entry on `EngineHandles` (done to a value the interpreter holds) or `EngineSource` (where a value comes from); `NoEngine` answers it; the recorder in `tests/test_vm.cpp` records it; `godot_ext/godot_host*.cpp` does it |
| Something about Godot's API as a whole | A rule in `godot/generate.py`, never a hand edit of `godot.as` or `godot_names.txt` |

## C++ rules that reviews keep finding

- C++17, `-fno-exceptions -fno-rtti -Wall -Wextra -Werror` (the extension alone has RTTI).
- A `.cpp` aims for 500 lines and fails the check above 800; a header 300 and 500; a function 60 and 120.
  `tools/check_style.py` runs with the tests. Split by what the code does, not by number.
- No banner comments (`// ---- x ----`). A comment says why, never what the next line does.
- `std::optional<T>` for a result that may be missing, not `bool` plus an out-parameter or a sentinel.
  `std::string_view` for text that is only read. Structured bindings, not `.first`/`.second`. `constexpr`
  tables, not `static const` built at start-up. `enum class`. `override`. Named casts.
- Nothing is allocated per instruction the interpreter runs, or per successful call into Godot. A
  `StringName` is made once and kept. No text is built on the path that succeeds.
- A value arriving from Godot into a typed register is checked against the type. Every failure is an AS3
  error with a message and a line.
- A change made for speed comes with numbers from `tests/bench/run_bench.py`, measured turn and turn about
  (`--against <other build>`).

## Workflow

1. A failing test first: a unit test in `tests/test_*.cpp`, a program in `tests/run/` with its `.out`, an
   IR golden file, or a case in `tests/godot_project/` compared with what GDScript gives.
2. Make it pass. Regenerate `godot.as` and `godot_names.txt` with the generator if its rules changed.
3. Before saying done, all of these, with the output read:

```sh
cmake --build compiler/build -j8 && ctest --test-dir compiler/build -j8          # 37 groups, `native` among them
cmake --build compiler/build-godot -j8 && python3 compiler/tests/run_godot.py compiler/tests
cmake --build compiler/build-san -j8 && ctest --test-dir compiler/build-san -j4  # before a merge
```

4. One kind of change per commit: a move with no edits, a rename, or a rewrite. A clean-up changes no test
   and no golden file.
5. Fix every finding of a review, of any size, in the same branch with a test. The project keeps no list
   of findings to fix later. Keep `docs/roadmap.md` true in the same branch.

## Do not

- Edit `godot/godot.as` or `godot/godot_names.txt` by hand.
- Add a dependency, or a second way of doing something the code already does one way.
- Commit anything under `spike/`, any build folder, or anything about a games console.
- Report a test as passing that you did not run.
