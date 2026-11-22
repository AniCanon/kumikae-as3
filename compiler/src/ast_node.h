// The syntax tree's common part: what every node has, and the nodes that expressions, statements and
// declarations all refer to. ast.h includes the whole tree.
#pragma once

#include "arena.h"
#include "token.h"

#include <cstdint>
#include <string_view>

namespace kas3 {

struct Type; // The static type of an expression. Filled in by the types pass (names.h); null before it.
struct Symbol; // What a name refers to. Filled in by name resolution (names.h); null before it.

template <class T>
struct List {
	T *items = nullptr;
	uint32_t count = 0;

	T *begin() const { return items; }
	T *end() const { return items + count; }
	T &operator[](uint32_t p_index) const { return items[p_index]; }
};

#define KAS3_NODES(X) \
	X(TypeRef) \
	X(NumberLit) X(StringLit) X(RegexLit) X(BoolLit) X(NullLit) X(ThisExpr) X(SuperExpr) X(Name) \
	X(ArrayLit) X(ObjectLit) X(VectorLit) X(FunctionExpr) X(Member) X(Index) X(TypeApply) X(Call) X(New) \
	X(Unary) X(Postfix) X(Binary) X(Assign) X(Conditional) \
	X(Block) X(ExprStmt) X(Empty) X(VarStmt) X(If) X(While) X(DoWhile) X(For) X(ForIn) X(Switch) X(Break) \
	X(Continue) X(Return) X(Throw) X(Try) X(Labeled) X(FunctionStmt) X(ImportStmt) \
	X(VarDecl) X(FunctionDecl) X(ClassDecl) X(InterfaceDecl) X(Import) X(PackageDecl) X(Program)

enum class NodeKind : uint8_t {
#define X(name) name,
	KAS3_NODES(X)
#undef X
};

// [offset, end) is the node's extent in the source text.
struct Node {
	NodeKind kind;
	uint32_t offset = 0;
	uint32_t end = 0;
};

struct Expr : Node {
	bool parenthesized = false; // Written inside parentheses; the range includes them.
	const Type *type = nullptr;
};
struct Stmt : Node {};

struct TypeRef : Node {
	static const NodeKind KIND = NodeKind::TypeRef;
	enum Form : uint8_t { Any, Void, Named, Applied, FixedArray, Callback }; // `*`, `void`, `a.b.C`, `Vector.<T>`
	Form form = Any;
	uint32_t completion_begin = 0; // After the token that introduced this type, including a missing type.
	List<std::string_view> name; // Dotted parts, for Named and Applied.
	List<uint32_t> name_offsets; // Where each part starts.
	List<TypeRef *> parameters; // For a callback; argument holds its result.
	TypeRef *argument = nullptr; // For Applied.
	Symbol *symbol = nullptr; // The class or interface, for Named and Applied.
};

// Declared early: expressions and statements refer to them.

struct MetadataArg {
	std::string_view key; // Empty if positional.
	std::string_view value;
	bool value_is_string = false; // Written in quotes.
};
struct Metadata {
	std::string_view name;
	List<MetadataArg> args;
	uint32_t offset = 0;
	uint32_t end = 0;
};

struct Modifiers {
	enum : uint16_t {
		Public = 1,
		Private = 2,
		Protected = 4,
		Internal = 8,
		Static = 16,
		Final = 32,
		Override = 64,
		Dynamic = 128,
		Native = 256
	};
	uint16_t bits = 0;
};

struct Declarator {
	std::string_view name;
	TypeRef *type = nullptr;
	Expr *initializer = nullptr;
	uint32_t offset = 0; // Of the name.
	uint32_t end = 0;
	Symbol *symbol = nullptr;
};
struct VarDecl : Node {
	static const NodeKind KIND = NodeKind::VarDecl;
	bool is_const = false;
	Modifiers modifiers;
	List<Metadata> metadata;
	List<Declarator> declarators;
};

struct Parameter {
	std::string_view name;
	TypeRef *type = nullptr;
	Expr *default_value = nullptr;
	bool is_rest = false;
	uint32_t offset = 0;
	uint32_t end = 0;
	Symbol *symbol = nullptr;
};
struct Block;
struct FunctionDecl : Node {
	static const NodeKind KIND = NodeKind::FunctionDecl;
	enum Accessor : uint8_t { None, Getter, Setter };
	Accessor accessor = None;
	bool has_parameter_list = false; // Distinguishes an unfinished completion name from a written signature.
	std::string_view name; // Empty for an anonymous function expression.
	uint32_t name_offset = 0;
	Modifiers modifiers;
	List<Metadata> metadata;
	List<Parameter> parameters;
	TypeRef *return_type = nullptr; // Null when not written.
	Block *body = nullptr; // Null for interface methods and `native` functions.
	Symbol *symbol = nullptr; // Null for an anonymous function expression.
};

template <class T>
T *new_node(Arena &p_arena, uint32_t p_offset, uint32_t p_end) {
	T *node = p_arena.make<T>();
	node->kind = T::KIND;
	node->offset = p_offset;
	node->end = p_end;
	return node;
}

} // namespace kas3
