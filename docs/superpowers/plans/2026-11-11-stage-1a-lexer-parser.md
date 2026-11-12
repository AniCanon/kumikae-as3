# Stage 1a: AS3 Lexer and Parser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A command-line tool, `kas3c parse`, that reads ActionScript 3 source files, reports syntax errors with file, line and column, and can print the syntax tree it built.

**Architecture:** A hand-written lexer and recursive-descent parser in C++17, as a static library (`kas3front`) with three future hosts: this command-line tool, the Godot editor plugin, and the language server. The parser produces an arena-allocated syntax tree with source positions on every node and recovers from errors so one run reports many. Nothing here resolves names or checks types; that is stage 1b.

**Tech Stack:** C++17, CMake, CTest, Python 3 (standard library) for golden-file tests. No third-party dependencies.

**Spec:** `docs/superpowers/specs/2026-11-11-kumikae-as3-design.md` (sections "Front end", "Language", "Stages" item 1), amended by `docs/superpowers/specs/2026-11-11-performance-redesign.md`.

## Global Constraints

- Everything lives under `compiler/`. Nothing outside `compiler/` and `docs/` is created or changed.
- C++17. The library is compiled with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`: it must be loadable into the Godot editor, which is built that way. No `throw`, `try`, `dynamic_cast` or `typeid`.
- No third-party code. The standard library is allowed.
- Namespace `kas3`. Library target `kas3front`. Tool target and binary name `kas3c`.
- File extension for AS3 source is `.as`.
- Source text is UTF-8. A leading byte-order mark is skipped. Line endings `\n`, `\r\n` and `\r` each count as one line break.
- Lines and columns are 1-based. A column counts bytes from the start of the line, plus one.
- Diagnostic format, exactly: `<file>:<line>:<column>: error: <message>` followed by a newline.
- The parser never crashes or hangs on any input, however malformed. Recursion depth is capped at 256 nested constructs; exceeding it is a diagnostic, not a stack overflow.
- First-version language (from the spec): packages and imports; classes, interfaces, inheritance; `public`, `private`, `protected`, `internal`; static and instance members; getters and setters; `const`; function values and closures; default and rest parameters; `Array`, `Vector.<T>`, object literals; `is` and `as`; `for`, `for in`, `for each`, `switch`, labelled loops; `try`/`catch`/`finally`, `throw`; untyped values (`*`); metadata tags; regular-expression literals.
- Left out of the first version, each with its own clear error (from the spec): E4X (inline XML, `@` attributes, `..` descendants, `default xml namespace`), the `with` statement, user-defined namespaces (`namespace` definitions, `use namespace`, `::` qualifiers), and `include`. `dynamic` classes parse (the modifier is recorded) and are rejected in stage 1b.
- Commit messages carry no attribution lines.

## Review Focus

- **Input cut off mid-construct** (an unterminated string, comment, regular expression, block, or argument list at end of file): one diagnostic at the place the construct began, and the parser stops cleanly.
- **Missing semicolons.** AS3 lets a line break end a statement. `return` followed by a line break returns nothing; `a` newline `++b` is two statements; a line that begins with `(` or `[` continues the previous statement.
- **`>` closing a type argument.** `Vector.<Vector.<int>>` must close two type lists, and `a >> b`, `a >>> b`, `a >= b` must still be operators.
- **`/` meaning division or the start of a regular expression,** decided by what came before it.
- **Reserved words used as names** after a dot (`event.type`, `obj.default`, `x.class`) and as object-literal keys (`{ default: 1, class: 2 }`), which real AS3 code does.

## File Structure

```
compiler/
  CMakeLists.txt
  README.md
  src/
    source.h / source.cpp          Source text, byte offsets to line and column
    diagnostics.h / diagnostics.cpp   Collecting and formatting errors
    token.h / token.cpp            Token kinds and their names; keyword table
    lexer.h / lexer.cpp            Text to tokens
    arena.h                        Bump allocator for syntax-tree nodes
    ast.h                          Node kinds and structs
    ast_printer.h / ast_printer.cpp   Tree to text, for tests and --dump-ast
    parser.h                       Parser class declaration
    parser.cpp                     Token helpers, recovery, program and package level
    parser_decl.cpp                Classes, interfaces, members, functions, variables, metadata
    parser_stmt.cpp                Statements
    parser_expr.cpp                Expressions and type annotations
  tools/
    kas3c.cpp                      Command-line entry point
  tests/
    check.h                        Minimal assertion macros
    test_source.cpp  test_lexer.cpp  test_parser.cpp
    run_golden.py                  Golden-file runner
    golden/ok/*.as + *.ast         Programs that must parse, with expected tree
    golden/errors/*.as + *.diag    Programs that must fail, with expected diagnostics
    corpus/*.as                    Real-shaped programs that must parse with no diagnostics
```

## Interfaces shared across tasks

Every task's implementer sees only their own task, so the shared types are fixed here.

```cpp
namespace kas3 {

// source.h
struct Position { int line; int column; };            // both 1-based
class Source {
public:
    Source(std::string p_path, std::string p_text);   // skips a UTF-8 byte-order mark
    const std::string &path() const;
    const std::string &text() const;                   // without the byte-order mark
    Position position(uint32_t p_offset) const;        // offsets past the end clamp to the end
    std::string_view line_text(int p_line) const;      // without its line break; empty if out of range
    int line_count() const;
};
bool read_file(const std::string &p_path, std::string *r_text);   // false if unreadable

// diagnostics.h
struct Diagnostic { uint32_t offset; std::string message; };
class Diagnostics {
public:
    static const int LIMIT = 100;                      // further errors are counted, not stored
    void error(uint32_t p_offset, std::string p_message);
    bool has_errors() const;
    int count() const;                                 // total reported, including those past LIMIT
    const std::vector<Diagnostic> &items() const;      // at most LIMIT, in order of offset then arrival
    std::string format(const Source &p_source) const;  // one line per item, in the exact format above;
                                                       // then "<file>: error: too many errors (<n> more)\n" if any were dropped
};

// token.h
enum class TokenKind : uint8_t { /* see Task 2 */ };
struct Token {
    TokenKind kind;
    uint32_t offset;            // byte offset of the first character
    uint32_t length;            // in bytes
    bool newline_before;        // a line break lies between the previous token and this one
};
const char *token_kind_name(TokenKind p_kind);          // e.g. "identifier", "'('", "'function'"

// lexer.h
class Lexer {
public:
    Lexer(const Source &p_source, Diagnostics &p_diagnostics);
    Token next();                                       // returns EndOfFile forever at the end
    Token rescan_as_regex(const Token &p_slash);        // p_slash is a Slash or SlashAssign just returned
    std::string_view text(const Token &p_token) const;
};

// arena.h
class Arena {
public:
    template <class T, class... Args> T *make(Args &&...);   // T must be trivially destructible
    template <class T> T *make_array(size_t p_count);        // zero-initialised
    std::string_view copy(std::string_view p_text);
};

// parser.h
struct Program;                                              // ast.h
Program *parse(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics);   // never null

// ast_printer.h
std::string print_ast(const Program *p_program, const Source &p_source);

} // namespace kas3
```

---

### Task 1: Build skeleton, source text, diagnostics

**Files:**
- Create: `compiler/CMakeLists.txt`, `compiler/README.md`, `compiler/src/source.{h,cpp}`, `compiler/src/diagnostics.{h,cpp}`, `compiler/tests/check.h`, `compiler/tests/test_source.cpp`

**Interfaces:**
- Produces: `Position`, `Source`, `read_file`, `Diagnostic`, `Diagnostics` exactly as in the shared block; CMake targets `kas3front` (static library) and `test_source`; test macros in `check.h`: `CHECK(cond)`, `CHECK_EQ(a, b)` (prints both values on failure), and `int finish()` which prints `<n> checks, <m> failed` and returns 0 or 1.

- [ ] **Step 1: Write `tests/check.h` and the failing tests** in `test_source.cpp`:
  - `position_of_first_byte`: text `"ab\ncd"`, offset 0 is line 1 column 1; offset 3 is line 2 column 1; offset 4 is line 2 column 2.
  - `line_endings`: `"a\r\nb\rc\nd"` has 4 lines; the offset of `d` is line 4 column 1; `line_text(1)` is `"a"`, `line_text(2)` is `"b"`.
  - `byte_order_mark_is_skipped`: text beginning `\xEF\xBB\xBF` then `"x"`: `text()` is `"x"`, offset 0 is line 1 column 1.
  - `offset_past_end_clamps`: text `"ab"`, `position(99)` is line 1 column 3.
  - `empty_source`: `line_count()` is 1; `position(0)` is line 1 column 1.
  - `diagnostic_format`: source path `dir/A.as`, text `"x\n  y"`, `error(4, "unexpected 'y'")`: `format()` is exactly `"dir/A.as:2:3: error: unexpected 'y'\n"`.
  - `diagnostics_are_ordered_by_position`: errors at offsets 9 then 2 format with the offset-2 line first.
  - `diagnostic_limit`: 105 errors: `items().size()` is 100, `count()` is 105, and `format()` ends with `"dir/A.as: error: too many errors (5 more)\n"`.
  - `read_missing_file_fails`: `read_file("/nonexistent/x.as", &text)` is false.
- [ ] **Step 2: Write `CMakeLists.txt`** (project `kas3`, C++17, flags from Global Constraints on `kas3front`, `enable_testing()`, each `tests/test_*.cpp` an executable linked to `kas3front` and registered with CTest; tests are built without `-Werror`). Run `cmake -S compiler -B compiler/build && cmake --build compiler/build`. Expected: FAIL, `source.h` not found.
- [ ] **Step 3: Implement `Source`, `read_file`, `Diagnostics`.** `Source` records line-start offsets once in its constructor; `position` is a binary search.
- [ ] **Step 4: Run** `cmake --build compiler/build && ctest --test-dir compiler/build --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Write `compiler/README.md`** (what this is, how to build and test, one paragraph) **and commit** `compiler: add build skeleton, source positions and diagnostics`.

### Task 2: Tokens and lexer

**Files:**
- Create: `compiler/src/token.{h,cpp}`, `compiler/src/lexer.{h,cpp}`, `compiler/tests/test_lexer.cpp`
- Modify: `compiler/CMakeLists.txt` (add sources)

**Interfaces:**
- Consumes: `Source`, `Diagnostics`.
- Produces: `TokenKind`, `Token`, `token_kind_name`, `Lexer` as in the shared block, plus `bool is_reserved_word(TokenKind)` and `bool is_identifier_like(TokenKind)` (true for `Identifier` and every keyword kind).

`TokenKind` members, in this order:

- `EndOfFile`, `Error`, `Identifier`, `Number`, `String`, `Regex`
- Reserved words (never usable as plain identifiers): `KwAs KwBreak KwCase KwCatch KwClass KwConst KwContinue KwDefault KwDelete KwDo KwElse KwExtends KwFalse KwFinally KwFor KwFunction KwIf KwImplements KwImport KwIn KwInstanceof KwInterface KwInternal KwIs KwNew KwNull KwPackage KwPrivate KwProtected KwPublic KwReturn KwSuper KwSwitch KwThis KwThrow KwTrue KwTry KwTypeof KwUse KwVar KwVoid KwWhile KwWith`
- Contextual words are **not** token kinds. `get set each static final override dynamic native namespace include` lex as `Identifier`; the parser compares their text.
- Punctuation: `LParen RParen LBrace RBrace LBracket RBracket Semicolon Comma Dot DotLess Ellipsis DotDot Colon ColonColon Question At`
- Operators: `Plus Minus Star Slash Percent PlusPlus MinusMinus Assign PlusAssign MinusAssign StarAssign SlashAssign PercentAssign Equal NotEqual StrictEqual StrictNotEqual Less Greater LessEqual GreaterEqual ShiftLeft ShiftRight ShiftRightUnsigned ShiftLeftAssign ShiftRightAssign ShiftRightUnsignedAssign Amp Pipe Caret Tilde Bang AmpAmp PipePipe AmpAssign PipeAssign CaretAssign AmpAmpAssign PipePipeAssign`

Lexing rules the tests do not fully determine:

- `DotLess` is `.<` (the opening of a type argument list). The lexer always applies longest match (`>>=`, `>>>`, and so on); splitting a `>>` that closes two type lists is the parser's job (Task 4).
- A number is decimal (`12`, `1.5`, `.5`, `1e3`, `1.5e-3`) or hexadecimal (`0x1F`). A number immediately followed by an identifier character is one `Error` token covering both, with the message `invalid number`.
- A string is delimited by `"` or `'`, may not contain a raw line break, and allows escapes `\n \t \r \b \f \v \0 \\ \' \" \xHH \uHHHH` and a backslash followed by a line break (continuation). Any other escaped character stands for itself. The token covers the quotes.
- `rescan_as_regex` re-lexes from the given slash to the closing unescaped `/` (a `/` inside `[...]` does not close), then any identifier characters as flags. A line break before the close is the error `unterminated regular expression`.
- Identifiers start with a letter, `_`, `$`, or any byte ≥ 0x80, and continue with those or digits. (Non-ASCII bytes are accepted without decoding.)
- Comments `//…` and `/*…*/` are skipped. Errors, each one token of kind `Error` at the start of the construct: `unterminated string`, `unterminated comment`, `unexpected character '<c>'` (for a byte < 0x20 other than whitespace, print it as `\xHH`).

- [ ] **Step 1: Write the failing tests** in `test_lexer.cpp`, each lexing a string and comparing the list of `token_kind_name` values (and texts where given):
  - `punctuation_and_operators`: every punctuation and operator kind above, separated by spaces, yields exactly that kind in order.
  - `longest_match`: `a>>>=b` is `identifier`, `'>>>='`, `identifier`; `a.<b` is `identifier`, `'.<'`, `identifier`; `a...b` is `identifier`, `'...'`, `identifier`; `a..b` is `identifier`, `'..'`, `identifier`.
  - `keywords_and_contextual_words`: `class get set each static override` yields `'class'` then five `identifier`.
  - `numbers`: `0 12 1.5 .5 1e3 1.5e-3 0x1F` are seven `number` tokens with those exact texts; `1.toString` is `number` (text `1`), `'.'`, `identifier`, because a digit must follow the dot for it to belong to the number.
  - `invalid_number`: `12abc` is one `error` token and the diagnostic `invalid number` at offset 0.
  - `strings`: `"a\"b"` and `'it''s'` (two strings) lex with the texts including quotes; `"a\` + line break + `b"` is one string.
  - `unterminated_string`: `"abc` + line break + `x` gives `error` then `identifier`, with `unterminated string` at offset 0.
  - `comments_are_skipped`: `a // c` + line break + `/* d */ b` is two identifiers, and the second has `newline_before` true.
  - `unterminated_comment`: `a /* b` gives `identifier`, `error`, and `unterminated comment` at offset 2.
  - `newline_before`: in `a` + line break + `b c`, only `b` has it set; a line break inside a block comment counts.
  - `regex_rescan`: lexing `/a[/]b/gi` gives `'/'` first; `rescan_as_regex` on it returns a `regular expression` token whose text is the whole input, and the next token is end of file.
  - `unterminated_regex`: `/abc` + line break gives the error `unterminated regular expression` at offset 0.
  - `non_ascii_identifier`: `caf\xC3\xA9 = 1` begins with one identifier of 5 bytes.
  - `control_character`: a `\x01` byte gives `unexpected character '\x01'`.
  - `end_of_file_repeats`: `next()` returns end of file three times in a row on empty input.
- [ ] **Step 2: Run the build.** Expected: FAIL, `lexer.h` not found.
- [ ] **Step 3: Implement `token.cpp` and `lexer.cpp`.** Keywords are recognised by a sorted table and binary search after scanning an identifier.
- [ ] **Step 4: Run** `ctest --test-dir compiler/build --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Commit** `compiler: add tokens and lexer`.

### Task 3: Syntax tree, arena, printer

**Files:**
- Create: `compiler/src/arena.h`, `compiler/src/ast.h`, `compiler/src/ast_printer.{h,cpp}`, `compiler/tests/test_ast.cpp`
- Modify: `compiler/CMakeLists.txt`

**Interfaces:**
- Consumes: `Source`.
- Produces: `Arena`, every node type below, and `print_ast`.

Node design (all in `ast.h`, all trivially destructible, all allocated from the `Arena`):

```cpp
template <class T> struct List { T *items; uint32_t count; };   // arena array

struct Node { NodeKind kind; uint32_t offset; uint32_t end; };  // [offset, end) in the source

// Types
struct TypeRef : Node {            // NodeKind::TypeRef
    enum Form : uint8_t { Any, Void, Named, Applied } form;     // `*`, `void`, `a.b.C`, `Vector.<T>`
    List<std::string_view> name;   // dotted parts, for Named and Applied
    TypeRef *argument;             // for Applied
};

// Expressions: struct Expr : Node. One struct per kind:
//   NumberLit{text} StringLit{text} RegexLit{text} BoolLit{value} NullLit ThisExpr SuperExpr
//   Name{text}
//   ArrayLit{List<Expr*> elements}            (a hole in `[1,,2]` is a null element)
//   ObjectLit{List<ObjectField> fields}       ObjectField{std::string_view key; bool key_is_string; Expr *value;}
//   VectorLit{TypeRef *element_type; List<Expr*> elements}        `new <int>[1, 2]`
//   FunctionExpr{FunctionDecl *function}
//   Member{Expr *object; std::string_view name}                   `a.b`
//   Index{Expr *object; Expr *index}                              `a[b]`
//   TypeApply{Expr *base; TypeRef *argument}                      `Vector.<int>` used as a value
//   Call{Expr *callee; List<Expr*> arguments}
//   New{Expr *callee; List<Expr*> arguments; bool has_arguments}
//   Unary{TokenKind op; Expr *operand}        prefix: + - ! ~ ++ -- typeof void delete
//   Postfix{TokenKind op; Expr *operand}      ++ --
//   Binary{TokenKind op; Expr *left; Expr *right}   includes is, as, in, instanceof, &&, ||, comma
//   Assign{TokenKind op; Expr *target; Expr *value}
//   Conditional{Expr *condition; Expr *then_value; Expr *else_value}

// Statements: struct Stmt : Node.
//   Block{List<Stmt*> statements} ExprStmt{Expr *expr} Empty
//   VarStmt{VarDecl *decl}                    one statement per `var`/`const` keyword
//   If{Expr *condition; Stmt *then_branch; Stmt *else_branch}
//   While{Expr *condition; Stmt *body}  DoWhile{Stmt *body; Expr *condition}
//   For{Stmt *init; Expr *condition; Expr *update; Stmt *body}    init is VarStmt, ExprStmt or null
//   ForIn{bool each; VarDecl *declaration; Expr *target; Expr *collection; Stmt *body}  one of declaration/target
//   Switch{Expr *subject; List<SwitchCase> cases}   SwitchCase{Expr *test /* null for default */; List<Stmt*> body}
//   Break{std::string_view label} Continue{std::string_view label}   empty when absent
//   Return{Expr *value} Throw{Expr *value}
//   Try{Block *body; List<CatchClause> catches; Block *finally_block}
//       CatchClause{std::string_view name; TypeRef *type; Block *body}
//   Labeled{std::string_view label; Stmt *body}
//   FunctionStmt{FunctionDecl *function}      a named function nested in a function

// Declarations
struct Metadata { std::string_view name; List<MetadataArg> args; uint32_t offset; };
struct MetadataArg { std::string_view key /* empty if positional */; std::string_view value; };
struct Modifiers {                // bit set
    enum : uint16_t { Public = 1, Private = 2, Protected = 4, Internal = 8, Static = 16, Final = 32,
                      Override = 64, Dynamic = 128, Native = 256 };
    uint16_t bits;
};
struct Declarator { std::string_view name; TypeRef *type; Expr *initializer; uint32_t offset; };
struct VarDecl : Node { bool is_const; Modifiers modifiers; List<Metadata> metadata; List<Declarator> declarators; };
struct Parameter { std::string_view name; TypeRef *type; Expr *default_value; bool is_rest; uint32_t offset; };
struct FunctionDecl : Node {
    enum Accessor : uint8_t { None, Getter, Setter } accessor;
    std::string_view name;                       // empty for an anonymous function expression
    Modifiers modifiers; List<Metadata> metadata;
    List<Parameter> parameters; TypeRef *return_type;   // null when not written
    Block *body;                                 // null for interface methods and `native` functions
};
struct ClassDecl : Node {
    std::string_view name; Modifiers modifiers; List<Metadata> metadata;
    TypeRef *extends; List<TypeRef*> implements;
    List<Node*> members;                         // VarDecl or FunctionDecl
};
struct InterfaceDecl : Node {
    std::string_view name; Modifiers modifiers; List<Metadata> metadata;
    List<TypeRef*> extends; List<FunctionDecl*> methods;
};
struct Import : Node { List<std::string_view> name; bool wildcard; };     // `import a.b.*;`
struct PackageDecl : Node { List<std::string_view> name; List<Import*> imports; List<Node*> definitions; };
struct Program : Node { PackageDecl *package; /* may be null */ List<Import*> imports; List<Node*> definitions; };
// `definitions` hold ClassDecl, InterfaceDecl, FunctionDecl, VarDecl. Program's are the file's helpers,
// declared outside the package block.
```

`NodeKind` has one member per struct above.

Printer format, which the golden files depend on:

- Every node is one line of the form `<indent><Kind> <attributes>`, with no parentheses. Its children follow on later lines, indented by two more spaces.
- Kind is the struct name. Attributes, in the order the struct declares them, are printed as `name=value` for strings (double-quoted, with `\` and `"` escaped), operators (the token's spelling, quoted), booleans (only when true, as the bare field name), and modifiers (`mods=public,static` in the bit order above, omitted when empty).
- A child that is null is not printed, except where position matters: `If` prints `else:` before the else branch; `For` prints `init:`, `condition:`, `update:` labels before those children when present; `Conditional`, `Binary`, `Assign` print children in order with no labels; `ArrayLit` prints `hole` for a null element.
- List-valued non-node fields print inline: `name="a.b.C"` for dotted names; a `Metadata` prints as its own child line `Metadata name="Export" args=[key="v", "positional"]`; a `Parameter` as `Parameter name="x" rest` with its type and default as children; a `Declarator` as `Declarator name="x"` with type and initializer as children; `ObjectField` as `Field key="k"` (plus `string` when the key was a string literal) with the value as its child; `SwitchCase` as `Case` or `Default` with the test (if any) then the body statements as children; `CatchClause` as `Catch name="e"` with type then body.
- A `TypeRef` prints as one line: `Type *`, `Type void`, `Type a.b.C`, or `Type Vector.<int>` (nested arguments inline).
- After the kind and attributes, every line ends with ` @<line>:<column>` of the node's start.

- [ ] **Step 1: Write the failing tests** in `test_ast.cpp`. Build these trees by hand with the `Arena` and compare `print_ast` output exactly:
  - `prints_binary`: a `Program` with one top-level `VarDecl` (`var x = 1 + 2`, offsets set so `var` is at 1:1, `x` at 1:5, `1` at 1:9, `2` at 1:13) prints

    ```
    Program @1:1
      VarDecl @1:1
        Declarator name="x" @1:5
          Binary op="+" @1:9
            NumberLit text="1" @1:9
            NumberLit text="2" @1:13
    ```
  - `prints_type_forms`: the four `TypeRef` forms print as `Type *`, `Type void`, `Type flash.events.Event`, `Type Vector.<Vector.<int>>`.
  - `prints_modifiers_and_flags`: a `FunctionDecl` named `f`, `Public|Static|Override`, getter, prints `FunctionDecl name="f" getter mods=public,static,override`.
  - `escapes_strings`: a `StringLit` whose text is `"a\"b"` (as written in source) prints `text="\"a\\\"b\""`.
  - `arena_copies_and_survives_growth`: 10,000 `make<NumberLit>()` calls return distinct pointers that all still hold the values written to them; `copy("abc")` returns a view equal to `"abc"` at a different address from the argument.
- [ ] **Step 2: Run the build.** Expected: FAIL, `ast.h` not found.
- [ ] **Step 3: Implement `arena.h`** (64 KB chunks, bump pointer, alignment respected, chunks freed in the destructor), **`ast.h`, and the printer.**
- [ ] **Step 4: Run the tests.** Expected: PASS.
- [ ] **Step 5: Commit** `compiler: add syntax tree, arena and printer`.

### Task 4: Parser core, expressions and types

**Files:**
- Create: `compiler/src/parser.h`, `compiler/src/parser.cpp`, `compiler/src/parser_expr.cpp`, `compiler/tests/test_parser.cpp`
- Modify: `compiler/CMakeLists.txt`

**Interfaces:**
- Consumes: `Lexer`, `Arena`, `Diagnostics`, all of `ast.h`, `print_ast`.
- Produces: `class Parser` (declared in `parser.h`, members split across the `parser*.cpp` files) and, for tests in this task only, `Expr *parse_expression_for_test(const Source &, Arena &, Diagnostics &)` which parses one expression and requires end of file after it. `parse()` itself arrives in Task 6.
- `Parser` private helpers later tasks rely on: `const Token &peek()`, `const Token &peek2()` (one further), `Token advance()`, `bool at(TokenKind)`, `bool at_word(std::string_view)` (an `Identifier` with that text), `bool accept(TokenKind)`, `Token expect(TokenKind, const char *p_what)` (reports `expected <what> but found <token>` once and returns a zero-length token at the current position without consuming), `void error_at(const Token &, std::string)`, `std::string describe(const Token &)` (`'<text>'` for most tokens, `end of file`, `a line break` is not a token), `bool enter()` / `void leave()` for the depth cap (on overflow reports `code is nested too deeply` once and returns false), `Expr *parse_expression()` (includes the comma operator), `Expr *parse_assignment()` (no comma), `TypeRef *parse_type()`, and `std::string_view parse_property_name(const char *p_what)` which accepts any identifier-like token (rule: reserved words are names after a dot).

Grammar decisions:

- Precedence, lowest to highest: comma; assignment (right-associative; `= += -= *= /= %= <<= >>= >>>= &= |= ^= &&= ||=`); conditional `?:`; `||`; `&&`; `|`; `^`; `&`; equality (`== != === !==`); relational (`< > <= >= is as in instanceof`); shift (`<< >> >>>`); additive; multiplicative; unary; postfix; call/member/`new`.
- `parse_assignment` takes a flag `allow_in`. The initialiser clause of a `for` header is parsed with it off so that `for (var k in o)` and `for (x in o)` are not read as the `in` operator.
- Postfix `++` and `--` do not apply if a line break precedes them (the operator's token has `newline_before`).
- A `Slash` or `SlashAssign` token in a position where an expression must begin is rescanned as a regular expression.
- `new` takes a member expression (no calls) and optional arguments: `new a.B(1)`, `new Vector.<int>()`, `new Foo`. `new <T>[...]` is a `VectorLit`.
- After a dot: `.<` starts a type application (`TypeApply`); otherwise a property name by `parse_property_name`. `..`, `::` and `@` are E4X or namespace syntax: report the matching message from Task 6's table and continue as if a plain dot or name had been written.
- Object-literal keys: an identifier-like token, a string, or a number.
- Function expressions: `function (params):Type { body }` and the named form `function name(...)`. Until Task 5 supplies statements, `parse_function_body()` is declared here and defined in Task 5; this task's tests use no function expressions.
- Closing a type argument: `parse_type` after `.<` and the inner type calls `expect_type_close()`, which accepts `Greater`, or splits a `ShiftRight`, `ShiftRightUnsigned`, `GreaterEqual`, `ShiftRightAssign` or `ShiftRightUnsignedAssign` token: it consumes one `>` and leaves the remainder as the current token (`>>` leaves `>`, `>>>` leaves `>>`, `>=` leaves `=`, `>>=` leaves `>=`, `>>>=` leaves `>>=`).
- Type names: `*`, `void`, or a dotted name of identifiers, optionally followed by `.<` type `>`.

- [ ] **Step 1: Write the failing tests** in `test_parser.cpp`, each comparing `print_ast`-style output of one expression (write a helper that prints an `Expr` subtree with the same printer) or the diagnostics:
  - `precedence`: `1 + 2 * 3` is `Binary +` with right child `Binary *`; `a = b = c` nests to the right; `a ? b : c ? d : e` nests in the else; `a || b && c` puts `&&` under `||`; `-a.b` is `Unary -` over `Member`; `a is B as C` is left-associative.
  - `calls_and_members`: `a.b(c)[d].e` prints `Member e` over `Index` over `Call` over `Member b` over `Name a`.
  - `reserved_word_after_dot`: `event.type`, `obj.default`, `x.class` each parse as `Member` with that name and no diagnostics.
  - `new_forms`: `new Foo`, `new a.B(1, 2)`, `new Vector.<int>()`, `new <int>[1, 2]` give `New`, `New` with two arguments, `New` over `TypeApply`, and `VectorLit`.
  - `nested_type_arguments`: `new Vector.<Vector.<int>>()` parses with no diagnostics and prints `Type Vector.<int>` inside the outer `TypeApply`; `a >> b`, `a >>> b`, `a >= b`, `a >>= b` are still operators.
  - `literals`: `[1, , "x"]` has a hole; `{ a: 1, "b": 2, default: 3, 4: 5 }` has four fields with `b` marked `string`; `true`, `false`, `null`, `this` print as their kinds.
  - `regex_where_an_expression_starts`: `/ab+c/gi` is a `RegexLit`; `a / b / c` is two divisions; `x = /=/` assigns a regex.
  - `postfix_needs_same_line`: `a++` is `Postfix`; in `a` + line break + `++b`, parsing one expression stops after `a` (the test helper then reports `expected end of file but found '++'`).
  - `error_missing_operand`: `1 +` reports `1:4: error: expected an expression but found end of file`.
  - `error_unclosed_paren`: `(1 + 2` reports `expected ')' but found end of file`.
  - `depth_cap`: 300 nested parentheses report `code is nested too deeply` exactly once and return.
  - `unsupported_e4x`: `a..b`, `a.@b`, `a::b` each report their Task 6 message and still return a tree.
- [ ] **Step 2: Run the build.** Expected: FAIL, `parser.h` not found.
- [ ] **Step 3: Implement.** A two-token lookahead buffer filled from the lexer; binary operators by precedence climbing over a table of (`TokenKind`, level); `is`, `as`, `in`, `instanceof` are relational operators recognised by kind.
- [ ] **Step 4: Run the tests.** Expected: PASS.
- [ ] **Step 5: Commit** `compiler: parse expressions and type annotations`.

### Task 5: Statements

**Files:**
- Create: `compiler/src/parser_stmt.cpp`
- Modify: `compiler/src/parser.h`, `compiler/tests/test_parser.cpp`, `compiler/CMakeLists.txt`

**Interfaces:**
- Consumes: the `Parser` helpers from Task 4.
- Produces: `Stmt *parse_statement()`, `Block *parse_block()`, `Block *parse_function_body()` (a block), `VarDecl *parse_variable_declaration(Modifiers, List<Metadata>, bool p_allow_in)` (after the caller has seen `var` or `const`; parses declarators only, not the terminator), `FunctionDecl *parse_function_rest(Modifiers, List<Metadata>, bool p_require_body)` (called with the current token just after `function`; handles `get`/`set`, name, parameters, return type, body), `void end_statement()`, `void synchronize()`, and for tests `Stmt *parse_statement_for_test(...)`.

Rules:

- **Ending a statement (`end_statement`).** Consume a `;` if present. Otherwise the statement is still ended if the next token is `}`, end of file, or has `newline_before`. Otherwise report `expected ';' but found <token>`.
- **`return`, `break`, `continue`, `throw`:** an operand or label is taken only if it is on the same line. `throw` with nothing on its line reports `expected an expression after 'throw'`.
- **A statement beginning with an identifier followed by `:`** is a labelled statement.
- **`for`:** after `(`, `each` (the contextual word) selects for-each and requires `in`. Otherwise parse the initialiser with `in` disallowed; if the next token is `in`, it is a for-in whose left side must be a single declarator without initialiser or an assignable expression (`Name`, `Member`, `Index`), else `invalid left-hand side in for-in`.
- **`switch`:** `case expr:` and `default:` clauses; a second `default` reports `more than one 'default' in switch`.
- **`try`** needs at least one `catch` or a `finally`: otherwise `'try' needs a 'catch' or 'finally'`. A catch parameter may have a type.
- **Nested named functions** are `FunctionStmt`. `get` and `set` are only accessors at class level; inside a function `function get()` is a function named `get`.
- **`with (...)`** reports the Task 6 message, then parses and discards the parenthesised expression and the statement.
- **Recovery (`synchronize`).** After an error inside a statement, skip tokens until one that is `;` (consume it), or `}`, or begins a statement or declaration (`var const function class interface if for while do switch return break continue throw try import package` or a modifier word) at a position with `newline_before`. Always consume at least one token so parsing advances.
- A function parameter is `name[:Type][= default]`, or `...name[:Type]` which must be last (`a rest parameter must be last`).

- [ ] **Step 1: Write the failing tests** (statement printed with the shared printer, or diagnostics):
  - `var_forms`: `var a:int = 1, b;` has two declarators; `const K:Number = 2;` is `VarDecl const`.
  - `if_else_chain`: `if (a) b(); else if (c) d(); else e();` nests the second `If` under `else:`.
  - `loops`: `for (var i:int = 0; i < n; i++) {}` prints `init:`, `condition:`, `update:`; `for (;;) {}` has none of them; `for (var k:String in o) {}` and `for (k in o) {}` are `ForIn`; `for each (var v:* in list) {}` is `ForIn each`; `while (a) b();` and `do { } while (a);` parse.
  - `in_operator_outside_for_header`: `x = k in o;` is an `Assign` whose value is `Binary in`; `for (var i:int = (a in b) ? 1 : 0; ;) {}` is a `For`.
  - `switch_cases`: `switch (x) { case 1: a(); break; case 2: default: b(); }` prints two `Case` and one `Default`, the second `Case` empty.
  - `labels`: `outer: for (;;) { continue outer; }` prints `Labeled label="outer"` and `Continue label="outer"`.
  - `try_forms`: `try { } catch (e:Error) { } finally { }` prints `Catch name="e"` with `Type Error`; `try { }` alone reports `'try' needs a 'catch' or 'finally'`.
  - `closures`: `var f:Function = function (a:int, b:int = 2, ...rest):void { return; };` has three parameters, the last `rest`; `function inner():void {}` inside a block is a `FunctionStmt`.
  - `line_break_ends_statement`: the three lines `var a = 1`, `var b = 2`, `a = b` in a block give three statements and no diagnostics.
  - `return_on_its_own_line`: `return` + line break + `x;` in a block gives `Return` with no value, then `ExprStmt`.
  - `continuation_lines`: `a = b` + line break + `(c)` is one statement, a `Call`; `a = b` + line break + `[0]` is an `Index`.
  - `missing_semicolon_same_line`: `a = 1 b = 2` reports `expected ';' but found 'b'` at column 7 and still yields two statements.
  - `recovery_reports_later_errors`: a block with `var = 1;` on line 1, `ok();` on line 2, `x = ;` on line 3 reports exactly two diagnostics, on lines 1 and 3, and the block contains the `ok()` call.
  - `rest_must_be_last`: `function (...a, b) {}` reports `a rest parameter must be last`.
  - `unterminated_block`: `{ a();` reports `expected '}' but found end of file` once.
- [ ] **Step 2: Run the build.** Expected: FAIL, undefined `parse_statement_for_test`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run the tests.** Expected: PASS, including every Task 4 test.
- [ ] **Step 5: Commit** `compiler: parse statements`.

### Task 6: Declarations, packages, and `parse()`

**Files:**
- Create: `compiler/src/parser_decl.cpp`
- Modify: `compiler/src/parser.cpp`, `compiler/src/parser.h`, `compiler/tests/test_parser.cpp`, `compiler/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 4 and 5.
- Produces: `Program *parse(const Source &, Arena &, Diagnostics &)`.

Rules:

- A file is: optional `package [dotted.name] { imports and definitions }`, then imports and definitions outside it (the file's helper classes and functions). A second `package` reports `only one package block is allowed in a file`.
- A definition is: zero or more metadata tags, zero or more modifiers, then `class`, `interface`, `function`, `var` or `const`. At package or file level, a statement that is not a definition reports `expected a class, interface, function or variable definition` and is skipped by `synchronize`.
- **Metadata:** `[` Name optional `(` args `)` `]` immediately before a definition or member. An arg is `key = value` or a bare value; a value is a string, number, or identifier-like word, stored as written (strings without their quotes). To tell metadata from an array literal at statement start inside a class body: a `[` where a member is expected is always metadata.
- **Modifiers** are `public private protected internal` (reserved words) and `static final override dynamic native` (contextual). Repeating one reports `duplicate '<word>'`; two of the four access words report `only one access modifier is allowed`. Which modifiers suit which definition is checked in stage 1b.
- **Class:** `class Name [extends Type] [implements Type, Type] { members }`. Members are `var`/`const` declarations and functions, each with metadata and modifiers. `function get name()` / `function set name(v)` are accessors when `get`/`set` is followed by an identifier-like token; `function get()` is a method named `get`. A member function has a body unless it is `native`. Anything else in a class body reports `expected a member definition` and is skipped.
- **Interface:** `interface Name [extends Type, Type] { methods }`. Methods are `function` signatures (accessors allowed) ended as statements, with no body; a body reports `interface methods cannot have a body`. Modifiers on interface methods report `interface methods cannot have modifiers`.
- **Import:** `import a.b.C;` or `import a.b.*;`.
- **Unsupported constructs.** Each reports its message at the construct's first token, then parsing continues:

  | Construct | Message |
  |---|---|
  | `with (...)` statement | `the 'with' statement is not supported` |
  | `namespace x;` or `namespace x = "..."` definition | `namespaces are not supported` |
  | `use namespace x;` | `namespaces are not supported` |
  | `a::b` | `namespaces are not supported` |
  | `include "file"` | `'include' is not supported` |
  | `default xml namespace = ...` | `E4X (XML syntax) is not supported` |
  | `<` where an expression must begin | `E4X (XML syntax) is not supported` |
  | `a..b`, `a.@b`, `@b` | `E4X (XML syntax) is not supported` |

  After `<` at expression start, skip to the end of the line.
- The program's node range covers the whole file.

- [ ] **Step 1: Write the failing tests:**
  - `empty_file`: empty text gives `Program @1:1` and no diagnostics.
  - `package_with_class`: a package `game.actors` containing `import godot.Node2D;` and `public class Player extends Node2D implements IHit, IMove { }` prints `PackageDecl name="game.actors"`, `Import name="godot.Node2D"`, `ClassDecl name="Player" mods=public` with `Type Node2D` and both interface types.
  - `members`: a class with `[Export] public var speed:Number = 200;`, `private static const MAX:int = 3;`, `override public function _process(delta:Number):void { }`, `public function get hp():int { return _hp; }`, `public function set hp(v:int):void { }`, and `function get():int { return 0; }` prints the metadata line, modifiers, `getter`/`setter` flags, and a plain function named `get`.
  - `metadata_forms`: `[Signal(name="hit", args="amount:int")]`, `[Tool]`, `[Embed(source="a.png", mimeType="image/png")]`, `[Foo(1, bar)]` print their args as specified in Task 3.
  - `interface_forms`: `interface IHit extends IA, IB { function hit(n:int):void; function get hp():int; }` prints two methods with no bodies; a body reports `interface methods cannot have a body`.
  - `helpers_outside_package`: a file with a package, then `class Helper { }` and `function util():void { }` after it, lists both under `Program`, not under the package.
  - `wildcard_import`: `import flash.utils.*;` prints `Import name="flash.utils" wildcard`.
  - `modifier_errors`: `public public class A {}` reports `duplicate 'public'`; `public private class A {}` reports `only one access modifier is allowed`.
  - `stray_statement_at_top_level`: `x = 1;` then a valid class: one diagnostic `expected a class, interface, function or variable definition`, and the class is still in the tree.
  - `second_package`: two package blocks report `only one package block is allowed in a file`.
  - `unsupported`: each row of the table above produces exactly its message at the construct's first token, and a valid class after it still parses.
  - `whole_program_never_crashes`: for each of these inputs `parse` returns non-null and at least one diagnostic: `package {`, `class`, `class A { function`, `class A { var x:Vector.<`, `[`, `}}}}`, 5,000 `{` characters, and the bytes `\x00\x01\x02`.
- [ ] **Step 2: Run the build.** Expected: FAIL, undefined `parse`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run the tests.** Expected: PASS, with every earlier test.
- [ ] **Step 5: Commit** `compiler: parse packages, classes, interfaces and members`.

### Task 7: `kas3c`, golden files, corpus

**Files:**
- Create: `compiler/tools/kas3c.cpp`, `compiler/tests/run_golden.py`, `compiler/tests/golden/ok/*.as` with `*.ast`, `compiler/tests/golden/errors/*.as` with `*.diag`, `compiler/tests/corpus/*.as`
- Modify: `compiler/CMakeLists.txt` (target `kas3c`; CTest entries `golden` and `cli`), `compiler/README.md`

**Interfaces:**
- Consumes: `read_file`, `Source`, `Diagnostics`, `Arena`, `parse`, `print_ast`.
- Produces: the command line:
  - `kas3c parse <file>...` parses each file, prints diagnostics for all of them to standard error, and exits 0 if there were none, 1 if any file had errors, 2 for a usage problem or an unreadable file (`kas3c: cannot read <path>` on standard error; other files are still processed).
  - `kas3c parse --dump-ast <file>...` also prints each file's tree to standard output, preceded by `# <path>` when more than one file is given.
  - `kas3c --version` prints `kas3c 0.1.0`. `kas3c` with no arguments, `--help`, or an unknown command prints usage (to standard output for `--help` with exit 0; otherwise to standard error with exit 2). Usage text begins `usage: kas3c parse [--dump-ast] <file>...`.
- `run_golden.py <kas3c> <tests dir> [--update]`: for each `golden/ok/X.as`, `kas3c parse --dump-ast` must exit 0 with standard output equal to `X.ast`; for each `golden/errors/X.as`, it must exit 1 with standard error equal to `X.diag` (paths in diagnostics are relative to the tests dir, so the runner runs `kas3c` with that as its working directory); every `corpus/*.as` must exit 0 with empty standard error. `--update` rewrites the `.ast` and `.diag` files instead of comparing. Prints one line per failure with a unified diff, then `<n> passed, <m> failed`; exits 1 on any failure.

- [ ] **Step 1: Write the failing CLI test** as a CTest script entry `cli` (a small Python file `tests/test_cli.py` taking the `kas3c` path): asserts the exit codes and first output lines above for `--version`, `--help`, no arguments, an unknown command, a missing file (exit 2, message), one good file (exit 0, no output), one bad file (exit 1, a diagnostic line matching `^.+:\d+:\d+: error: `), and good plus bad together (exit 1, and `--dump-ast` prints two `# ` headers).
- [ ] **Step 2: Run** `ctest --test-dir compiler/build -R cli`. Expected: FAIL (no `kas3c`).
- [ ] **Step 3: Implement `kas3c.cpp` and `run_golden.py`.**
- [ ] **Step 4: Write the golden inputs.** `golden/ok/`: one file per area, each under 40 lines: `expressions.as`, `statements.as`, `classes.as`, `interfaces.as`, `metadata.as`, `closures.as`, `vectors.as`, `semicolons.as` (the line-break cases from the Review Focus), `keywords_as_names.as`, `regex.as`. `golden/errors/`: `unterminated_string.as`, `unterminated_comment.as`, `unterminated_regex.as`, `unterminated_block.as`, `missing_semicolon.as`, `unsupported.as` (every row of Task 6's table), `recovery.as` (three separate errors in one file), `too_deep.as`. Generate the expected files with `--update`, then **read every generated `.ast` and `.diag` and confirm it is what the rules in this plan say it should be** before committing; a golden file that records a bug is worse than no test.
- [ ] **Step 5: Write the corpus:** the AS3 programs from the speed spike, which are real-shaped code against the Godot API. Take them from branch `spike/stage-0b` (`git show spike/stage-0b:spike/as3/<Name>.as`): `Empty MoveNodes PerNodeChild PerNodeProcess SignalTraffic NBody Sieve ArrayConversion Saxpy Shapes Alloc Probe V2Probe`, plus `v2/RayQueries.as` saved as `RayQueries.as`. They must parse with no diagnostics. If one does not, the parser is wrong unless the file uses something this plan lists as unsupported; fix the parser, not the corpus.
- [ ] **Step 6: Run** `ctest --test-dir compiler/build --output-on-failure`. Expected: every test passes, including `golden` and `cli`.
- [ ] **Step 7: Update `compiler/README.md`** with the command-line usage and how to add a golden test, **and commit** `compiler: add kas3c parse, golden tests and corpus`.

---

## Amendments (added after review, before implementation)

These are binding and extend the tasks named.

- **Scope reminder.** This plan ends at `kas3c parse`. Nothing runs inside Godot until the interpreter stage.
- **Task 4, lookahead and regular expressions.** The second lookahead token is fetched lazily, only when `peek2()` is called, and `rescan_as_regex` rewinds the lexer to the slash's offset, discarding anything lexed after it. Otherwise the lexer would run past a `/` before the parser knows it begins a regular expression. Tests: `x = /"/;` and `x = /'/;` parse with no diagnostics.
- **Task 4, `:*=`.** Where a type is expected, a `StarAssign` token is the type `*` followed by `=`: `parse_type` consumes the star and leaves `Assign` as the current token. Tests (run in Task 5 where declarations exist): `var x:*=1;` and `function (a:*=null):* {}` parse with no diagnostics.
- **Task 4, `in` inside a `for` header.** The no-`in` restriction applies only at the top level of the initialiser. It is lifted inside parentheses, brackets, call arguments, array and object literals, and function bodies. Tests: `for (var i:int = [a in b].length; ;) {}`, `for (var i:int = f(a in b); ;) {}`, `for (var o:Object = { k: a in b }; ;) {}`.
- **Task 4, tree depth.** Constructs built in a loop rather than by recursion (left-associative binary chains, member/index/call chains) are limited to 4,096 links; beyond that, report `expression is too long` once and stop extending the chain. Together with the recursion cap this bounds the depth of the tree, so later passes and the printer can recurse safely. Tests: a 100,000-term `1+1+…`, `a.b.b…` and `a()()…` each report `expression is too long` once and return; `--dump-ast` on a 4,000-term sum prints without crashing (Task 7).
- **Tasks 4–6, one mistake, one diagnostic.** The parser reports nothing at an `Error` token: the lexer already did. It treats the token as an expression or skips it, whichever keeps parsing moving. Tests: `var s = "abc` + line break + `var t = 1;` reports exactly `unterminated string`; `x = 12abc;` reports exactly `invalid number`.
- **Tasks 5–6, a `[` on a new line after an initialiser.** In a class body and at package or file level, a `[` that begins a line ends the preceding variable initialiser, because it starts a metadata tag for the next member. Inside function bodies the `[` continues the expression, as before. Test: a class containing `public var a:int = 1` + line break + `[Export]` + line break + `public var b:int = 2` has two members, the second with metadata, and no diagnostics.
- **Tasks 5–6, `import` inside a class or function body** is valid AS3. In a function body it is the statement `ImportStmt{Import *import}` (a new node); in a class body the `Import` node is added to `members`. Test: both parse with no diagnostics and print an `Import` line.
- **Task 6, malformed input in bulk.** A test parses every byte-prefix of every file in `tests/corpus/` (added to this task from Task 7's list so the test has inputs) and asserts that `parse` returns a tree every time. It prints nothing per prefix.
- **Task 7, sanitizers.** `compiler/CMakeLists.txt` has an option `KAS3_SANITIZE` adding `-fsanitize=address,undefined` to everything. Before the final commit, the whole suite is run once in a separate build directory with it on, and must pass.
- **Committing.** Stage `compiler` and `docs` by name. Never `git add -A` on this branch: the working directory holds ignored and unignored leftovers from other branches.
