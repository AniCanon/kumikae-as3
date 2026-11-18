# `lsp/`: the language server

`kas3c lsp` serves any editor that speaks the Language Server Protocol, over standard input and output. To
set up an editor, see [editors/README.md](../../editors/README.md).

**Depends on:** `src/` only. It includes no header from `ir/`, `vm/` or Godot. It has no third-party
dependency: the JSON reader is in this folder on purpose.

## What it answers

| Request | Answer |
|---|---|
| A file opening or changing | The file's errors: what `kas3c check` prints for the project |
| Go to definition | Where the name under the cursor is declared, in the project, the built-in library or Godot's API |
| Find references | Every use of it in the project's files. For a method, also the methods that override it and the one it overrides |
| Completion | Members after a dot, types where a type is written, override stubs after `override`, and otherwise the names in reach |
| Signature help | The declaration being called and the active parameter, including in unfinished and nested calls |
| Hover | The declaration, and the class or package it belongs to |
| Document symbols | The classes and members of the file |
| Workspace symbols | The project's definitions whose name contains the query, then Godot's classes and the built-in library's |

Rename and code actions aren't implemented. [The completion guide](../../editors/completion.md) describes
completion from the user's side.

## Files

| Files | Job |
|---|---|
| `server`, `server_queue`, `server_documents` | Dispatch, the queue of waiting messages, and document versions |
| `server_places`, `server_diagnostics`, `server_navigation`, `server_completion` | Positions and URIs in the editor's terms, the errors sent after a check, navigation, and completion with signature help |
| `workspace` | The project's files as the editor has them: an edit applied, and files checked when asked for |
| `channel` | Messages framed with `Content-Length`, over binary input and output |
| `json`, `uri`, `positions` | The wire format |
| `paths` | Paths as text, with one spelling on every system |

## How it works

- **The project** is the nearest folder at or above the opened one that has a `project.godot`, or else the
  opened folder. Every `.as` file under it is part of the program, except in folders whose name starts
  with a dot. An open file's text in the editor stands in for the file on disk.
- **An edit checks again only what it can change.** Only the changed file is parsed again. Its
  declarations are checked at once, with those of the files that read a declaration that changed. Bodies
  are checked one file at a time: first the file a request asks about, then the rest while the editor is
  idle. See [src/README.md](../src/README.md#checking-as-the-program-is-edited).
- **The server is single-threaded.** It answers a batch of messages first. Then it checks what is left,
  open files first, and asks the channel after each file whether a message is waiting. If one is, it
  stops, sends the errors it has so far, handles the message, and goes on from where it stopped.
- **A burst of typing is checked once.** Waiting document changes are applied together. A queued request
  that was cancelled, or whose document was closed, is skipped.
- **A request that a newer edit follows is still answered,** for the text it was asked about.
- **Errors are published for each file as it's checked,** with the document's version when the editor
  supports versions. A file that becomes clean gets an empty list, sent again for each newly checked
  version so that an editor typing ahead can't keep a stale error.
- **Positions** are counted in UTF-16 code units, or in bytes when the editor offers UTF-8.
- **Godot's API and the built-in library** are written, read-only, to `kas3c-declarations` in the system's
  temporary folder, so that go to definition has a file to open. No errors are reported for them.

### Completion on the wire

`server_completion.cpp` turns the compiler's candidates into LSP items. It negotiates three things with
the editor independently: snippets, separate insert and replace ranges, and a shared edit range for the
whole list.

- An editor without snippets receives plain names.
- An editor without insert and replace ranges receives one ordinary replacement range.
- An automatic import and an override stub carry an additional text edit.
- A list requested with nothing typed is marked incomplete when automatic imports were held back, so that
  typing a prefix asks again.

The server never parses a declaration to find a call's parameters. The compiler's candidates carry them.

## Rules

- **It's the compiler, not a second one.** Everything the server reports comes from `src/`. It must never
  disagree with `kas3c check`.
- **A handler never waits on the whole project** for an edit to one file. A request checks the bodies of
  the file it asks about. Find references is the exception: it checks every file.
- **A new request has a test** in `tests/test_lsp.cpp` and, when it concerns the wire, in
  `tests/test_lsp_stdio.py`.

## Limits

- A file the editor opens from outside the project, or from a folder whose name starts with a dot, isn't
  checked, unless no folder is open.
- A project of more than 20,000 `.as` files is read up to that number. The server says so in the editor
  and on standard error.
- A file is one file however the editor reaches it: through a symbolic link, in another letter case, or in
  another Unicode form of its name.
- On Windows, the path rules are tested with Windows paths on every system. The server hasn't been built
  or run on Windows itself.

## Tests

`test_lsp`, `test_workspace`, `test_json`, `lsp_stdio`, and the incremental oracles `test_incremental` and
`test_server_incremental`. See [tests/README.md](../tests/README.md#the-incremental-oracle).
