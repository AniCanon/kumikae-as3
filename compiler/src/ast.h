// The syntax tree. Every node is trivially destructible and lives in an Arena. Text fields are views into
// the source text or into the arena.
#pragma once

#include "arena.h"
#include "token.h"

#include <cstdint>
#include <string_view>

namespace kas3 {

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

struct Expr : Node {};
struct Stmt : Node {};

// ---- types ----

struct TypeRef : Node {
	static const NodeKind KIND = NodeKind::TypeRef;
	enum Form : uint8_t { Any, Void, Named, Applied }; // `*`, `void`, `a.b.C`, `Vector.<T>`
	Form form = Any;
	List<std::string_view> name; // Dotted parts, for Named and Applied.
	TypeRef *argument = nullptr; // For Applied.
};

// ---- declarations (declared early: expressions and statements refer to them) ----

struct MetadataArg {
	std::string_view key; // Empty if positional.
	std::string_view value;
};
struct Metadata {
	std::string_view name;
	List<MetadataArg> args;
	uint32_t offset = 0;
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
	uint32_t offset = 0;
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
};
struct Block;
struct FunctionDecl : Node {
	static const NodeKind KIND = NodeKind::FunctionDecl;
	enum Accessor : uint8_t { None, Getter, Setter };
	Accessor accessor = None;
	std::string_view name; // Empty for an anonymous function expression.
	Modifiers modifiers;
	List<Metadata> metadata;
	List<Parameter> parameters;
	TypeRef *return_type = nullptr; // Null when not written.
	Block *body = nullptr; // Null for interface methods and `native` functions.
};

// ---- expressions ----

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

// ---- statements ----

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
};
struct Switch : Stmt {
	static const NodeKind KIND = NodeKind::Switch;
	Expr *subject = nullptr;
	List<SwitchCase> cases;
};
struct Break : Stmt {
	static const NodeKind KIND = NodeKind::Break;
	std::string_view label; // Empty when absent.
};
struct Continue : Stmt {
	static const NodeKind KIND = NodeKind::Continue;
	std::string_view label;
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
	TypeRef *type = nullptr;
	Block *body = nullptr;
	uint32_t offset = 0;
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

// ---- larger declarations ----

struct ClassDecl : Node {
	static const NodeKind KIND = NodeKind::ClassDecl;
	std::string_view name;
	Modifiers modifiers;
	List<Metadata> metadata;
	TypeRef *extends = nullptr;
	List<TypeRef *> implements;
	List<Node *> members; // VarDecl, FunctionDecl or Import.
};
struct InterfaceDecl : Node {
	static const NodeKind KIND = NodeKind::InterfaceDecl;
	std::string_view name;
	Modifiers modifiers;
	List<Metadata> metadata;
	List<TypeRef *> extends;
	List<FunctionDecl *> methods;
};
struct Import : Node { // `import a.b.C;` or `import a.b.*;`
	static const NodeKind KIND = NodeKind::Import;
	List<std::string_view> name;
	bool wildcard = false;
};
struct PackageDecl : Node {
	static const NodeKind KIND = NodeKind::PackageDecl;
	List<std::string_view> name;
	List<Import *> imports;
	List<Node *> definitions; // ClassDecl, InterfaceDecl, FunctionDecl or VarDecl.
};
struct Program : Node {
	static const NodeKind KIND = NodeKind::Program;
	PackageDecl *package = nullptr; // May be null.
	List<Import *> imports; // Outside the package block.
	List<Node *> definitions; // The file's helpers, declared outside the package block.
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
