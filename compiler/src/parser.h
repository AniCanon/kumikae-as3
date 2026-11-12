// Tokens to a syntax tree, by recursive descent. The parser recovers from errors so that one run reports
// many, and it never fails to return a tree.
#pragma once

#include "arena.h"
#include "ast.h"
#include "diagnostics.h"
#include "lexer.h"
#include "source.h"

#include <string>
#include <string_view>
#include <vector>

namespace kas3 {

// Never returns null, however malformed the source.
Program *parse(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics);

// For tests: one expression, or one statement, which must be followed by the end of the file.
Expr *parse_expression_for_test(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics);
Stmt *parse_statement_for_test(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics);

class Parser {
public:
	// Recursion is capped at this many nested constructs.
	static const int MAX_DEPTH = 256;
	// Trees built in a loop (operator chains, member and call chains) are capped at this height.
	static const uint32_t MAX_HEIGHT = 4096;

	Parser(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics);

	Program *parse_program();
	Expr *parse_single_expression();
	Stmt *parse_single_statement();

private:
	const Source &source_;
	Arena &arena_;
	Diagnostics &diagnostics_;
	Lexer lexer_;

	Token current_;
	Token next_;
	bool has_next_ = false; // The second token is fetched only on demand: see peek2().
	uint32_t previous_end_ = 0; // Where the last consumed token ended.

	int depth_ = 0;
	bool too_deep_reported_ = false;
	bool too_long_reported_ = false;
	uint32_t last_height_ = 1; // Height of the expression most recently returned.

	bool panic_ = false; // An error was reported; the enclosing list should resynchronise.
	bool has_error_offset_ = false;
	uint32_t last_error_offset_ = 0; // One diagnostic per position: later ones at the same place are dropped.

	bool no_in_ = false; // Inside a `for` header's first clause, where `in` is not an operator.
	bool bracket_ends_initializer_ = false; // In member and top-level initialisers: a `[` on a new line is metadata.

	// ---- tokens (parser.cpp) ----
	const Token &peek() const { return current_; }
	const Token &peek2();
	Token advance();
	bool at(TokenKind p_kind) const { return current_.kind == p_kind; }
	bool at_word(std::string_view p_word) const;
	bool accept(TokenKind p_kind);
	Token expect(TokenKind p_kind, const char *p_what);
	std::string_view text(const Token &p_token) const { return lexer_.text(p_token); }
	std::string describe(const Token &p_token) const;
	void error_at(uint32_t p_offset, std::string p_message, bool p_panic = true);
	void error_expected(const char *p_what);
	bool enter();
	void leave() { depth_--; }
	void report_too_long(uint32_t p_offset);
	void synchronize();
	void skip_to_statement_end();
	void skip_line();
	void note_current();

	template <class T>
	T *node(uint32_t p_offset) {
		return new_node<T>(arena_, p_offset, p_offset);
	}
	template <class T>
	T *finish(T *p_node) {
		p_node->end = previous_end_ > p_node->offset ? previous_end_ : p_node->offset;
		return p_node;
	}
	template <class T>
	List<T> to_list(const std::vector<T> &p_items) {
		List<T> list;
		list.count = (uint32_t)p_items.size();
		list.items = arena_.make_array<T>(p_items.size());
		for (size_t i = 0; i < p_items.size(); i++) {
			list.items[i] = p_items[i];
		}
		return list;
	}

	// ---- expressions and types (parser_expr.cpp) ----
	Expr *parse_expression();
	Expr *parse_assignment();
	Expr *parse_conditional();
	Expr *parse_binary(int p_min_level);
	Expr *parse_unary();
	Expr *parse_postfix();
	Expr *parse_call_member(bool p_allow_calls);
	Expr *parse_primary();
	Expr *parse_new();
	Expr *parse_array_literal();
	Expr *parse_object_literal();
	Expr *parse_function_expression();
	Expr *missing_expression();
	List<Expr *> parse_arguments(uint32_t *r_height);
	TypeRef *parse_type();
	void expect_type_close();
	std::string_view parse_property_name(const char *p_what);
	List<std::string_view> parse_dotted_name(const char *p_what, bool *r_wildcard);

	// ---- statements (parser_stmt.cpp) ----
	Stmt *parse_statement();
	Stmt *parse_statement_inner();
	Block *parse_block();
	Block *parse_function_body();
	Stmt *parse_if();
	Stmt *parse_for();
	Stmt *parse_switch();
	Stmt *parse_try();
	VarDecl *parse_variable_declaration(Modifiers p_modifiers, List<Metadata> p_metadata, bool p_allow_in);
	FunctionDecl *parse_function_rest(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata,
			bool p_body_expected, bool p_allow_accessor, bool p_name_required);
	Import *parse_import();
	bool parse_unsupported_statement();
	void end_statement();
	static bool begins_statement(TokenKind p_kind);

	// ---- declarations (parser_decl.cpp) ----
	void parse_definitions(bool p_in_package, std::vector<Import *> &r_imports, std::vector<Node *> &r_definitions,
			PackageDecl **r_package);
	List<Metadata> parse_metadata_tags();
	Modifiers parse_modifiers();
	ClassDecl *parse_class(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata);
	InterfaceDecl *parse_interface(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata);
	PackageDecl *parse_package();
};

} // namespace kas3
