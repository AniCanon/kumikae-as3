// Questions about a checked program: what is at this position, where is it declared, who refers to it.
// The language server asks them, and later the Godot editor.
#pragma once

#include "names.h"

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

// Built over a Compilation after check(). It reads the trees the passes wrote onto, so it is good for as
// long as that Compilation is the one using them.
class Queries {
	Compilation &compilation_;
	std::unordered_map<const Unit *, std::vector<Occurrence>> occurrences_;

	const std::vector<Occurrence> &occurrences(const Unit &p_unit);

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
};

} // namespace kas3
