# `src/`: the front end

Source text to a checked syntax tree: the lexer, the parser, names (packages, scopes, links between files)
and the type of every expression. `kas3c check`, the lowerer and the language server all read its result.

| Files | Job |
|---|---|
| `source`, `token`, `lexer` | Text to tokens |
| `parser.cpp`, `parser_decl`, `parser_stmt`, `parser_expr` | Tokens to the tree in `ast.h`, one file per kind of syntax |
| `ast.h`, `ast_walk.h`, `ast_printer`, `ast_reset`, `arena.h` | The tree, its one walk, its printed form, its memory |
| `names*.cpp`, `names.h` | Declarations, scopes, imports, and the rules about them |
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
`test_query`, `test_source`, and the `golden` group.

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
