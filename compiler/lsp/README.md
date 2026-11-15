# `lsp/`: the language server

`kas3c lsp`: errors as you type, go to definition, find references, hover, completion, signature help, outline and symbol search, for
any editor that speaks the Language Server Protocol. Setting up an editor is in
[editors/README.md](../../editors/README.md).

| Files | Job |
|---|---|
| `server` | Messages in and out, one handler per method |
| `workspace` | The project's files as the editor has them, and re-checking after an edit |
| `json`, `uri`, `positions` | The wire format |

## Rules

- **It is the compiler, not a second one.** Everything it reports comes from `src/` (`query.h` for
  positions and references). It must never disagree with `kas3c check`.
- **Includes `src/` only.** No `ir/`, `vm/` or Godot header.
- **No dependency:** the JSON reader is here, on purpose.
- **A handler never blocks on the whole project** for an edit to one file.
- A new request has a test in `tests/test_lsp.cpp` and, when it concerns the wire, in
  `tests/test_lsp_stdio.py`.

Tests: `test_lsp`, `test_workspace`, `test_json`, `lsp_stdio`.

## Completion transport

`server_completion.cpp` maps compiler queries to LSP items and signature help. It negotiates snippets,
insert/replace edits and completion-list edit defaults independently. Positions use the server's
existing UTF-8/UTF-16 conversion. A client without snippets receives plain names; a client without
insert/replace support receives one ordinary replacement range. Shared edit ranges are sent only to
clients advertising that default. Completion lists are complete for the current prefix.

The server does not parse declarations to discover call parameters. Structured compiler candidates
provide required parameter names; signature queries identify the innermost call. Callback positions
remain name-only. Query caches belong to `Workspace`'s current `Queries`; a recheck destroys them before
replacing the compilation. The synchronous loop still coalesces waiting document changes.

See [the user guide](../../editors/completion.md) and [measurements](../../docs/results/2026-11-completion.md).
