// Name resolution: a set of source files read as one program, and what every name in it refers to.
//
// Three passes run over the syntax trees:
//   declare  packages, their definitions, and each class's members            (names.cpp)
//   link     imports, the class hierarchy, and the override rules             (names_link.cpp)
//   bodies   every type name and every name inside a function                 (names_bodies.cpp)
// The results are written onto the trees as `symbol` fields.
#pragma once

#include "arena.h"
#include "ast.h"
#include "diagnostics.h"
#include "source.h"

#include <cstdint>
#include <deque>
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
	std::string_view name;
	Unit *unit = nullptr; // Null only for `arguments`.
	uint32_t offset = 0; // Of the name where it is declared.
	Node *node = nullptr; // ClassDecl, InterfaceDecl, FunctionDecl or VarDecl; null for the rest.
	TypeRef *type = nullptr; // The declared type; for a function or method, the return type. May be null.
	uint16_t modifiers = 0; // Modifiers bits.
	bool is_const = false;
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
	std::unordered_map<std::string_view, Symbol *> members; // The constructor is not among them.
	Symbol *constructor = nullptr;
	ImportScope imports; // Imports written in the class body.
	bool broken = false; // Its `extends` did not resolve, or it was part of a cycle.
	bool incomplete = false; // A built-in class whose members are not declared yet: marked [Incomplete].
	bool inherited = false; // Property halves have been filled in from the bases.
	uint32_t mark = 0; // For cycle detection.
};

struct Package {
	std::string name; // Empty for the top-level package.
	std::unordered_map<std::string_view, Symbol *> definitions;
};

// A resolved use of a name, kept for `--dump-names` and for tools.
struct Reference {
	uint32_t offset;
	std::string_view name;
	Symbol *symbol;
};

// One source file.
struct Unit {
	Source source;
	Arena arena;
	Diagnostics diagnostics;
	Program *tree = nullptr;
	bool is_declaration = false; // Declares what exists elsewhere: `native`, no bodies.
	bool is_builtin = false; // Part of the embedded library.
	Package *package = nullptr; // The package its block names; the top-level package if it has no block.
	std::unordered_map<std::string_view, Symbol *> helpers; // Definitions outside the package block.
	ImportScope block_imports; // Imports inside the package block.
	ImportScope outer_imports; // Imports outside it.
	std::vector<Reference> references;

	Unit(std::string p_path, std::string p_text) :
			source(std::move(p_path), std::move(p_text)) {}
};

// Where a name is looked up from.
struct Context {
	Unit *unit = nullptr;
	Package *package = nullptr;
	ImportScope *imports = nullptr;
	Symbol *klass = nullptr; // The enclosing class or interface, if any.
};

// The outcome of a package-level lookup.
struct Found {
	Symbol *first = nullptr;
	Symbol *second = nullptr; // Set when the name is ambiguous.
	Symbol *hidden = nullptr; // A match that exists but is not visible from here.
	bool unimported = false; // `hidden` was named by its package, without the import AS3 requires.
	bool suppressed = false; // No match, but a failed import could have supplied one: stay quiet.
};

class Compilation {
	std::deque<Unit> units_;
	std::deque<Symbol> symbols_;
	std::deque<ClassInfo> classes_;
	std::unordered_map<std::string, Package> packages_;
	Package *top_level_ = nullptr;
	Symbol *object_ = nullptr;
	Symbol *vector_ = nullptr;
	Symbol *element_ = nullptr; // `T`, the element type inside the built-in Vector.
	bool checked_ = false;

	// declare
	void declare_unit(Unit &p_unit);
	void declare_definition(Unit &p_unit, Node *p_node, bool p_in_block);
	void declare_package_level(Unit &p_unit, Symbol *p_symbol, bool p_in_block);
	void declare_class_members(Unit &p_unit, Symbol *p_class, ClassDecl *p_decl);
	void declare_interface_members(Unit &p_unit, Symbol *p_interface, InterfaceDecl *p_decl);
	void declare_member(Unit &p_unit, Symbol *p_owner, Symbol *p_member);
	void declare_accessor(Unit &p_unit, Symbol *p_owner, FunctionDecl *p_function);
	void check_native(Unit &p_unit, uint16_t p_modifiers, uint32_t p_offset);

	// link
	void link_imports(Unit &p_unit);
	void link_hierarchy();
	void link_class(Symbol *p_class);
	void link_interface(Symbol *p_interface);
	void cut_class_cycles();
	void cut_interface_cycles();
	void check_overrides(Symbol *p_class);
	void inherit_properties(Symbol *p_class);
	bool imported(const Context &p_from, const Symbol *p_definition) const;

public:
	Compilation(); // Loads the built-in declarations.

	// Parses the file. The returned unit lives as long as the compilation.
	Unit *add(std::string p_path, std::string p_text, bool p_is_declaration = false);
	// Resolves names in every file added so far. Does nothing if any file has a syntax error, so that one
	// mistake is not reported twice. Call once.
	void check();
	bool has_errors() const;
	// The diagnostics of every file, in the order the files were added.
	std::string format_diagnostics() const;
	// One line per resolved reference in each file that is not a declaration file.
	std::string dump_names() const;

	// ---- used by the passes ----

	Symbol *new_symbol(SymbolKind p_kind, std::string_view p_name, Unit *p_unit, uint32_t p_offset);
	Package *package(const std::string &p_name); // Created if missing.
	Package *find_package(const std::string &p_name);
	Package *top_level() const { return top_level_; }
	Symbol *vector_class() const { return vector_; }
	Symbol *object_class() const { return object_; }
	Symbol *element_parameter() const { return element_; }
	std::deque<Unit> &units() { return units_; }

	// Resolves the imports into r_scope, reporting those that name nothing.
	void resolve_imports(Unit &p_unit, const List<Import *> &p_imports, ImportScope &r_scope);
	Context context_of(Unit &p_unit, bool p_in_block);
	Context context_of(Symbol *p_class);
	bool visible(const Symbol *p_definition, const Context &p_from) const;
	Found lookup_package_level(const Context &p_from, std::string_view p_name) const;
	// A definition named by its package: `a.b` and `C`.
	Found lookup_qualified(const Context &p_from, const std::string &p_package, std::string_view p_name) const;
	// Resolves a written type, recording the reference and reporting what is wrong. Returns the class or
	// interface, or null for `*`, `void`, a null p_type, or an error.
	Symbol *resolve_type(const Context &p_from, TypeRef *p_type);
	// A member of the class or of its bases that code inside p_class may name. With p_static_only, instance
	// members are passed over, as they are for code in a static method; *r_instance receives the first one
	// passed over. *r_hidden receives a match that exists but is not visible from the class.
	Symbol *find_member(Symbol *p_class, std::string_view p_name, bool p_static_only, Symbol **r_instance,
			Symbol **r_hidden) const;
	// True if the members that the class inherits are not all known: a base failed to resolve, or is a
	// built-in class whose members are not declared yet.
	bool hierarchy_broken(Symbol *p_class) const;

	// "a.b.C", "C", or "C in this file" for a helper; "Owner.name" for a member.
	std::string qualified_name(const Symbol *p_symbol) const;
	// "line 3", "Other.as:3:5", or "the built-in library".
	std::string where(const Unit &p_from, const Symbol *p_symbol) const;
	std::string ambiguous_message(std::string_view p_name, const Found &p_found) const;
};

} // namespace kas3
