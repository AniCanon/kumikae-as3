# `tools/`

| File | Job |
|---|---|
| `kas3c.cpp` | The command line: `parse`, `check`, `ir`, `run`, `lsp`. It only reads arguments and calls the libraries; no rule of the language lives here |
| `check_style.py` | The size limits and the ban on banner comments from [the C++ guide](../../docs/cpp-guidelines.md). Runs with the tests (`style`, `style_self_test`) |
| `style_baseline.txt` | What is over a limit today. It is empty, and may only stay empty: do not add to it |
| `bench_recheck.cpp` | Times a re-check after one edit |

A new rule in the guide that can be checked by a program is added to `check_style.py` with a case in its
self-test, so that the guide and the check never say different things.
