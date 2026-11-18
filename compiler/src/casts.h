// What a class called with one value does (`T(value)`), written once for the checker's advice and the
// lowerer.
#pragma once

#include "builtins.h"
#include "names.h"

namespace kas3 {

enum class ClassCall {
	// AS3's cast: the value itself when it is a T, null for null and undefined, and TypeError #1034 for
	// anything else. A class or an interface of the program's own, a class of Godot's (of its objects, or a
	// value type such as Vector2), Function, Class and Dictionary.
	Cast,
	// A conversion that always gives a T: String, Number, int, uint, Boolean and Int64.
	Conversion,
	// Something of the class's own, which is no way to say that a value is a T: Array(x) makes an array,
	// Object(x) gives any value back, Date(x) gives text, Error(x) makes an error.
	Other,
};

inline ClassCall class_call(const Symbol *p_class) {
	if (!p_class || !p_class->unit || (p_class->kind != SymbolKind::Class && p_class->kind != SymbolKind::Interface)) {
		return ClassCall::Other;
	}
	const std::string_view name = p_class->name;
	if (p_class->unit->is_builtin) {
		if (name == "Function" || name == "Class" || name == "Dictionary") {
			return ClassCall::Cast;
		}
		const bool converts = name == "String" || name == "Number" || name == "int" || name == "uint" || name == "Boolean" || name == "Int64";
		return converts ? ClassCall::Conversion : ClassCall::Other;
	}
	if (!p_class->unit->is_declaration) {
		return ClassCall::Cast;
	}
	if (p_class->unit->source.path() != GODOT_DECLARATIONS_PATH) {
		return ClassCall::Other;
	}
	// One of Godot's: a cast, for a class of objects and for a value type (Vector2, Color).
	return p_class->kind == SymbolKind::Class ? ClassCall::Cast : ClassCall::Other;
}

} // namespace kas3
