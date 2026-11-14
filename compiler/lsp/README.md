# `lsp/`: the language server

`kas3c lsp`: errors as you type, go to definition, find references, hover, outline and symbol search, for
any editor that speaks the Language Server Protocol. Setting up an editor is in
[editors/README.md](../../editors/README.md).

| Files | Job |
|---|---|
| `server` | Messages in and out, one handler per method |
| `workspace` | The project's files as the editor has them, and re-checking after an edit |
| `json`, `uri` | The wire format |

## Rules

- **It is the compiler, not a second one.** Everything it reports comes from `src/` (`query.h` for
  positions and references). It must never disagree with `kas3c check`.
- **Includes `src/` only.** No `ir/`, `vm/` or Godot header.
- **No dependency:** the JSON reader is here, on purpose.
- **A handler never blocks on the whole project** for an edit to one file.
- A new request has a test in `tests/test_lsp.cpp` and, when it concerns the wire, in
  `tests/test_lsp_stdio.py`.

Tests: `test_lsp`, `test_workspace`, `test_json`, `lsp_stdio`.
