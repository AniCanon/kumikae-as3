// What an object holds: the one place that says so. Freeing an object lets go of each slot this visits, and
// the cycle collector (vm/collector.h) follows each, so a kind of container added later (an Array, a keyed
// table) adds its slots here and is then both freed and collected.
//
// A slot is visited with the kind it holds: an object's fields by their declared kinds, a vector's elements
// by its element kind. A function value is an object of the Function class, whose fields are the function's
// index and what it is called with first (ir::FUNCTION_FIRST: a method's object, or a closure's
// environment), so its bound object or environment is visited as its field. A slot that holds nothing that
// must be let go (a number) is not visited.
#pragma once

#include "ir.h"
#include "runtime.h"

namespace kas3 {
namespace vm {

template <typename Visit>
void each_held(const ir::Class &p_class, Object *p_object, Visit &&p_visit) {
	if (p_class.element != ir::Kind::Void) {
		if (!ir::is_held(p_class.element)) {
			return;
		}
		Vector *vector = static_cast<Vector *>(p_object);
		for (uint32_t i = 0; i < vector->length; i++) {
			p_visit(p_class.element, vector->data[i]);
		}
		return;
	}
	Slot *fields = p_object->fields();
	for (size_t i = 0; i < p_class.fields.size(); i++) {
		if (ir::is_held(p_class.fields[i].kind)) {
			p_visit(p_class.fields[i].kind, fields[i]);
		}
	}
}

// Whether an object of the class can hold anything that may lead back to it: another object, an untyped
// value, or one of Godot's objects (which may listen to, or be scripted by, a script object). Only these are
// looked at by the cycle collector. A value of one of Godot's value types (a Vector2, a Callable, an Array)
// is not followed: Godot does not say how many hold one.
inline bool may_hold_objects(const ir::Class &p_class) {
	auto leads = [](ir::Kind p_kind) { return p_kind == ir::Kind::Obj || p_kind == ir::Kind::Any || p_kind == ir::Kind::Ref; };
	if (p_class.element != ir::Kind::Void) {
		return leads(p_class.element);
	}
	for (const ir::Field &field : p_class.fields) {
		if (leads(field.kind)) {
			return true;
		}
	}
	return false;
}

} // namespace vm
} // namespace kas3
