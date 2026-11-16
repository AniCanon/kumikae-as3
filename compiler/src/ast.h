// The syntax tree. Every node is trivially destructible and lives in an Arena. Text fields are views into
// the source text or into the arena.
//
// ast_node.h holds what every node has, ast_expr.h the expressions, ast_stmt.h the statements, and this file
// the declarations.
#pragma once

#include "ast_expr.h"
#include "ast_node.h"
#include "ast_stmt.h"

#include <cstdint>
#include <string_view>

namespace kas3 {

struct ClassDecl : Node {
	static const NodeKind KIND = NodeKind::ClassDecl;
	std::string_view name;
	uint32_t name_offset = 0;
	Modifiers modifiers;
	List<Metadata> metadata;
	TypeRef *extends = nullptr;
	List<TypeRef *> implements;
	List<Node *> members; // VarDecl, FunctionDecl or Import.
	Symbol *symbol = nullptr;
};
struct InterfaceDecl : Node {
	static const NodeKind KIND = NodeKind::InterfaceDecl;
	std::string_view name;
	uint32_t name_offset = 0;
	Modifiers modifiers;
	List<Metadata> metadata;
	List<TypeRef *> extends;
	List<FunctionDecl *> methods;
	Symbol *symbol = nullptr;
};
struct Import : Node { // `import a.b.C;` or `import a.b.*;`
	static const NodeKind KIND = NodeKind::Import;
	List<std::string_view> name;
	List<uint32_t> name_offsets;
	bool wildcard = false;
	Symbol *symbol = nullptr; // The definition it names, when there is one and it may be imported.
};
struct PackageDecl : Node {
	static const NodeKind KIND = NodeKind::PackageDecl;
	uint32_t body_offset = 0; // Immediately after the opening brace.
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

} // namespace kas3
