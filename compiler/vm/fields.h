// Where an object's fields are. Every read and write of a field of an object, by the interpreter, the
// library, the collector and the hosts, comes through here, so that how a class lays its fields out is said
// in this file alone. Today each field is one slot, in the order of the class's description
// (ir::Class::fields, a base's first): a field that takes more than one (one of Godot's values held inline)
// changes field_slot() and slots_of(), and nothing that calls them. What a vector's places are kept as is
// said here too (place_kind()).
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

// What a place of a vector is counted, copied and let go of as: its element's kind, or, where each place
// holds one of Godot's values whole (ir::Class::element_bare), a kind that counts nothing. A value read
// out of such a place, or stored into one, is another matter: it crosses into a handle and back
// (vm/library.h, read_element and write_element).
inline ir::Kind place_kind(const ir::Class &p_class) {
	return p_class.element_bare == ir::Bare::None ? p_class.element : ir::Kind::Void;
}
// A packed numeric vector stores one or four bytes per place. Other vectors store a full slot.
inline bool narrow_places(const ir::Class &p_class) {
	return p_class.element == ir::Kind::U8 || p_class.element == ir::Kind::F32 || p_class.element == ir::Kind::I32 || p_class.element == ir::Kind::U32;
}

// A new vector of a class: new_vector(), told what the class says its places hold.
inline Vector *new_vector_of(const ir::Class &p_class, uint32_t p_index, uint32_t p_length, bool p_fixed, Object *p_list) {
	Vector *vector = new_vector(p_index, p_length, p_fixed, static_cast<uint8_t>(p_class.element_bare), narrow_places(p_class), p_list, p_class.element == ir::Kind::U8);
	if (vector) {
		vector->floating = p_class.element == ir::Kind::F32;
	}
	return vector;
}

// Every field as it is before anything is written to it (empty_value()).
inline void reset_fields(const ir::Class &p_class, Object *p_object) {
	for (uint32_t i = 0; i < p_class.fields.size(); i++) {
		field(p_class, p_object, i) = empty_value(ir::is_floating(p_class.fields[i].kind), p_class.fields[i].starts_null);
	}
}

} // namespace vm
} // namespace kas3
