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
	// No syntax tree is taller than about this, so later passes may recurse over it safely.
	static const uint32_t MAX_HEIGHT = 512;
	// Chains built in a loop (operators, members, calls) stop growing here, leaving room for what encloses them.
	static const uint32_t MAX_CHAIN = 448;
	// `Vector.<Vector.<...>>` may nest this far.
	static const int MAX_TYPE_DEPTH = 32;

	Parser(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics,
			std::vector<TextRegion> *p_regions = nullptr);

	Program *parse_program();
	Expr *parse_single_expression();
	Stmt *parse_single_statement();

private:
	// Restores a parser flag when a scope ends.
	struct Restore {
		bool &flag;
		bool saved;
		Restore(bool &p_flag, bool p_value) :
				flag(p_flag), saved(p_flag) {
			flag = p_value;
		}
		~Restore() { flag = saved; }
	};

	const Source &source_;
	Arena &arena_;
	Diagnostics &diagnostics_;
	Lexer lexer_;

	Token current_;
	Token next_;
	bool has_next_ = false; // The second token is fetched only on demand: see peek2().
	uint32_t previous_end_ = 0; // Where the last consumed token ended.

	int depth_ = 0;
	int type_depth_ = 0;
	bool too_deep_reported_ = false;
	bool too_long_reported_ = false;
	bool aborted_ = false; // Nesting ran past the cap: the rest of the file is not parsed.
	uint32_t last_height_ = 1; // Height of the expression most recently returned.

	uint32_t statement_depth_ = 0; // Statements enclosing the current position, within the current function.
	uint32_t body_height_ = 0; // Tallest thing seen so far in the current function body.
	uint32_t function_height_ = 1; // Height of the function most recently parsed.
	int brace_depth_ = 0; // `{` consumed and not yet closed.
	bool eof_error_ = false; // An error at the end of the file was reported; the rest would follow from it.
	bool ended_in_lexer_error_ = false; // The last token before the end was one the lexer rejected.

	bool panic_ = false; // An error was reported; the enclosing list should resynchronise.
	bool has_error_offset_ = false;
	uint32_t last_error_offset_ = 0; // One diagnostic per position: later ones at the same place are dropped.

	bool no_in_ = false; // Inside a `for` header's first clause, where `in` is not an operator.
	bool bracket_ends_initializer_ = false; // In member and top-level initialisers: a `[` on a new line is metadata.

	// Tokens. In parser.cpp.
	const Token &peek() const { return current_; }
	const Token &peek2();
	bool at_namespace_attribute();
	Token advance();
	bool at(TokenKind p_kind) const { return current_.kind == p_kind; }
	bool at_word(std::string_view p_word) const;
	bool accept(TokenKind p_kind);
	Token expect(TokenKind p_kind, const char *p_what);
	void expect_close(TokenKind p_kind, const Token &p_opener);
	bool at_modifier_word() const;
	void abort_too_deep();
	bool grow(uint32_t p_height);
	void note_height();
	std::string_view text(const Token &p_token) const { return lexer_.text(p_token); }
	std::string describe(const Token &p_token) const;
	void error_at(uint32_t p_offset, std::string p_message, bool p_panic = true);
	void error_expected(const char *p_what);
	bool enter();
	void leave() { depth_--; }
	void report_too_long(uint32_t p_offset);
	void synchronize(int p_level);
	void skip_to_statement_end();
	void skip_xml();
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

	// Expressions. In parser_expr.cpp.
	Expr *parse_expression();
	Expr *parse_assignment();
	Expr *parse_conditional();
	Expr *parse_binary(int p_min_level);
	Expr *parse_unary();
	Expr *parse_postfix();
	Member *parse_member_link(Expr *p_object);
	Expr *parse_call_member(bool p_allow_calls);
	Expr *parse_literal_token();
	Expr *parse_regex();
	Expr *parse_parenthesized();
	Expr *parse_primary();
	Expr *parse_new();
	Expr *missing_expression();
	List<Expr *> parse_arguments(uint32_t *r_height);
	std::string_view parse_property_name(const char *p_what);

	// Literals. In parser_literals.cpp.
	Expr *parse_array_literal();
	Expr *parse_object_literal();
	Expr *parse_function_expression();

	// Types and dotted names. In parser_types.cpp.
	TypeRef *parse_type();
	void expect_type_close();
	List<std::string_view> parse_dotted_name(const char *p_what, bool *r_wildcard, List<uint32_t> *r_offsets);

	// Statements. In parser_stmt.cpp.
	Stmt *parse_statement();
	Stmt *parse_statement_inner();
	Stmt *parse_while();
	Stmt *parse_do_while();
	Stmt *parse_break();
	Stmt *parse_continue();
	Stmt *parse_return();
	Stmt *parse_throw();
	Stmt *parse_with();
	Block *parse_block();
	Stmt *parse_if();
	Stmt *parse_for();
	Stmt *parse_switch();
	Stmt *parse_try();
	VarDecl *parse_variable_declaration(Modifiers p_modifiers, List<Metadata> p_metadata, bool p_allow_in);
	Import *parse_import();
	bool parse_unsupported_statement();
	void end_statement();
	static bool begins_statement(TokenKind p_kind);

	// Functions. In parser_functions.cpp.
	Block *parse_function_body();
	uint32_t parse_function_name(FunctionDecl *p_function, Modifiers p_modifiers, bool p_allow_accessor,
			bool p_name_required);
	uint32_t parse_parameters(FunctionDecl *p_function);
	FunctionDecl *parse_function_rest(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata,
			bool p_body_expected, bool p_allow_accessor, bool p_name_required);

	// Declarations. In parser_decl.cpp.
	void parse_definitions(bool p_in_package, std::vector<Import *> &r_imports, std::vector<Node *> &r_definitions,
			PackageDecl **r_package);
	List<Metadata> parse_metadata_tags();
	Modifiers parse_modifiers();
	void parse_class_member(uint32_t p_start, std::vector<Node *> &r_members);
	ClassDecl *parse_class(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata);
	void parse_interface_member(uint32_t p_start, std::vector<FunctionDecl *> &r_methods);
	InterfaceDecl *parse_interface(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata);
	PackageDecl *parse_package();
};

} // namespace kas3
