# `lsp/`: the language server

`kas3c lsp`: errors as you type, go to definition, find references, hover, completion, signature help, outline and symbol search, for
any editor that speaks the Language Server Protocol. Setting up an editor is in
[editors/README.md](../../editors/README.md).

| Files | Job |
|---|---|
| `server`, `server_queue`, `server_documents` | Framed transport, dispatch, bounded lookahead and document versions |
| `server_places`, `server_diagnostics`, `server_navigation`, `server_completion` | Positions and URIs in the editor's terms; the errors sent after a check; definition, references, hover and symbols; completion and signature help |
| `workspace` | The project's files as the editor has them: an edit applied, a file's bodies checked when asked for, the rest checked a file at a time |
| `channel` | Messages framed with `Content-Length`, over binary input and output |
| `json`, `uri`, `positions` | The wire format |
| `paths` | Paths as text, one spelling on every system; Windows' rules are tested everywhere |

## Rules

- **It is the compiler, not a second one.** Everything it reports comes from `src/` (`query.h` for
  positions and references). It must never disagree with `kas3c check`.
- **Includes `src/` only.** No `ir/`, `vm/` or Godot header.
- **No dependency:** the JSON reader is here, on purpose.
- **A handler never blocks on the whole project** for an edit to one file. A request checks the bodies of
  the file it asks about (find references: every file's); `settle` checks the rest one file at a time and
  stops when a message is waiting.
- A new request has a test in `tests/test_lsp.cpp` and, when it concerns the wire, in
  `tests/test_lsp_stdio.py`.

Tests: `test_lsp`, `test_workspace`, `test_json`, `lsp_stdio`, and the incremental oracles `test_incremental`
(the workspace) and `test_server_incremental` (whole sessions through `handle_batch` and `settle`).

## Completion transport

`server_completion.cpp` maps compiler queries to LSP items and signature help. It negotiates snippets,
insert/replace edits and completion-list edit defaults independently. Positions use the server's
existing UTF-8/UTF-16 conversion. A client without snippets receives plain names; a client without
insert/replace support receives one ordinary replacement range. Shared edit ranges are sent only to
clients advertising that default. An empty-prefix list is marked incomplete when automatic imports were withheld, so typing a prefix
requests those candidates. Typed-member lists remain complete.

The server does not parse declarations to discover call parameters. Structured compiler candidates
provide required parameter names; signature queries identify the innermost call. Callback positions
remain name-only. Query caches belong to `Workspace`'s current `Queries`: an edit clears them (a rebuild
destroys them with the compilation), and a file whose bodies are checked drops what was worked out from
it. The synchronous loop coalesces waiting document changes. Lookahead holds at most 32 messages or about
1 MiB before dispatch (a single allowed frame can exceed that byte threshold). It reads only currently
available bytes and retains an incomplete frame for later. Cancellation applies to queued work. The
compiler stays single-threaded: a batch's answers are sent first; then the check of what is left (open
files first) runs and asks the channel after each file whether a message waits; if one does it stops, its
diagnostics so far are sent, the next batch is handled, and it goes on from there.
Diagnostics are published for each file once it is checked. Document versions and lookahead use the same
integer validation.

See [the user guide](../../editors/completion.md).

Auto-import and override items include an additional text edit. Override primary ranges remain on one
line, contain the caret and filter on the method name; metadata and existing modifiers survive. Snippet
clients receive a final caret before the generated throwing body; other clients receive plain text.
Snippet escaping distinguishes ordinary text from placeholder text: a closing brace is escaped only
inside a placeholder. This keeps generated source braces literal in Rider's native snippet converter.
Diagnostics retain their document version when supported, including unchanged errors and empty lists
at a new checked version. A client typing ahead may discard an earlier clear; the next clean version
must clear its errors again. The publication cache retains only the latest list/version per file, keeps
clean entries only while open with version support, and suppresses same-version duplicates. Clients
without version support still receive a single clear when an error goes away.
Windows stdio is binary and pipe readiness uses the Windows pipe API; runtime verification is pending.

Language-block candidates carry ordinary source text and tab-stop spans from the front end. The shared
edit serializer escapes literal slices and encodes numbered/mirrored placeholders and the final caret.
Keyword templates replace the filtering identifier, including its suffix, with one ordinary text edit.
They coexist with completion-list range defaults. A client without snippet support still receives the
keyword; override declarations retain their plain-text fallback. Empty function-prefix lists are
incomplete so typing can refresh statement eligibility after parser recovery.
