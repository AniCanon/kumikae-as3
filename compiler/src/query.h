// Questions about a checked program: what is at this position, where is it declared, who refers to it.
// The language server asks them, and later the Godot editor.
#pragma once

#include "names.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
};

struct CompletionEdit {
	uint32_t begin = 0;
	uint32_t end = 0;
	std::string text;
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
	std::optional<CompletionEdit> edit;
	std::optional<CompletionEdit> additional_edit;
	std::optional<uint32_t> caret; // Byte offset in edit.text for clients supporting a final tab stop.
};

struct CompletionResult {
	std::vector<Completion> items;
	bool incomplete = false; // More candidates become eligible once the user types a prefix.
};

// Built over one checked generation. Discard queries before the compilation's trees or body scopes
// change, including a successful update_bodies that preserves the Compilation itself.
class Queries {
	Compilation &compilation_;
	std::unordered_map<const Unit *, std::vector<Occurrence>> occurrences_;
	std::unordered_map<const Symbol *, Completion> completion_items_;
	mutable std::unordered_map<const Unit *, std::vector<Node *>> context_nodes_;
	const std::vector<Node *> &context_nodes(const Unit &p_unit) const;
	const Completion &completion_item(Symbol *p_symbol);
	static const FunctionDecl *function_declaration(const Symbol *p_symbol);
	CompletionEdit import_edit(const Unit &p_unit, uint32_t p_offset, std::string_view p_name) const;
	std::vector<Completion> overrides(const Unit &p_unit, const FunctionDecl &p_function, Symbol *p_class, uint32_t p_offset);
	static std::string line_break(const Unit &p_unit);
	static std::string line_indent(const Unit &p_unit, uint32_t p_offset);

	const std::vector<Occurrence> &occurrences(const Unit &p_unit);
	friend class Completer;

public:
	explicit Queries(Compilation &p_compilation) :
			compilation_(p_compilation) {}

	// The name the offset is in, or that ends right at it. Null if there is none, or it resolved to nothing.
	const Occurrence *at(const Unit &p_unit, uint32_t p_offset);
	Place declaration(const Symbol *p_symbol) const;
	// Every use in the program's files, in file order, then by position. Declaration files are searched
	// only for the declaration itself. For a method, the methods that override it and the one it overrides
	// count as the same thing: a call through a base class may reach any of them.
	std::vector<Place> references(const Symbol *p_symbol, bool p_with_declaration);
	// The declaration as one line of AS3: "function hit(by:Node, power:int = 1):void".
	std::string describe(const Symbol *p_symbol) const;
	// What it is and where it lives: "method of game.Player", "class in package godot", "local variable".
	std::string context(const Symbol *p_symbol) const;
	std::vector<OutlineItem> outline(const Unit &p_unit) const;
	// Definitions in the program's files whose name contains the query, whatever the case. An empty
	// query matches everything.
	std::vector<FoundSymbol> find(std::string_view p_query, size_t p_limit);
	// What could be written at the offset, narrowed by the letters already there, whatever their case.
	// After a dot: what the value before it has. Where a type is written: types. Otherwise the names in
	// reach (locals, members, definitions, keywords). Nothing inside a comment or a string, and nothing
	// after a dot whose value has no known type.
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
