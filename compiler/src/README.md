# `src/`: the front end

Source text to a checked syntax tree: the lexer, the parser, names (packages, scopes, links between files)
and the type of every expression. `kas3c check`, the lowerer and the language server all read its result.

| Files | Job |
|---|---|
| `source`, `token`, `lexer` | Text to tokens |
| `parser.cpp`, `parser_decl`, `parser_stmt`, `parser_expr`, `parser_literals`, `parser_types` | Tokens to the tree in `ast.h`, one file per kind of syntax |
| `ast.h`, `ast_walk.h`, `ast_printer`, `ast_reset`, `arena.h` | The tree, its one walk, its printed form, its memory |
| `names*.cpp`, `names.h`, `names_resolver.h` | Declarations, scopes, imports, and the rules about them; `names_lookup` looks up a name at package level and `names_members` on a class; the bodies pass is `names_bodies`, `names_locals`, `names_statements` and `names_expressions` |
| `typer.h`, `typer_*.cpp`, `type_table.cpp` | The type of each expression and what fits where, by what is typed |
| `whole_numbers` | Rules the checker and the lowerer must agree on, written once |
| `godot_names.cpp`, `builtins.h` | The name table and AS3's own declarations |
| `query*`, `completion*` | Editor queries, lexical context, completion candidates/edits and active-call signatures |
| `diagnostics` | Every error, as `file:line:column: error: message` |

## Rules

- **Depends on nothing else here.** No include from `ir/`, `vm/`, `godot_ext/` or `lsp/`, and no Godot
  header. Godot's API arrives as AS3 declarations, like any source file.
- **A new syntax form** needs its node in `ast.h`, its case in `ast_walk.h` and `ast_printer.cpp`, and a
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
It never caches a history of prefix responses. `Workspace` destroys queries before changing the
compilation that owns their symbol/tree pointers. `test_workspace` exercises repeated requests, edits,
changed types and close; the compiler corpus tests exercise malformed input under sanitizers.

`names_incremental` reuses the semantic environment only when source outside direct function bodies is
identical and the prior full check was clean. Units with interfaces, accessors, body-local imports or
parse errors fall back. All changed units resolve names before any is typed, preserving cross-file
signature consistency. Stable declaration symbols own their names; each unit owns its replaceable body
symbols and scopes. `Source` copies share an immutable snapshot, so replacing the parse never exposes
text through an expired source reference. Type interning rebases after 1,024 additional entries.

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
