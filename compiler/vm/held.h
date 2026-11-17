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

#include "fields.h"
#include "ir.h"
#include "runtime.h"
#include "table.h"

namespace kas3 {
namespace vm {

// The properties of an Object, a Dictionary, or an Array beyond its indices (vm/table.h): each key (a
// String, or a Dictionary's object) and each value, all untyped. A deleted entry holds nothing.
template <typename Visit>
void each_entry(Entries *p_entries, Visit &&p_visit) {
	for (Entry &entry : p_entries->all()) {
		if (any::tag(entry.key) != AnyTag::Undefined) {
			p_visit(ir::Kind::Any, entry.key);
			p_visit(ir::Kind::Any, entry.value);
		}
	}
}

template <typename Visit>
void each_held(const ir::Class &p_class, Object *p_object, Visit &&p_visit) {
	if (p_class.table) {
		each_entry(entries_of(p_object), p_visit);
		return;
	}
	if (p_class.element != ir::Kind::Void) {
		Vector *vector = static_cast<Vector *>(p_object);
		const ir::Kind kind = place_kind(p_class);
		for (uint32_t i = 0; ir::is_held(kind) && i < vector->length; i++) {
			p_visit(kind, slots_of(vector)[i]); // A place that holds something counted is a slot.
		}
		if (vector->properties) {
			each_entry(vector->properties, p_visit);
		}
		return;
	}
	for (uint32_t i = 0; i < p_class.fields.size(); i++) {
		if (ir::is_held(p_class.fields[i].kind)) {
			p_visit(p_class.fields[i].kind, field(p_class, p_object, i));
		}
	}
}

// Before what an object holds is let go of (freeing it, or the collector emptying its garbage): a table's
// or an Array's properties are taken out first, and the container is left empty. Letting go of one can
// run engine code that reaches the container, which then finds it whole and empty, never half emptied.
// The caller lets go of each key and value, untyped, and each_held() then visits nothing of them.
inline std::vector<Entry> take_entries(const ir::Class &p_class, Object *p_object) {
	if (p_class.table) {
		return entries_of(p_object)->take_all();
	}
	if (p_class.array && static_cast<Vector *>(p_object)->properties) {
		return static_cast<Vector *>(p_object)->properties->take_all();
	}
	return {};
}

// Whether an object of the class can hold anything that may lead back to it: another object, an untyped
// value, or one of Godot's objects (which may listen to, or be scripted by, a script object). Only these are
// looked at by the cycle collector. A value of one of Godot's value types (a Vector2, a Callable, an Array)
// is not followed: Godot does not say how many hold one. Nor is a vector of Godot's objects of a class
// that is never reference-counted (nodes: ir::Class::element_counted): the engine counts no reference to
// such an object, so none of them is a node of the collector's (EngineHandles::node), and a frame of a
// game that keeps twenty thousand nodes in a vector does not walk them.
// This is what the class alone says. A vector of objects of the program's classes says more with the
// classes its elements may be of, which the collector reads with the whole module before it (collector.cpp,
// classes_looked_at): one whose elements hold nothing but numbers is not looked at either.
inline bool may_hold_objects(const ir::Class &p_class) {
	auto leads = [](ir::Kind p_kind) { return p_kind == ir::Kind::Obj || p_kind == ir::Kind::Any || p_kind == ir::Kind::Ref; };
	if (p_class.table || p_class.array) {
		return true; // Untyped values, in its properties.
	}
	if (p_class.element != ir::Kind::Void) {
		return leads(p_class.element) && !(p_class.element == ir::Kind::Ref && !p_class.element_counted);
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
