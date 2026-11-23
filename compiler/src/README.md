# `src/`: the front end

Fixed array annotations (`[]T`) are interned by element type. Their length is read-only. The element type is checked for indexed writes and brace initializers.

Callback annotations such as `(int, String)=>void` are interned by their complete signature. Binding checks the declared target, and calling checks the arguments against that signature.

The front end turns source text into a checked syntax tree. It holds the lexer, the parser, name resolution
and the type checker, plus the queries an editor asks. `kas3c check`, the lowerer and the language server
all read its result.

**Depends on:** nothing else in this repository. It includes no header from `ir/`, `vm/`, `godot_ext/` or
`lsp/`, and no Godot header. Godot's API arrives as AS3 declarations, like any source file.

After checking your source, the checker can give an untyped local the storage of its first write.
The proof follows that declaration or assignment through its block, checks all later assignments,
and checks each use. It covers typed parameters, fields, getters, construction and typed return values.
A fresh engine object can bind a member while it stays detached. A node passed in by a caller can
already carry a script. Captures, early reads and operations that observe a dynamic tag keep the
declared representation. See [Untyped member access](../../docs/design/untyped-performance.md).

## Files

| Files | Job |
|---|---|
| `source`, `token`, `lexer` | Text to tokens |
| `parser.cpp` and `parser_*.cpp` | Tokens to the tree, one file for each kind of syntax: declarations, statements, expressions, literals, types, functions |
| `ast.h`, `ast_node.h`, `ast_expr.h`, `ast_stmt.h` | The tree |
| `ast_walk.h`, `ast_printer*`, `ast_reset`, `arena.h` | The tree's one walk, its printed form, and its memory |
| `names.h`, `names_symbols.h`, `names_resolver.h` | Declarations, scopes and symbols |
| `names.cpp`, `names_link`, `names_rules`, `names_signatures` | Declaring every definition, linking imports and base classes, the rules about them, and the types written outside bodies |
| `names_lookup`, `names_members` | Looking up a name at package level, and on a class |
| `names_bodies`, `names_locals`, `names_statements`, `names_expressions` | Resolving names inside function bodies |
| `names_update`, `names_retract`, `names_dependencies`, `names_doubt`, `incremental.h`, `fingerprints`, `dependencies` | [Checking as the program is edited](#checking-as-the-program-is-edited) |
| `typer.h`, `typer_*.cpp`, `type_table.cpp` | The type of each expression, and what fits where |
| `whole_numbers`, `held_in_place.h` | Rules the checker and the lowerer must agree on, written once |
| `godot_names`, `builtins.h` | The table of Godot's own names, and AS3's built-in declarations |
| `query*`, `completion*` | [Editor queries](#editor-queries): what is at a position, where it's declared, who refers to it, and completion |
| `names_dump` | The output of `kas3c check --dump-names` |
| `diagnostics` | Every error, as `file:line:column: error: message` |

## Rules

- **The checker runs on every keystroke in an editor.** An edit to one file must not cause work that grows
  with the whole project. `tools/bench_recheck.cpp` measures it.
- **A rule the lowerer also needs goes in a header both read.** It's never written twice. `whole_numbers.h`
  decides whether an expression is a whole number. `held_in_place.h` names the Godot value types that a
  vector stores in place.
- **An error message is written once,** with `quoted()` around names.
- **Syntax trees stay shallow.** The parser limits nesting so that later passes can walk a tree by plain
  recursion. [The language page](../../docs/language.md#limits) lists the limits.
- **A syntax error silences only what it could have caused:** the rest of its own file, every file that
  uses something that file declares, and every complaint that a name is missing. Names are still resolved,
  so go to definition and hover work in a half-typed file.

## Where a new thing goes

| New thing | Needs |
|---|---|
| A syntax form | Its node in `ast.h` or the header it includes for that kind of node, its case in `ast_walk.h` and `ast_printer.cpp`, and a golden file under `tests/golden/` |
| An error message | A test in `tests/test_type_errors.cpp`, or a file under `tests/golden/errors/` |
| A type rule | The `typer_*.cpp` for what is typed, and a case in `tests/test_types.cpp` |

## Checking as the program is edited

`Compilation::check` checks everything once. `prepare` and `update` check a program as it's edited, in two
layers:

- **The declaration layer** of a file is what other files can see: its definitions, imports, base classes
  and signatures.
- **The body layer** is the function bodies, the class rules and the types.

A file's bodies read only what other files declare. So once every signature is resolved, bodies are checked
one file at a time, in any order. A full check is the same pipeline with every file in it.

- **Redone, not rebuilt.** `retract` takes a file's declarations out of the package tables. Declaring the
  file again gives each declaration the same `Symbol` and `ClassInfo` back, found by file, class, kind and
  name, so what other files hold stays valid.
- **Fingerprints** (`fingerprints`). Each declaration has a fingerprint: its text outside bodies, with what
  its types resolved to. A class also has one for its chain of supertypes and one for its whole shape.
- **Dependencies** (`names_dependencies`, `dependencies`). The checker records what each file's
  declarations read and what its bodies read: symbols found, classes used as types, classes extended,
  member names looked up, packages imported whole. When a fingerprint changes, the files that read it are
  found through an index. Readers of declarations are redone round by round. Readers of bodies have only
  their bodies checked again.
- **Order.** Each file keeps a rank that decides which definition of a duplicate name wins and where a
  cycle of base classes is cut. The result doesn't depend on the order edits arrive in.
- **Doubt** (`names_doubt`). While a file has a syntax error, errors in other files that it could have
  caused are hidden. What the passes found is kept, and judged again when the set of broken files changes.
- **Files join and leave** as edits. A package is known while some file names it, and forgotten with the
  last one, so a long session doesn't grow.

Only a change to a declaration file starts the check over. `test_incremental` compares every answer with a
fresh check after random edits. See [tests/README.md](../tests/README.md#the-incremental-oracle).

## Editor queries

`Queries` in `query.h` answers what an editor asks about a checked program.

- **Documentation follows the resolved symbol.** The editor queries read the ASDoc text immediately
  before its declaration from the parser's text regions. Getter and setter share one description.
- **Text regions come from the parser.** `Parsed::text_regions` records comments, strings and regular
  expression literals as the lexer and parser found them, so completion needs no second scanner.
- **Completion reads the resolver's scopes.** The resolver keeps `Unit::local_scopes` for function bodies.
  Resolution and completion read the same scope maps, so they agree on hoisted locals, parameters, catch
  variables and function-local imports.
- **Caches live for one checked generation.** `Queries` caches candidate descriptions for the current
  check. An edit clears them, and a rebuilt compilation destroys them. No history of earlier answers is
  kept.
- **A query reads other files' declarations only.** The one exception is `override` completion, which
  writes a base class's default values. `Workspace::unit` checks the bodies of a file's base classes for
  that reason.
- **Override completion** uses the same visibility rule as name resolution. The generated signature
  qualifies types and default values so that they mean the same thing in the derived class. Tests apply
  the generated edit and check the result with a fresh compiler.
- **Keyword completion** takes reserved-word spellings from `TokenKind`. The completion table keeps
  the handwritten descriptions, display order and contextual labels such as `for each`, `get` and `set`.
- **Statement completion** (`completion_statements`) offers language blocks such as `try` and `for` at the
  start of a statement. A block is ordinary AS3 text with ordered tab stops. Only the language server
  turns that into snippet syntax.

## Tests

`test_lexer`, `test_parser`, `test_ast`, `test_source`, `test_names`, `test_types`, `test_type_errors`,
`test_godot_math`, `test_query`, `test_documentation`, `test_override_completion`, `test_statement_completion`, and the `golden`
group.
