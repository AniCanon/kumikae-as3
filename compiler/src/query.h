// Questions about a checked program: what is at this position, where is it declared, who refers to it.
// The language server asks them, and later the Godot editor.
#pragma once

#include "names.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kas3 {

// A name in the source that refers to a symbol, or declares it.
struct Occurrence {
	uint32_t offset;
	uint32_t length;
	Symbol *symbol;
	bool is_declaration;
};

struct Place {
	const Unit *unit = nullptr; // Null when there is no place: the implicit `arguments`.
	uint32_t offset = 0;
	uint32_t length = 0;
};

// One line of a file's outline.
struct OutlineItem {
	std::string name; // Never empty. "get x" and "set x" for accessors.
	SymbolKind kind = SymbolKind::Class;
	bool is_const = false;
	bool is_constructor = false;
	uint32_t offset = 0; // The whole declaration: [offset, end).
	uint32_t end = 0;
	uint32_t name_offset = 0; // Inside it.
	uint32_t name_length = 0;
	std::vector<OutlineItem> children;
};

// A definition found by name.
struct FoundSymbol {
	std::string name;
	std::string container; // The class for a member, otherwise the package. May be empty.
	SymbolKind kind = SymbolKind::Class;
	bool is_const = false;
	bool is_constructor = false;
	const Unit *unit = nullptr;
	uint32_t offset = 0;
	uint32_t length = 0;
};

struct CompletionRange {
	uint32_t begin;
	uint32_t caret;
	uint32_t end;
	bool call = false; // A standalone expression statement, not a possible function reference.
};

struct SignatureHelp {
	std::string label;
	std::vector<std::string> parameters;
	uint32_t active_parameter = 0;
	std::string active_name; // The active parameter's name. Empty if the function takes none.
};

struct CompletionEdit {
	uint32_t begin = 0;
	uint32_t end = 0;
	std::string text;
};

struct CompletionTabStop {
	uint32_t begin; // Byte span in edit.text, ordered by source position; empty spans are caret stops.
	uint32_t end;
	uint32_t index; // Repeated indices mirror one another; zero is the final caret.
};

// One thing completion offers.
struct Completion {
	std::string name;
	SymbolKind kind = SymbolKind::Local; // Not meaningful for a keyword.
	bool is_keyword = false;
	bool is_package = false;
	bool is_const = false;
	std::string detail; // The declaration as one line of AS3. Empty for a keyword.
	bool callable = false;
	std::vector<std::string> required_parameters;
	bool is_parameter_name = false; // In a call's argument, the name of that argument's parameter.
	std::optional<CompletionEdit> edit;
	std::optional<CompletionEdit> additional_edit;
	std::vector<CompletionTabStop> tab_stops;
};

// True if the name starts with the prefix, whatever the case of either: how completion narrows.
bool starts_with_any_case(std::string_view p_name, std::string_view p_prefix);

// What is written where `override` comes before a member's name.
struct OverrideOffer {
	std::vector<Completion> items;
	bool after_function = false; // `function` is written already: no keyword can follow.
};

struct CompletionResult {
	std::vector<Completion> items;
	bool incomplete = false; // More candidates become eligible once the user types a prefix.
};

// Built over one state of the declarations. Discard queries before an update of the compilation, and tell
// them of each file whose bodies are checked.
class Queries {
	Compilation &compilation_;
	std::unordered_map<const Unit *, std::vector<Occurrence>> occurrences_;
	std::unordered_map<const Symbol *, Completion> completion_items_;
	mutable std::unordered_map<const Unit *, std::vector<Node *>> context_nodes_;
	const std::vector<Node *> &context_nodes(const Unit &p_unit) const;
	const Completion &completion_item(Symbol *p_symbol);
	static const FunctionDecl *function_declaration(const Symbol *p_symbol);
	CompletionEdit import_edit(const Unit &p_unit, uint32_t p_offset, std::string_view p_name) const;
	// Where `override` is written before a member's name in p_class: each member of a base class that it
	// may override, written whole. p_function is the declaration the parser made of what is written, if
	// it made one; otherwise the words before p_start are read, and must be modifiers with `override`.
	std::optional<OverrideOffer> overrides(const Unit &p_unit, const FunctionDecl *p_function, Symbol *p_class,
			uint32_t p_start, uint32_t p_offset);
	static std::string line_break(const Unit &p_unit);
	static std::string line_indent(const Unit &p_unit, uint32_t p_offset);
	void statement_templates(const Unit &p_unit, uint32_t p_offset, std::vector<Completion> &p_items) const;
	static bool block_start(const Unit &p_unit, const Block *p_block, uint32_t p_offset);
	bool statement_start(const Unit &p_unit, uint32_t p_offset) const;
	static uint32_t skip_trivia(const Unit &p_unit, uint32_t p_offset);

	const std::vector<Occurrence> &occurrences(const Unit &p_unit);
	std::vector<const Symbol *> types_; // Every class and interface, once asked for.
	bool types_listed_ = false;
	const std::vector<const Symbol *> &types();
	// The methods that are the same thing as this one because of an interface: for an interface's method
	// the methods that implement it, and for a class's method the interface methods it implements.
	std::unordered_set<const Symbol *> same_through_interfaces(const Symbol *p_member);
	friend class Completer;

public:
	explicit Queries(Compilation &p_compilation) :
			compilation_(p_compilation) {}
	// Drops what was worked out from a file whose bodies were checked again.
	void forget(const Unit &p_unit);

	// The name the offset is in, or that ends right at it. Null if there is none, or it resolved to nothing.
	const Occurrence *at(const Unit &p_unit, uint32_t p_offset);
	Place declaration(const Symbol *p_symbol) const;
	// Where go to definition leads from the offset: the declaration of the name there, or for the class
	// in `new X()` its constructor, if it declares one. No place if there is no name there.
	Place definition(const Unit &p_unit, uint32_t p_offset);
	// Every use in the program's files, in file order, then by position. Declaration files are searched
	// only for the declaration itself. For a method, the methods that override it and the one it overrides
	// count as the same thing: a call through a base class may reach any of them. So do an interface's
	// method and the methods that implement it, in the class itself or in a base class.
	std::vector<Place> references(const Symbol *p_symbol, bool p_with_declaration);
	// The declaration as one line of AS3: "function hit(by:Node, power:int = 1):void".
	std::string describe(const Symbol *p_symbol) const;
	// A parameter as its declaration writes it: "...rest", "power:int". From the declaration alone, so that
	// it reads the same whether or not the function's file has had its bodies checked.
	static std::string parameter_text(const Unit &p_unit, const Parameter &p_parameter);
	// What it is and where it lives: "method of game.Player", "class in package godot", "local variable".
	std::string context(const Symbol *p_symbol) const;
	std::vector<OutlineItem> outline(const Unit &p_unit) const;
	// Definitions whose name contains the query, whatever the case: the program's own, then those of the
	// declaration files (Godot's API, the built-in library). An empty query matches everything in the
	// program, and nothing outside it.
	std::vector<FoundSymbol> find(std::string_view p_query, size_t p_limit);
	// What could be written at the offset, narrowed by the letters already there, whatever their case.
	// After a dot: what the value before it has (Object's members if its type is `*` or not known), or
	// for a package's path what the package holds. Where a type is written: types. Otherwise the names in
	// reach (locals, members, definitions, keywords). Nothing inside a comment or a string, and nothing
	// after a name that means nothing here.
	// Counts support bounded-state tests and benchmarks; caches are owned by this checked generation.
	size_t cached_completion_items() const { return completion_items_.size(); }
	size_t cached_context_units() const { return context_nodes_.size(); }
	const LocalScope *local_scope_at(const Unit &p_unit, uint32_t p_offset) const;
	bool text_byte(const Unit &p_unit, uint32_t p_offset) const;
	bool in_text(const Unit &p_unit, uint32_t p_offset) const;
	CompletionRange completion_range(const Unit &p_unit, uint32_t p_offset) const;
	std::optional<SignatureHelp> signature_help(const Unit &p_unit, uint32_t p_offset) const;
	CompletionResult complete(const Unit &p_unit, uint32_t p_offset);
};

} // namespace kas3
