# Kumikae AS3: language server

Design, 2026-11-12. Status: **proposed, not yet approved by the owner.**

This document plans the language server that the main design lists under stage 6 ("Tooling"). It proposes
building the server in slices, the first of them much earlier than stage 6.

Lines marked **(owner)** are what the owner asked for. Lines marked **(call)** are the designer's decisions,
open to being overruled. Lines marked **(unverified)** are believed true but were not checked; the slice that
depends on one must check it first.

## Intent

The owner wants AS3 in Godot to be easy to use and easy to debug from JetBrains IDEs, Rider first **(owner)**,
and asked for the language server to be planned **(owner)**.

A language server is the program an editor talks to for everything it knows about your code: errors as you
type, completion, go to definition, rename. It speaks the Language Server Protocol (LSP), so one server
serves every editor that has an LSP client.

## Goals

1. **Errors as you type,** identical to what `kas3c check` reports, in every supported editor.
2. **Navigation:** go to definition, find references, document outline, workspace symbol search.
3. **Completion and signature help** that know Godot's classes and your own.
4. **One server for every editor.** JetBrains IDEs and VS Code need only a thin client each.
5. **Fast enough to forget.** A keystroke gets fresh errors without a visible wait.

## Non-goals

- The debugger. That is the debug adapter, a separate program with its own protocol.
- A formatter.
- Refactorings beyond rename.
- The Godot editor's built-in script editor. It gets completion and errors from the same front end through
  Godot's own script-language interface (stage 2), not through this server.

## Anti-goals

- **A second opinion.** The server must never disagree with the compiler about what is an error. It links
  the same front-end library and adds no checks of its own.
- **A server that goes blind on a half-typed line.** Code being edited is broken most of the time. If one
  syntax error switches off navigation and completion for the file, the server has failed.

## Shape

```
 editor ──(LSP over stdio, JSON-RPC)──► kas3c lsp ──► kas3front (the compiler's front end)
                                            │
                                            └── workspace: open buffers, files on disk, declaration files
```

- **One binary (call).** The server is a subcommand, `kas3c lsp`, not a separate program. One thing to
  build, ship and version.
- **Same language and constraints as the compiler (call):** C++17, no exceptions, no RTTI, no third-party
  dependencies. JSON reading and writing is a small in-house module. Left to the builder: whether to vendor a
  single-header JSON library instead.
- **Standard input and output only (call).** No sockets in the first slices.

### The workspace

- The server finds the project by walking up from the opened folder to `project.godot`. Without one, the
  opened folder is the project.
- Every `.as` file under the project is part of the program. Open files use the editor's unsaved text.
- Declaration files: the built-in library, plus Godot's classes. Until stage 2 generates those from Godot's
  API description, the server accepts declaration files from its configuration.

### Keeping up with typing

- **First version: re-check everything on each change (call).** The front end is small and fast, and
  whole-program re-checking has no stale-state bugs.
- **A measured budget decides when that stops being enough (call):** re-checking a 100,000-line project
  must take under 50 ms on a laptop. The first slice measures it on a generated project. If it fails, the
  server caches each file's parse and declarations and re-runs only the passes that a change invalidates.
- Changes arrive as whole documents at first (LSP "full sync"). Incremental sync is an optimisation for
  later.

### Positions

The compiler counts columns in bytes. LSP counts in UTF-16 code units unless the client agrees otherwise.
The server asks for UTF-8 positions (LSP 3.17 `positionEncoding`) and converts when the client refuses.
**(unverified:** which JetBrains versions accept UTF-8 positions.)

## What the front end must change first

Name resolution today runs only when every file parses (stage 1b, ruling 4). That is right for a
command-line check and wrong for an editor. Before the first slice:

1. **Resolve names in files that have syntax errors.** The parser already returns a tree for any input. The
   passes must accept it: skip placeholder nodes, and stay quiet about names inside a region the parser
   recovered from, so that one typing mistake does not produce a page of errors.
2. **Keep positions for everything navigable.** Stage 1b records each resolved reference and each
   declaration's position. Members reached through `.` join them in stage 1c.
3. **A query interface on the library:** "what is at this position", "where is it declared", "who refers to
   it". The server and, later, the Godot editor plugin both use it.

`kas3c check` keeps its current behaviour: with a syntax error anywhere, it reports syntax errors only.

## Slices

Each slice ends in something you can use in an editor.

| Slice | Needs | Delivers | Done when |
|---|---|---|---|
| **L1. Errors and navigation** | Stage 1b (names) and the three changes above | Errors as you type; go to definition; find references; document outline; workspace symbols; hover showing the declaration | In VS Code and Rider, a typing mistake shows an error within the budget, and go to definition works on locals, parameters, classes, imports and inherited members. |
| **L2. Completion** | Stage 1c (types) | Completion after `.` and for bare names; signature help; hover with types; navigation on members; type errors as you type | Typing `position.` on a `Node2D` lists `Vector2`'s members. |
| **L3. Godot knowledge** | Stage 2 (generated Godot declarations) | Godot's documentation in hover and completion; code actions: add import, add `override`; rename | Hovering `move_and_slide` shows Godot's own description. |
| **L4. JetBrains plugin** | L1 to L3 | One IntelliJ-platform plugin: file type, highlighting, the server wired in, and later the debug adapter | Installing the plugin in Rider gives everything above with no manual setup. |

**Proposed order (call):** build L1 straight after stage 1c, before the interpreter (stage 2). Reasons:

- It is the first thing you can *use*: open a project in an editor and see the compiler working.
- Every later stage is written and debugged in AS3 test programs. A working editor makes that faster.
- The error-tolerance work above is cheaper now, while the passes are new, than after two backends depend
  on them.

Cost of that order: the interpreter starts later by the length of L1.

L2 then follows stage 1c's types as soon as L1 exists. L3 and L4 stay in stage 6.

## Testing

- **Scripted sessions.** A test is a file of JSON-RPC messages to send and the responses expected. No editor
  is involved, so the tests run in CI with the rest.
- **The same corpus.** Every corpus file is opened, edited one byte at a time, and closed; the server must
  answer every request and never crash. This reuses the parser's every-prefix and every-deletion tests.
- **Agreement with the compiler.** For every golden error file, the server's diagnostics must equal
  `kas3c check`'s.
- **By hand, once per slice:** VS Code and Rider.

## Unverified, to check before the slice that depends on it

- Which JetBrains editions give plugins an LSP client. The main design already flags this. If Rider's
  edition does not, L4 implements JetBrains' own language interfaces on top of the same query interface, and
  L1 is demonstrated in VS Code first.
- That whole-program re-checking meets the 50 ms budget at 100,000 lines.
- UTF-8 position support in the target editors.

## Left to the builder

- The JSON module, the message loop, and how requests are cancelled.
- The cache design, if the budget requires one.
- The VS Code client used for testing. It is a few dozen lines and is not a product.
