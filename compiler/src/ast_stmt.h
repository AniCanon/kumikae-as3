// The syntax tree's statements.
#pragma once

#include "ast_node.h"

#include <cstdint>
#include <string_view>

namespace kas3 {

struct Block : Stmt {
	static const NodeKind KIND = NodeKind::Block;
	List<Stmt *> statements;
};
struct ExprStmt : Stmt {
	static const NodeKind KIND = NodeKind::ExprStmt;
	Expr *expr = nullptr;
};
struct Empty : Stmt {
	static const NodeKind KIND = NodeKind::Empty;
};
struct VarStmt : Stmt { // One statement per `var` or `const` keyword.
	static const NodeKind KIND = NodeKind::VarStmt;
	VarDecl *decl = nullptr;
};
struct If : Stmt {
	static const NodeKind KIND = NodeKind::If;
	Expr *condition = nullptr;
	Stmt *then_branch = nullptr;
	Stmt *else_branch = nullptr;
};
struct While : Stmt {
	static const NodeKind KIND = NodeKind::While;
	Expr *condition = nullptr;
	Stmt *body = nullptr;
};
struct DoWhile : Stmt {
	static const NodeKind KIND = NodeKind::DoWhile;
	Stmt *body = nullptr;
	Expr *condition = nullptr;
};
struct For : Stmt {
	static const NodeKind KIND = NodeKind::For;
	Stmt *init = nullptr; // VarStmt, ExprStmt or null.
	Expr *condition = nullptr;
	Expr *update = nullptr;
	Stmt *body = nullptr;
};
struct ForIn : Stmt { // Exactly one of declaration and target is set.
	static const NodeKind KIND = NodeKind::ForIn;
	bool each = false;
	// Set by the checker for `for each` over one of Godot's collections that says what it holds (a packed
	// array): the type of each element.
	const Type *element = nullptr;
	VarDecl *declaration = nullptr;
	Expr *target = nullptr;
	Expr *collection = nullptr;
	Stmt *body = nullptr;
};
struct SwitchCase {
	Expr *test = nullptr; // Null for `default`.
	bool is_default = false;
	List<Stmt *> body;
	uint32_t offset = 0;
	uint32_t end = 0;
};
struct Switch : Stmt {
	static const NodeKind KIND = NodeKind::Switch;
	Expr *subject = nullptr;
	List<SwitchCase> cases;
};
struct Break : Stmt {
	static const NodeKind KIND = NodeKind::Break;
	std::string_view label; // Empty when absent.
	uint32_t label_offset = 0;
};
struct Continue : Stmt {
	static const NodeKind KIND = NodeKind::Continue;
	std::string_view label;
	uint32_t label_offset = 0;
};
struct Return : Stmt {
	static const NodeKind KIND = NodeKind::Return;
	Expr *value = nullptr;
};
struct Throw : Stmt {
	static const NodeKind KIND = NodeKind::Throw;
	Expr *value = nullptr;
};
struct CatchClause {
	std::string_view name;
	uint32_t name_offset = 0;
	TypeRef *type = nullptr;
	Block *body = nullptr;
	uint32_t offset = 0;
	uint32_t end = 0;
	Symbol *symbol = nullptr;
};
struct Try : Stmt {
	static const NodeKind KIND = NodeKind::Try;
	Block *body = nullptr;
	List<CatchClause> catches;
	Block *finally_block = nullptr;
};
struct Labeled : Stmt {
	static const NodeKind KIND = NodeKind::Labeled;
	std::string_view label;
	Stmt *body = nullptr;
};
struct FunctionStmt : Stmt { // A named function nested in a function.
	static const NodeKind KIND = NodeKind::FunctionStmt;
	FunctionDecl *function = nullptr;
};
struct Import;
struct ImportStmt : Stmt { // An import inside a function body.
	static const NodeKind KIND = NodeKind::ImportStmt;
	Import *import = nullptr;
};

} // namespace kas3
