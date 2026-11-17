# `src/`: the front end

Source text to a checked syntax tree: the lexer, the parser, names (packages, scopes, links between files)
and the type of every expression. `kas3c check`, the lowerer and the language server all read its result.

| Files | Job |
|---|---|
| `source`, `token`, `lexer` | Text to tokens |
| `parser.cpp`, `parser_decl`, `parser_stmt`, `parser_expr`, `parser_literals`, `parser_types`, `parser_functions` | Tokens to the tree in `ast.h`, one file per kind of syntax |
| `ast.h` (with `ast_node.h`, `ast_expr.h`, `ast_stmt.h`), `ast_walk.h`, `ast_printer` (the dispatch; the parts in `ast_printer_parts.cpp`, the class in `ast_printer_class.h`), `ast_reset`, `arena.h` | The tree, its one walk, its printed form, its memory |
| `names*.cpp`, `names.h`, `names_symbols.h`, `names_resolver.h` | Declarations, scopes, imports, and the rules about them; `names_lookup` looks up a name at package level and `names_members` on a class; `names_signatures` resolves the types written outside bodies; the bodies pass is `names_bodies`, `names_locals`, `names_statements` and `names_expressions` |
| `names_update`, `names_retract`, `names_dependencies`, `incremental.h`, `fingerprints`, `dependencies` | Checking as the program is edited: the declarations of what an edit reaches redone, symbols kept, what each file read, and bodies checked when asked for |
| `typer.h`, `typer_*.cpp`, `type_table.cpp` | The type of each expression and what fits where, by what is typed |
| `whole_numbers` | Rules the checker and the lowerer must agree on, written once |
| `held_in_place.h` | Which of Godot's value types a vector holds in its places (`Vector2`, `Vector2i`), for the checker, which refuses `null` written where an element of one goes, and the lowerer, which makes the vector's class hold them so |
| `godot_names.cpp`, `builtins.h` | The name table and AS3's own declarations |
| `query*`, `completion*` | Editor queries (`query_symbols`: outline and symbol search), lexical context, completion candidates/edits and active-call signatures |
| `diagnostics` | Every error, as `file:line:column: error: message` |

## Rules

- **Depends on nothing else here.** No include from `ir/`, `vm/`, `godot_ext/` or `lsp/`, and no Godot
  header. Godot's API arrives as AS3 declarations, like any source file.
- **A new syntax form** needs its node in `ast.h` or the header it includes for that kind of node, its case in `ast_walk.h` and `ast_printer.cpp`, and a
  golden file under `tests/golden/`.
- **A new diagnostic** is written once, with `quoted()` for names, and has a test in
  `tests/test_type_errors.cpp` or a file under `tests/golden/errors/`.
- **A rule the lowerer also needs** goes here, in a header both read. It is never written twice.
- The checker runs on every keystroke in an editor: no work that grows with the whole project for an edit
  to one file (`tools/bench_recheck.cpp` measures it).

Tests: `test_lexer`, `test_parser`, `test_ast`, `test_names`, `test_types`, `test_type_errors`,
`test_query`, `test_override_completion`, `test_statement_completion`, `test_source`, and the `golden` group.

## Editor query ownership

`Parsed::text_regions` records comments, strings and regex literals from the actual lexer/parser,
including their termination state. This avoids a second scanner in completion. Regex rescanning
replaces any conflicting lookahead intervals. Identifier bytes have one shared definition in `token`.

The resolver retains `Unit::local_scopes` for source bodies. Resolution and completion read the same
scope maps, including hoisted locals, parameters, named expressions, catches and function imports.
Scopes and their parent pointers live only as long as their owning compilation; native declarations
without bodies do not retain scopes.

`Queries` lazily caches candidate descriptions and relevant context nodes for that checked generation.
It never caches a history of prefix responses. `Workspace` clears them on an edit, destroys them with a
rebuilt compilation, and has them forget a file whose bodies are checked again. A query reads other files'
declarations only, with one exception: `override` completion writes a base class's default values, which
the base file's bodies resolve, so `Workspace::unit` checks the files of a file's bases with it. `test_workspace` exercises repeated requests, edits,
changed types and close; the compiler corpus tests exercise malformed input under sanitizers.

## Checking as the program is edited

`Compilation::check` checks everything once. `prepare` and `update` check it as it is edited, in two
layers. The declaration layer of a file is declare, link (imports, bases, cycles, overrides, inherited
property halves) and the signature pass; the body layer is the bodies pass, the class rules and the types.
A file's bodies read only what other files declare, so they are checked one file at a time, in any order,
once every signature is resolved. The full check is the same pipeline with every file in it.

- **Redone, not rebuilt.** `retract` takes a file's declarations out of the package tables; declaring it
  again takes the same `Symbol` and `ClassInfo` back by file, class, kind and name, so what other files
  hold stays valid. A duplicate name is won by the file added first (its rank), whatever order the redo
  runs in. Symbols of declarations that are gone are used again for new ones.
- **Fingerprints** (`fingerprints`): each declaration's text outside bodies with what its types resolved to;
  a class's chain (header and supertypes) and shape (chain, every member written, supertypes' shapes).
- **Dependencies** (`names_dependencies`, `dependencies`): what a file's declarations read and what its
  bodies read, worked out from the annotated tree: symbols found, classes used as types, classes extended,
  member names looked up through a program class, package-level names, packages imported whole, files whose
  positions its messages quote, files whose property halves its classes took. A changed fingerprint finds
  its readers through the index; readers of declarations are redone round by round, readers of bodies only
  have their bodies checked again, from where their signatures left them.
- **Cycles** of bases take in every file of the cycle; an interface cycle takes every interface's file, so
  that the cut is where a whole check makes it.
- **Doubt** (`names_doubt`) is a view: what the passes said is kept, and judged again when what a broken file
  declares may have changed.

- **Files join and leave** (`insert`, `remove`) as edits: a file keeps its place in the storage (`Unit::id`)
  and its place in the order (`rank`, which decides the first definition of a name and where a cycle is
  cut) is renumbered; `files()` is the list to walk, in order, without the places of files that left. A
  package is known while a file's block names it, and forgotten with the last.
- A change goes down a chain of classes a round at a time, however deep; only a file redone many times in
  one edit is taken for a mistake in the dependencies and starts the check over.

Only a declaration file that changed starts over (`Workspace`). `test_incremental` compares every answer
with a fresh check after random edits; see `tests/README.md`.

Override completion uses the same inherited visibility rule as name resolution. Its primary edit covers
only the filtering word; additional edits insert visibility without rewriting metadata or comments.
Generated signatures qualify accessible types and public default references, including references already
qualified by class or package. Package prefixes are not value references. Getter and setter declarations
and final modifiers are checked separately, including inherited complementary accessors. Parser recovery
retains an empty override name's span so completion also works before its first character. A declaration
on the next line remains a separate member, including its metadata; valid multiline signatures retain
their names. Tests apply generated edits and check the resulting source with a fresh compiler.

`completion_statements` adds language-block text at statement starts found through cached AST containers.
It excludes expression/header positions and existing continuations, including keyword recovery in
unfinished `if`/`for` headers. Empty gaps use conservative brace/semicolon boundaries. A block carries
ordinary AS3 text and ordered byte-span tab stops; repeated indices link names, and zero is the final
caret. Overrides use the same representation. Only the LSP layer encodes snippet syntax. The try/catch
default rethrows its caught error. These candidates add no project-wide scan or history of edits.
