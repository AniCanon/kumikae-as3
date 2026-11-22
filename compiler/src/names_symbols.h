// What a name refers to: symbols, the static types made of them, classes, packages and imports. names.h
// holds the files and the Compilation that make them.
#pragma once

#include "ast.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kas3 {

struct ClassInfo;
struct Package;
struct Unit;

enum class SymbolKind : uint8_t {
	Class,
	Interface,
	Function, // At package level.
	Variable, // At package level.
	Field,
	Method,
	Property, // A getter, a setter, or both.
	Parameter,
	Local,
	LocalFunction,
	CatchVariable,
	Arguments, // The implicit `arguments` of a function.
};

const char *symbol_kind_name(SymbolKind p_kind);

struct Symbol {
	SymbolKind kind = SymbolKind::Local;
	std::string name; // Stable across replacement of a source snapshot.
	Unit *unit = nullptr; // Null only for `arguments`.
	uint32_t offset = 0; // Of the name where it is declared.
	Node *node = nullptr; // ClassDecl, InterfaceDecl, FunctionDecl or VarDecl; null for the rest.
	TypeRef *type = nullptr; // The declared type; for a function or method, the return type. May be null.
	uint16_t modifiers = 0; // Modifiers bits.
	bool is_const = false;
	bool is_rest = false; // A `...rest` parameter.
	bool file_private = false; // Declared outside the file's package block.
	Package *package = nullptr; // For package-level definitions and file helpers.
	Symbol *owner = nullptr; // The class or interface, for members.
	ClassInfo *klass = nullptr; // For Class and Interface.
	FunctionDecl *getter = nullptr; // For Property.
	FunctionDecl *setter = nullptr;

	bool is_static() const { return (modifiers & Modifiers::Static) != 0; }
	bool is_type() const { return kind == SymbolKind::Class || kind == SymbolKind::Interface; }
	bool is_instance_member() const {
		return (kind == SymbolKind::Field || kind == SymbolKind::Method || kind == SymbolKind::Property) &&
				!is_static();
	}
};

// A static type. Interned: two equal types are the same object.
struct Type {
	enum Kind : uint8_t {
		Any, // `*`
		Void,
		Null, // The type of `null`.
		Unknown, // Could not be worked out; an error has been reported, or the rule is not built yet.
		Instance, // An object of a class or interface.
		Vector, // Vector.<element>
		FixedArray, // []element
		Callback, // A Function with a fixed signature.
		ClassObject, // The class itself, as in `Math` or `Player`: `element` is the type of its instances.
	};
	Kind kind = Any;
	bool typed_sequence() const { return kind == Vector || kind == FixedArray; }
	bool contains_fixed_array() const {
		const Type *type = this;
		while (type && type->kind == Vector) {
			type = type->element;
		}
		return type && type->kind == FixedArray;
	}
	Symbol *symbol = nullptr; // For Instance.
	std::vector<const Type *> parameters; // For Callback; element is its return type.
	const Type *element = nullptr; // For Vector and ClassObject.
};

// An expression whose type `--dump-types` prints: a statement's, or an initializer's.
struct TypedExpression {
	uint32_t offset;
	const Type *type;
};

// One import, resolved: a single definition, or a whole package.
struct ResolvedImport {
	Symbol *symbol = nullptr;
	Package *package = nullptr;
};

// The imports in force at some place in a file. A class body and a function body may add their own.
struct ImportScope {
	ImportScope *parent = nullptr;
	std::vector<ResolvedImport> imports;
	// Imports that named nothing. A name one of them could have supplied is not reported again.
	std::vector<std::string_view> failed;
	bool failed_wildcard = false;
};

struct ClassInfo {
	Symbol *symbol = nullptr;
	Symbol *base = nullptr; // Null for Object, for interfaces, and after an error.
	std::vector<Symbol *> interfaces; // What a class implements, or what an interface extends.
	// What the class's objects have. The constructor is not among them.
	std::unordered_map<std::string_view, Symbol *> members;
	// What the class itself has. A static member may share its name with an instance member.
	std::unordered_map<std::string_view, Symbol *> statics;
	std::unordered_map<std::string_view, Symbol *> &table(bool p_static) { return p_static ? statics : members; }
	Symbol *constructor = nullptr;
	ImportScope imports; // Imports written in the class body.
	bool broken = false; // Its `extends` did not resolve, or it was part of a cycle.
	bool incomplete = false; // A built-in class whose members are not declared yet: marked [Incomplete].
	bool inherited = false; // Property halves have been filled in from the bases.
	uint32_t mark = 0; // For cycle detection.
	// Whether this class or a base is broken or incomplete, once the hierarchy is settled.
	bool chain_known = false;
	bool chain_broken = false;
};

struct Package {
	std::string name; // Empty for the top-level package.
	std::unordered_map<std::string_view, Symbol *> definitions;
	// Further definitions of a name in other files: the first file's is in definitions.
	std::unordered_multimap<std::string_view, Symbol *> repeated;
	size_t files = 0; // How many files' package blocks name it: with none, an edited program forgets it.
};

// A resolved use of a name, kept for `--dump-names` and for tools.
struct Reference {
	uint32_t offset;
	std::string_view name;
	Symbol *symbol;
};

} // namespace kas3
