// The resolver of the bodies pass, shared by the files that walk what it resolves:
//   names_bodies.cpp       definitions, functions and their scopes
//   names_locals.cpp       what a function body declares, wherever in it it is written
//   names_statements.cpp   statements: labels, loops and catch scopes
//   names_expressions.cpp  names, members and the rest of expressions
#pragma once

#include "names.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kas3 {

using Scope = LocalScope;

struct Label {
	std::string_view name;
	bool loop;
};

class Resolver {
	Compilation &compilation_;
	Unit &unit_;
	Context context_;
	bool static_ = false; // Inside a static method or a static field's initializer.
	Scope *scope_ = nullptr; // Innermost.
	Scope *function_scope_ = nullptr; // Of the innermost function: where `var` declares.
	std::vector<Label> labels_;
	int loops_ = 0;
	int switches_ = 0;
	Symbol *arguments_ = nullptr;

	void error(uint32_t p_offset, std::string p_message) { unit_.diagnostics.error(p_offset, std::move(p_message)); }
	void unresolved(uint32_t p_offset, std::string p_message) { unit_.diagnostics.unresolved(p_offset, std::move(p_message)); }
	void record(uint32_t p_offset, std::string_view p_name, Symbol *p_symbol) {
		unit_.references.push_back({ p_offset, p_name, p_symbol });
	}
	std::string line_of(const Symbol *p_symbol) const {
		return std::to_string(unit_.source.position(p_symbol->offset).line);
	}

	Symbol *lookup(std::string_view p_name, uint32_t p_offset, bool p_report);

	void type(TypeRef *p_type) { compilation_.resolve_type(context_, p_type); }

	void declare_locals(VarDecl *p_decl);
	void declare_local_function(FunctionDecl *p_function);
	void hoist_switch(Switch *p_switch, std::vector<Import *> *r_imports);
	void hoist_try(Try *p_try, std::vector<Import *> *r_imports);
	void hoist(Stmt *p_statement, std::vector<Import *> *r_imports);
	Scope &retain_scope(Scope &p_fallback, const Node *p_extent);
	// p_declared: the function is a declaration (a method, an interface's method, a package-level function),
	// whose signature and body imports the signature pass resolved.
	void body_imports(FunctionDecl *p_function, ImportScope &r_imports, bool p_declared);
	void signature(FunctionDecl *p_function);
	void signatures(Node *p_node, bool p_in_block);
	void declare_parameters(FunctionDecl *p_function, Scope &r_scope, bool p_declared);
	void function(FunctionDecl *p_function, bool p_expression, bool p_declared = false);
	const Label *find_label(std::string_view p_name) const;
	void loop_body(Stmt *p_body);
	void try_statement(Try *p_try);
	void loop_statement(Stmt *p_statement);
	void switch_statement(Switch *p_switch);
	void break_statement(Break *p_break);
	void continue_statement(Continue *p_continue);
	void labeled_statement(Labeled *p_labeled);
	void statement(Stmt *p_statement);
	void assignable(Expr *p_target);
	void name(Name *p_name);
	void member(Member *p_member);
	void expressions(const List<Expr *> &p_list);
	void literal(Expr *p_expression);
	void operation(Expr *p_expression);
	void expression(Expr *p_expression);
	void variables(VarDecl *p_decl);
	void definition(Node *p_node, bool p_in_block);

public:
	Resolver(Compilation &p_compilation, Unit &p_unit) :
			compilation_(p_compilation), unit_(p_unit) {}

	// The types written outside function bodies, with the imports of the bodies that are in force for them.
	// Every file's run before any body is typed: typing reads other files' declarations.
	void run_signatures();
	// Everything else: the bodies, field initializers and default values.
	void run();
};

} // namespace kas3
