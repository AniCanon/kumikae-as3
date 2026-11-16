// Name resolution: a set of source files read as one program, and what every name in it refers to.
//
// Three passes run over the syntax trees:
//   declare  packages, their definitions, and each class's members            (names.cpp)
//   link     imports, the class hierarchy, and the override rules             (names_link.cpp, names_lookup.cpp,
//                                                                              names_members.cpp)
//   bodies   every type name and every name inside a function                 (names_resolver.h lists its files)
// The results are written onto the trees as `symbol` fields.
#pragma once

#include "arena.h"
#include "ast.h"
#include "diagnostics.h"
#include "names_symbols.h"
#include "source.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kas3 {

// A source file and its syntax tree. Parsing is most of the cost of a check, so a tool that checks again
// and again keeps these and hands them to each new Compilation. The passes write onto the tree: only one
// Compilation may use a Parsed at a time, and the one before it must not be asked anything afterwards.
struct Parsed {
	Source source;
	Arena arena;
	Diagnostics diagnostics; // Syntax errors.
	std::vector<TextRegion> text_regions; // Sorted, non-overlapping, shared with editor queries.
	Program *tree = nullptr;
	bool used = false; // A Compilation has written onto the tree.

	Parsed(std::string p_path, std::string p_text) :
			source(std::move(p_path), std::move(p_text)) {}
};

std::shared_ptr<Parsed> parse_file(std::string p_path, std::string p_text);
// The embedded library, parsed.
std::vector<std::shared_ptr<Parsed>> parse_builtins();

// The resolver's scopes, retained for editor queries only where a body exists. Stable addresses in Unit.
struct LocalScope {
	LocalScope *parent = nullptr;
	std::unordered_map<std::string_view, Symbol *> names;
	ImportScope imports;
	const Node *extent = nullptr;
};

// One source file in a Compilation.
struct Unit {
	std::shared_ptr<Parsed> parsed;
	Source source;
	Diagnostics diagnostics; // The syntax errors, then what the passes report.
	Program *tree = nullptr;
	bool is_declaration = false; // Declares what exists elsewhere: `native`, no bodies.
	bool is_builtin = false; // Part of the embedded library.
	bool broken = false; // It has a syntax error: what it declares may be read wrongly, or not at all.
	bool failed_import = false; // One of its imports named nothing.
	Package *package = nullptr; // The package its block names; the top-level package if it has no block.
	std::unordered_map<std::string_view, Symbol *> helpers; // Definitions outside the package block.
	ImportScope block_imports; // Imports inside the package block.
	ImportScope outer_imports; // Imports outside it.
	std::vector<Reference> references;
	std::vector<TypedExpression> typed;
	std::deque<LocalScope> local_scopes;
	std::deque<Symbol> body_symbols; // Replaced together with this unit's body semantics.

	explicit Unit(std::shared_ptr<Parsed> p_parsed) :
			parsed(std::move(p_parsed)), source(parsed->source), diagnostics(parsed->diagnostics), tree(parsed->tree) {}
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
	bool reusable_bodies_ = false;
	size_t initial_type_count_ = 0;
	std::deque<Type> types_;
	std::unordered_map<const Symbol *, const Type *> instance_types_;
	std::unordered_map<const Type *, const Type *> vector_types_;
	std::unordered_map<const Type *, const Type *> class_object_types_;
	Type any_, void_, null_, unknown_;

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
	void drop_doubtful_errors();
	void inherit_properties(Symbol *p_class);

public:
	Compilation(); // Loads the built-in declarations.
	// The same, with the built-in declarations already parsed (parse_builtins()).
	explicit Compilation(const std::vector<std::shared_ptr<Parsed>> &p_builtins);

	// Parses the file. The returned unit lives as long as the compilation.
	Unit *add(std::string p_path, std::string p_text, bool p_is_declaration = false);
	// A file parsed earlier. What an earlier Compilation wrote onto its tree is removed.
	Unit *add(std::shared_ptr<Parsed> p_parsed, bool p_is_declaration = false);
	// Resolves names in every file added so far. Does nothing if any file has a syntax error, so that one
	// mistake is not reported twice. Call once.
	// With p_through_syntax_errors, names are resolved all the same, for tools that answer questions about
	// code that is being typed, and what a syntax error can't have caused is still reported: nothing more
	// in a file that has one, nothing in a file that uses what such a file declares, and nowhere that a
	// name is missing (names_doubt.cpp).
	void check(bool p_through_syntax_errors = false);
	// Reuses declaration identities only when the source outside function bodies is unchanged.
	// Drop queries before calling: a successful update invalidates changed units' body symbols,
	// scopes and syntax pointers. False leaves the compilation unchanged and requests a full check.
	bool update_bodies(const std::vector<std::pair<Unit *, std::shared_ptr<Parsed>>> &p_changes);
	bool has_errors() const;
	// The diagnostics of every file, in the order the files were added.
	std::string format_diagnostics(bool p_warnings = true) const; // Without the warnings when false.
	// One line per resolved reference in each file that is not a declaration file.
	std::string dump_names() const;
	// One line per expression statement and initializer in each file that is not a declaration file.
	std::string dump_types() const;
	// "int", "Vector.<String>", "Class.<Math>", "*", "void", "null", or "?" for unknown.
	std::string type_name(const Type *p_type) const;

	const Type *any_type() const { return &any_; }
	const Type *void_type() const { return &void_; }
	const Type *null_type() const { return &null_; }
	const Type *unknown_type() const { return &unknown_; }
	const Type *instance_type(Symbol *p_class); // Vector.<*> for the Vector class itself.
	const Type *vector_type(const Type *p_element);
	const Type *class_object_type(const Type *p_instance);
	// The type a written type means. p_element is what `T` stands for inside Vector's declaration.
	const Type *type_of(const TypeRef *p_type, const Type *p_element = nullptr);
	// A top-level built-in class by name, or null.
	Symbol *builtin(std::string_view p_name) const;

	// For the passes.

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
	// A member of the class or of its bases that code inside p_class may name: an instance member first,
	// then a static one. With p_static_only, instance members are passed over, as they are for code in a
	// static method; *r_instance receives the first one passed over. *r_hidden receives a match that exists
	// but is not visible from the class.
	void settle_hierarchy();
	Symbol *find_member(Symbol *p_class, std::string_view p_name, bool p_static_only, Symbol **r_instance,
			Symbol **r_hidden) const;
	// True if the members that the class inherits are not all known: a base failed to resolve, or is a
	// built-in class whose members are not declared yet.
	bool hierarchy_broken(Symbol *p_class) const;
	// The getter or the setter of a property reached through p_through (null: the property's own owner). A
	// class's property holds both halves, inherited ones too; through an interface the other half may be
	// declared in an interface it extends. *r_known: whether a missing half is certainly missing.
	FunctionDecl *property_half(Symbol *p_through, const Symbol *p_property, FunctionDecl::Accessor p_half,
			bool *r_known = nullptr);

	// "a.b.C", "C", or "C in this file" for a helper; "Owner.name" for a member.
	std::string qualified_name(const Symbol *p_symbol) const;
	// "line 3", "Other.as:3:5", or "the built-in library".
	std::string where(const Unit &p_from, const Symbol *p_symbol) const;
	std::string ambiguous_message(std::string_view p_name, const Found &p_found) const;
};

// A name as it is shown in a message: 'name'.
inline std::string quoted(std::string_view p_name) {
	std::string result = "'";
	result += p_name;
	result += '\'';
	return result;
}

// The symbol a table holds under the name, or null.
inline Symbol *find_in(const std::unordered_map<std::string_view, Symbol *> &p_table, std::string_view p_name) {
	auto found = p_table.find(p_name);
	return found == p_table.end() ? nullptr : found->second;
}

// What code in a class of package p_package may name on a base class p_base: p_member is not private, and
// if it is internal, p_base is in that package.
bool inherited_visible(const Symbol *p_member, const Symbol *p_base, const Package *p_package);

// Whether a class written in a declaration file carries a tag: [Abstract], [Singleton].
bool has_tag(const Symbol *p_class, std::string_view p_tag);
// Whether a field is written `[Signal] public var hit:Signal;`: it names a signal of the script's own.
bool declares_signal(const VarDecl *p_decl);
// Whether a field is written `[Export] public var speed:Number;`: Godot's inspector shows it.
bool declares_export(const VarDecl *p_decl);
// The number literal written, alone or after a `-` (`5`, `-1.5`), or null; r_negative says which.
const NumberLit *written_number(const Expr *p_value, bool *r_negative);
// Whether an [Export] field's first value is a constant Godot can be told: a number, a String, a Boolean or
// null, or `new` with numbers only (the checker makes sure the class is one of Godot's values).
bool export_constant(const Expr *p_value);
// The first p_count parts of a name written with dots ("flash.utils"), joined again.
std::string dotted_name(const List<std::string_view> &p_parts, uint32_t p_count);

} // namespace kas3
