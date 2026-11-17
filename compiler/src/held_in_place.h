// Which of Godot's value types a vector holds in its places, written once for the checker and the lowerer:
// a `Vector.<Vector2>` and a `Vector.<Vector2i>` keep each value itself where it is, eight bytes, as a C#
// array of structs does. Such a place is never null, so the checker refuses `null` written where an element
// goes (typer_calls.cpp, no_null_place), and the lowerer makes the vector's class one that holds its
// elements whole (ir::Class::element_bare).
#pragma once

#include "builtins.h"
#include "names.h"

namespace kas3 {

// Whether a vector of elements of that type holds each in its place: Godot's own Vector2 or Vector2i, not a
// class of the program that has the name.
inline bool held_in_place(const Type *p_element) {
	const Symbol *symbol = p_element && p_element->kind == Type::Instance ? p_element->symbol : nullptr;
	const bool godot = symbol && symbol->kind == SymbolKind::Class && symbol->unit && symbol->unit->is_declaration &&
			symbol->unit->source.path() == GODOT_DECLARATIONS_PATH;
	return godot && (symbol->name == "Vector2" || symbol->name == "Vector2i");
}

} // namespace kas3
