// What a class called with one value does (`T(value)`), written once for the checker's advice and the
// lowerer.
#pragma once

#include "builtins.h"
#include "names.h"

namespace kas3 {

enum class ClassCall {
	// AS3's cast: the value itself when it is a T, null for null and undefined, and TypeError #1034 for
	// anything else. A class or an interface of the program's own, a class of Godot's objects, Function
	// and Class.
	Cast,
	// A conversion that always gives a T: String, Number, int, uint, Boolean and Int64.
	Conversion,
	// Something of the class's own, which is no way to say that a value is a T: Array(x) makes an array,
	// Object(x) gives any value back, Date(x) gives text, Error(x) makes an error. A Godot value type has
	// no cast.
	Other,
};

// Whether the class is one of Godot's value types (Vector2, Color): a value is said to be one neither by
// a cast nor by `as`, which ask only of objects and of AS3's own values.
inline bool is_godot_value_type(const Symbol *p_class) {
	if (!p_class || !p_class->unit || p_class->kind != SymbolKind::Class || !p_class->unit->is_declaration ||
			p_class->unit->source.path() != GODOT_DECLARATIONS_PATH) {
		return false;
	}
	for (const Symbol *at = p_class; at && at->klass; at = at->klass->base) {
		if (at->name == "GodotObject") {
			return false;
		}
	}
	return true;
}

inline ClassCall class_call(const Symbol *p_class) {
	if (!p_class || !p_class->unit || (p_class->kind != SymbolKind::Class && p_class->kind != SymbolKind::Interface)) {
		return ClassCall::Other;
	}
	const std::string_view name = p_class->name;
	if (p_class->unit->is_builtin) {
		if (name == "Function" || name == "Class") {
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
	// One of Godot's: a cast for a class of objects, each of which extends GodotObject.
	for (const Symbol *at = p_class; at && at->klass; at = at->klass->base) {
		if (at->name == "GodotObject") {
			return ClassCall::Cast;
		}
	}
	return ClassCall::Other;
}

} // namespace kas3
