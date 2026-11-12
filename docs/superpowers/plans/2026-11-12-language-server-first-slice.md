# Language Server, First Slice (L1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `kas3c lsp` is a language server an editor can talk to: the compiler's errors as you type, go to
definition, find references, hover, a document outline and workspace symbol search.

**Architecture:** The front-end library gains three things: a check that runs every pass on files with
syntax errors, a way to reuse a file's parse between checks, and a query index (what is at this position,
where is it declared, who refers to it). A second library, `kas3lsp`, holds the JSON module, the workspace
(files on disk, open buffers, one compilation) and the server (messages in, messages out). `kas3c lsp`
connects it to standard input and output.

**Tech stack:** C++17, no exceptions, no RTTI, no third-party code. Python 3 for the end-to-end test.

**Spec:** `docs/superpowers/specs/2026-11-12-language-server-design.md`.

## Global Constraints

- Library and server code build with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
- `kas3c check` and `--dump-names` output do not change. Every existing test passes unchanged.
- The server adds no checks of its own: what it publishes is what `kas3c check` prints for the same files.
- Standard output carries protocol messages only.
- No console vendor material. Stage files by name. No downloads without the owner's yes.

## Measured before planning

A generated project (`compiler/tests/bench/generate_project.py`: 202 files, 99,586 lines, every class
extends a Godot class), Release build, this laptop:

| What | Time |
|---|---|
| Godot's API alone (parse, declare, link) | about 30 ms |
| The 100,000 lines on top of it | about 55 ms, of which parsing is about 35 ms |
| Whole check | about 85 ms |

The design's budget is 50 ms, so whole re-parsing fails it. Task 2 reuses the parse of every file that did
not change, which the design names as the remedy.

## Design decisions (rulings made while planning)

1. **What is published.** Exactly `kas3c check`'s output: while any file has a syntax error, only syntax
   errors are published, in every file. Navigation keeps working, because the passes still run. Reason:
   a file that lost a declaration to a syntax error makes clean files report unknown names, and the
   project's rule is to report only what is certain. Cost if wrong: type errors blink off while a line is
   half-typed. A later slice can silence only the broken region.
2. **Tolerant checking is opt-in** (`Compilation::check(true)`). `kas3c check` keeps its behaviour.
3. **Parse reuse.** A `Parsed` file (source, tree, syntax errors) is shared between compilations. The
   passes write onto the tree, so a tree is wiped (`clear_annotations`) before it is used again, and only
   one compilation may use it at a time. Cost if wrong: a field the wipe misses is a dangling pointer; the
   sanitizer build runs edit sequences with queries at every offset to catch it.
4. **Positions.** UTF-16 is the default. UTF-8 is used when the client offers it. Both are tested.
5. **A diagnostic's range** runs from its offset to the end of the identifier or number there, otherwise
   one character.
6. **Godot's and the built-in declarations** are written, read-only, to a folder in the system's temporary
   directory, so that go to definition has a file to open. No diagnostics are ever published for them.
7. **The project** is the folder holding `project.godot`, found by walking up from the opened folder;
   otherwise the opened folder. Every `.as` file under it is part of the program, in sorted order, except
   in folders whose name starts with a dot. At most 20,000 files are read.
8. **Typing bursts.** A change marks the workspace as stale. The check runs when no further message is
   waiting, or when a request needs an answer.
9. **Scope.** Included: diagnostics, definition, references, hover (the declaration), document outline,
   workspace symbols (the program's own, not Godot's). Not included: completion and signature help (L2),
   Godot's documentation, rename, code actions (L3).
10. **Find references** lists uses of the same symbol. Overrides of a method are different symbols and are
    not listed.

## Review Focus

1. A clean file shows an error it would not get from `kas3c check`, or keeps one after it is fixed.
2. A position is off by one or more when a line has non-ASCII text before it.
3. A query on a half-typed file crashes, hangs, or reads a tree that a later check has wiped.
4. A path with a space, a non-ASCII character or a `%` does not round-trip, so a file is checked twice
   ("already defined") or not at all.
5. Malformed input from the editor (bad JSON, a huge or missing `Content-Length`, a request before
   `initialize`) kills the server or writes something other than protocol to standard output.

---

### Task 1: Tolerant checking

**Files:** Modify `compiler/src/diagnostics.h/.cpp`, `compiler/src/names.h`, `compiler/src/names.cpp`,
`compiler/src/names_link.cpp`, `compiler/src/ast.h`. Create `compiler/tests/test_query.cpp`.

**Produces:** `void Diagnostics::mute()` (later errors are dropped); `void Compilation::check(bool
p_through_syntax_errors = false)`; `Symbol *Import::symbol` (set by `resolve_imports`).

- [ ] Tests first: with a syntax error in a file, `check(true)` resolves a name in that file and in another
  file (its `symbol` is set), and `format_diagnostics()` equals `check()`'s; `check()` is unchanged; an
  import's `symbol` is the class it names.
- [ ] Run, fail; implement; run the whole suite; commit.

### Task 2: Parse reuse

**Files:** Modify `compiler/src/names.h`, `compiler/src/names.cpp`. Create `compiler/src/ast_walk.h`,
`compiler/src/ast_reset.cpp`. Test in `test_query.cpp`.

**Produces:** `struct Parsed { Source source; Arena arena; Diagnostics diagnostics; Program *tree; bool
used; }`; `std::shared_ptr<Parsed> parse_file(std::string p_path, std::string p_text)`;
`Unit *Compilation::add(std::shared_ptr<Parsed> p_parsed, bool p_is_declaration = false)`;
`Compilation(const std::vector<std::shared_ptr<Parsed>> &p_builtins)`;
`std::vector<std::shared_ptr<Parsed>> parse_builtins()`; `void clear_annotations(Program *p_tree)`;
`template <class F> void walk(Node *p_node, F &&p_visit)` (every node, parents first).

- [ ] Tests first: two compilations in a row over the same `Parsed` files give the same diagnostics and
  `dump_names()` as fresh ones, for every corpus file; after `clear_annotations` no `symbol`, `member` or
  `type` field in the tree is set (checked with `walk`).
- [ ] Run, fail; implement; whole suite in the normal and sanitizer builds; commit.

### Task 3: Queries

**Files:** Create `compiler/src/query.h`, `compiler/src/query.cpp`. Test in `test_query.cpp`.

**Produces:**

```cpp
struct Occurrence { uint32_t offset; uint32_t length; Symbol *symbol; bool is_declaration; };
struct Place { const Unit *unit; uint32_t offset; uint32_t length; };
struct OutlineItem { std::string name; SymbolKind kind; bool is_const; bool is_constructor;
		uint32_t offset, end, name_offset, name_length; std::vector<OutlineItem> children; };
class Index {
public:
	explicit Index(Compilation &p_compilation); // After check().
	const Occurrence *at(const Unit &p_unit, uint32_t p_offset); // The name under or just before the offset.
	Place declaration(const Symbol *p_symbol) const; // unit is null for `arguments`.
	std::vector<Place> references(const Symbol *p_symbol, bool p_with_declaration);
	std::string describe(const Symbol *p_symbol) const; // "function hit(by:Node, power:int = 1):void"
	std::vector<OutlineItem> outline(const Unit &p_unit) const;
	std::vector<const Symbol *> find(std::string_view p_query, size_t p_limit) const; // Program files only.
};
```

Occurrences come from `Unit::references`, from `Member::symbol`/`Member::member`, from `Import::symbol`, and
from each symbol's own declaration. A unit's list is built on first use.

- [ ] Tests first, on marked source (`tests/marked.h`): definition of a local, a parameter, a field, a
  method called through `this.` and through another object, an inherited member, a class in a type, an
  import, a static through its class, a Godot method, a getter; the position just after a name's last
  letter finds it; a position on whitespace or a keyword finds nothing; references of a field across two
  files, with and without its declaration; `describe` for each symbol kind; outline of a class with a
  constructor, a constant, a property and a helper class; `find` is case-insensitive and capped; every
  query on a file with a syntax error answers.
- [ ] Run, fail; implement; pass; commit.

### Task 4: JSON

**Files:** Create `compiler/lsp/json.h`, `compiler/lsp/json.cpp`, `compiler/tests/test_json.cpp`. Modify
`compiler/CMakeLists.txt` (library `kas3lsp`; tests link it).

**Produces:** `class Json` (null, bool, number, string, array, object; `get(key)`, `at(i)`, `string()`,
`number()`, `boolean()` returning a default on the wrong kind); `bool parse_json(std::string_view, Json *)`;
`std::string write_json(const Json &)`.

- [ ] Tests first: round trips; escapes including `é` and a surrogate pair; an integer stays an
  integer when written; malformed input (truncated, trailing comma, bare word, lone surrogate, 10,000
  nested arrays) returns false without crashing; a missing key or wrong kind gives the default.
- [ ] Run, fail; implement; pass; commit.

### Task 5: Workspace

**Files:** Create `compiler/lsp/uri.h/.cpp`, `compiler/lsp/workspace.h/.cpp`, `compiler/tests/test_workspace.cpp`.

**Produces:** `std::string path_from_uri(std::string_view)`, `std::string uri_from_path(std::string_view)`;

```cpp
class Workspace {
public:
	struct Options { bool godot = true; std::vector<std::string> declarations; };
	explicit Workspace(Options p_options);
	void set_root(const std::string &p_folder); // Finds project.godot above it, then scans.
	void open(const std::string &p_path, std::string p_text);
	void change(const std::string &p_path, std::string p_text);
	void close(const std::string &p_path);
	void file_changed_on_disk(const std::string &p_path);
	Compilation &compilation(); // Checks first if anything changed.
	Index &index();
	Unit *unit(const std::string &p_path);
	std::vector<Unit *> program_units();
	std::string real_path(const Unit &p_unit); // A file an editor can open, also for Godot's declarations.
};
```

- [ ] Tests first (in a temporary folder): the scan finds nested `.as` files, skips dot folders, and finds
  `project.godot` above the opened folder; an open buffer replaces its disk copy (one unit, no "already
  defined"); closing returns to the disk text; a deleted file leaves the program; a path with a space and
  with `é` round-trips through a URI; a change re-parses only the changed file (the other units' `Parsed`
  are the same objects); Godot's declarations get a readable file.
- [ ] Run, fail; implement; pass; commit.

### Task 6: Server

**Files:** Create `compiler/lsp/server.h/.cpp`, `compiler/tests/test_lsp.cpp`, `compiler/tests/test_lsp_stdio.py`.
Modify `compiler/tools/kas3c.cpp`, `compiler/CMakeLists.txt`.

**Produces:**

```cpp
class Server {
public:
	explicit Server(Workspace::Options p_options);
	void handle(std::string_view p_message, std::vector<std::string> &r_out); // One JSON message in.
	void settle(std::vector<std::string> &r_out); // Check if stale, and publish what changed.
	bool exited() const; int exit_code() const;
};
int run_server(int p_in_fd, int p_out_fd, Workspace::Options p_options); // Content-Length framing.
```

`kas3c lsp [--no-godot] [--declarations <file>]...`.

- [ ] Tests first, in process (`test_lsp.cpp`): `initialize` answers with the capabilities and the chosen
  position encoding; a request before `initialize` gets error -32002; an unknown request gets -32601 and
  an unknown notification gets nothing; `didOpen` with an error publishes it with a range; fixing it
  publishes an empty list; `didClose` of a file not on disk publishes an empty list; nothing is published
  twice in a row for an unchanged file; an `é` before the error gives the right column in UTF-16 and in
  UTF-8; a URI with `%20` works; `definition`, `references`, `hover`, `documentSymbol` and
  `workspace/symbol` answer on a two-file project; definition of a Godot method answers with a file that
  exists; two changes then `settle` check once; `shutdown` then `exit` gives exit code 0, `exit` alone 1.
- [ ] Agreement: for every `tests/golden/**` error file, the published messages and positions equal the
  `.diag` file's.
- [ ] Robustness: every corpus file is opened, then every prefix and every single-byte deletion is sent as
  a change, with `definition`, `hover` and `documentSymbol` asked at sampled offsets; nothing crashes
  (sanitizer build). Godot's API is loaded for a sample.
- [ ] End to end (`test_lsp_stdio.py`): a scripted session over the real process; standard output parses
  as framed messages and nothing else; bad JSON, a missing `Content-Length` and end of input all end
  cleanly.
- [ ] Run, fail; implement; pass; commit.

### Task 7: The budget

**Files:** Create `compiler/tools/bench_recheck.cpp`. Modify `compiler/CMakeLists.txt`.

- [ ] `bench_recheck <folder>` opens the folder as a workspace, changes one file 20 times and prints the
  median time of a re-check. Run it on the generated 100,000-line project in the Release build and record
  the number. If it is over 50 ms, find where the time goes and rule on it in the ledger.

### Task 8: Editors and documents

**Files:** Create `editors/vscode/package.json`, `editors/vscode/extension.js`,
`editors/vscode/language-configuration.json`, `editors/README.md`. Modify `README.md`, `compiler/README.md`,
`docs/roadmap.md`, `docs/diagrams/build.py`, `roadmap.svg`, the language-server design's unverified list.

- [ ] The VS Code client: starts `kas3c lsp`, registers `.as` files. It needs `vscode-languageclient` from
  npm, which is a download: write the files, do not install.
- [ ] Rider: the set-up steps for a language-server client. Not verified by hand until the owner agrees to
  the installs.
- [ ] Documents; commit; push the branch.
