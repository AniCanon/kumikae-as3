// The syntax tree's expressions.
#pragma once

#include "ast_node.h"

#include <cstdint>
#include <string_view>

namespace kas3 {

struct NumberLit : Expr {
	static const NodeKind KIND = NodeKind::NumberLit;
	std::string_view text;
};
struct StringLit : Expr {
	static const NodeKind KIND = NodeKind::StringLit;
	std::string_view text; // As written, quotes included.
};
struct RegexLit : Expr {
	static const NodeKind KIND = NodeKind::RegexLit;
	std::string_view text;
};
struct BoolLit : Expr {
	static const NodeKind KIND = NodeKind::BoolLit;
	bool value = false;
};
struct NullLit : Expr {
	static const NodeKind KIND = NodeKind::NullLit;
};
struct ThisExpr : Expr {
	static const NodeKind KIND = NodeKind::ThisExpr;
};
struct SuperExpr : Expr {
	static const NodeKind KIND = NodeKind::SuperExpr;
};
struct Name : Expr {
	static const NodeKind KIND = NodeKind::Name;
	std::string_view text;
	Symbol *symbol = nullptr;
};
struct ArrayLit : Expr {
	static const NodeKind KIND = NodeKind::ArrayLit;
	List<Expr *> elements; // A hole in `[1,,2]` is a null element.
};
struct ObjectField {
	std::string_view key;
	bool key_is_string = false;
	Expr *value = nullptr;
	uint32_t offset = 0;
	uint32_t end = 0;
};
struct ObjectLit : Expr {
	static const NodeKind KIND = NodeKind::ObjectLit;
	List<ObjectField> fields;
};
struct VectorLit : Expr { // `new <int>[1, 2]`
	static const NodeKind KIND = NodeKind::VectorLit;
	TypeRef *element_type = nullptr;
	List<Expr *> elements;
};
struct FunctionExpr : Expr {
	static const NodeKind KIND = NodeKind::FunctionExpr;
	FunctionDecl *function = nullptr;
};
struct Member : Expr { // `a.b`
	static const NodeKind KIND = NodeKind::Member;
	Expr *object = nullptr;
	std::string_view name;
	uint32_t name_offset = 0;
	// Set when the chain up to here names a definition by its package (`flash.utils.getTimer`). The object
	// below is then a package path, not a value.
	Symbol *symbol = nullptr;
	Symbol *member = nullptr; // The member of the object's type that this names, when it is known.
	// What `a.b` names, either way.
	Symbol *resolved() const { return symbol ? symbol : member; }
};
struct Index : Expr { // `a[b]`
	static const NodeKind KIND = NodeKind::Index;
	Expr *object = nullptr;
	Expr *index = nullptr;
};
struct TypeApply : Expr { // `Vector.<int>` used as a value
	static const NodeKind KIND = NodeKind::TypeApply;
	Expr *base = nullptr;
	TypeRef *argument = nullptr;
};
struct Call : Expr {
	static const NodeKind KIND = NodeKind::Call;
	Expr *callee = nullptr;
	List<Expr *> arguments;
};
struct New : Expr {
	static const NodeKind KIND = NodeKind::New;
	Expr *callee = nullptr;
	List<Expr *> arguments;
	bool has_arguments = false;
};
struct Unary : Expr { // Prefix: + - ! ~ ++ -- typeof void delete
	static const NodeKind KIND = NodeKind::Unary;
	TokenKind op = TokenKind::Error;
	Expr *operand = nullptr;
};
struct Postfix : Expr { // ++ --
	static const NodeKind KIND = NodeKind::Postfix;
	TokenKind op = TokenKind::Error;
	Expr *operand = nullptr;
};
struct Binary : Expr { // Includes is, as, in, instanceof, &&, || and the comma operator.
	static const NodeKind KIND = NodeKind::Binary;
	TokenKind op = TokenKind::Error;
	Expr *left = nullptr;
	Expr *right = nullptr;
};
struct Assign : Expr {
	static const NodeKind KIND = NodeKind::Assign;
	TokenKind op = TokenKind::Error;
	Expr *target = nullptr;
	Expr *value = nullptr;
};
struct Conditional : Expr {
	static const NodeKind KIND = NodeKind::Conditional;
	Expr *condition = nullptr;
	Expr *then_value = nullptr;
	Expr *else_value = nullptr;
};

} // namespace kas3
