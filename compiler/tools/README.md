# `tools/`: the command line and the build's helpers

| File | Job |
|---|---|
| `kas3c.cpp` | The command line: `parse`, `check`, `run`, `ir`, `pack`, `cpp` and `lsp`. It reads arguments and calls the libraries. No rule of the language lives here. [The compiler's README](../README.md#the-kas3c-command-line) describes the commands |
| `check_style.py` | The size limits and the ban on banner comments from the [C++ guidelines](../../docs/cpp-guidelines.md). It runs with the tests, as `style` and `style_self_test` |
| `style_baseline.txt` | Files allowed over a limit. It's empty and must stay empty |
| `bench_recheck.cpp` | Times a re-check after an edit to a body, a signature, a member, and a method every file calls, and after a file joins and leaves the program. `--soak N` repeats renames and prints resident memory |
| `embed_runtime.py` | Writes the runtime's headers as C++ for the Godot extension, so that the extension can build a project's program library wherever it's installed. CMake runs it |
| `case_table.py` | Writes `vm/case_table.cpp`, the table behind `toUpperCase()` and `toLowerCase()`, from the Unicode data of the Python that runs it |
| `build_web_template.sh` | Builds a size-optimized single-threaded Godot 4.7 Web GDExtension export template (`dlink_enabled=yes`) from a Godot checkout (`--2d`, `--profile`) |

## Run the style check

```sh
python3 compiler/tools/check_style.py compiler
```

## Rules

- **A rule in the guidelines that a program can check goes into `check_style.py`,** with a case in its
  self-test, so that the guide and the check never disagree.
- **Don't add to `style_baseline.txt`.** Split the file or the function instead.
- **`bench_recheck.cpp` uses only the `Workspace` calls that older revisions have,** so that
  `tests/bench/recheck.py` can build it against an older revision and compare.
