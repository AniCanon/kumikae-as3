// Where an object's fields are. Every read and write of a field of an object, by the interpreter, the
// library, the collector and the hosts, comes through here, so that how a class lays its fields out is said
// in this file alone. Today each field is one slot, in the order of the class's description
// (ir::Class::fields, a base's first): a field that takes more than one (one of Godot's values held inline)
// changes field_slot() and slots_of(), and nothing that calls them.
#pragma once

#include "ir.h"
#include "runtime.h"

namespace kas3 {
namespace vm {

// The slot a field of the class starts at, by its number in the class's description.
inline size_t field_slot(const ir::Class &, uint32_t p_field) {
	return p_field;
}

// How many slots an object of the class is made with.
inline size_t slots_of(const ir::Class &p_class) {
	return p_class.fields.size();
}

// A field of an object, by the description of its class or of one that class extends. What the slot holds
// is the description's to say (ir::Field: its kind, the class or the Godot type it is declared to hold).
inline Slot &field(const ir::Class &p_class, Object *p_object, uint32_t p_field) {
	return slot_at(p_object, field_slot(p_class, p_field));
}

// A field of an object of one of the library's own classes (Function, Class, Error, RegExp, Date), by the
// number ir/ gives it, and the one slot of an Object or a Dictionary, which holds its Entries (vm/table.h):
// each of those is one slot by its making, whatever a program's classes come to hold.
inline Slot &own_field(Object *p_object, uint32_t p_field) {
	return slot_at(p_object, p_field);
}

// The Godot object of a script object whose class extends one of Godot's, as a handle: the first field of
// every such class (the lowerer's "$owner").
inline Slot &owner_field(Object *p_object) {
	return slot_at(p_object, 0);
}

// Every field as it is before anything is written to it (empty_value()).
inline void reset_fields(const ir::Class &p_class, Object *p_object) {
	for (uint32_t i = 0; i < p_class.fields.size(); i++) {
		field(p_class, p_object, i) = empty_value(p_class.fields[i].kind == ir::Kind::F64, p_class.fields[i].starts_null);
	}
}

} // namespace vm
} // namespace kas3
