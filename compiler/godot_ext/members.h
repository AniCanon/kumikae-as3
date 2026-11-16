// What Godot reaches of a script by name: its public fields, to read and write, and its public methods, with
// Godot's types. Every lookup by name (get, set, has_method, call, the property and method lists) is made
// here, so that they agree.
#pragma once

#include "ir.h"
#include "runtime.h"

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <optional>

namespace kas3 {

// A member Godot asked for by name.
struct Member {
	enum class What : uint8_t { None, Field, Method };
	What what = What::None;
	uint32_t index = ir::NONE; // The field's slot, or the method's function.
};

// The member of the class Godot may reach by that name: a public field (the class's own before a base's), a
// public method by its AS3 name, or a virtual of Godot's the class answers, by Godot's name. Private,
// protected and internal members are not reached from Godot.
Member find_member(uint32_t p_class, const godot::StringName &p_name);

// A field as Godot is given it. Godot gets null for a script object it can't hold, and is told so.
godot::Variant read_field(const vm::Object *p_object, uint32_t p_field);
// A value from Godot stored in a field, converted as a store into the field converts: an Array field takes
// a copy of Godot's Array, a Dictionary field a copy of Godot's Dictionary, or null. False, the field
// unchanged, when the value does not fit or the field can't be written from Godot (a const, a signal, a
// script object, a Date).
bool write_field(vm::Object *p_object, uint32_t p_field, const godot::Variant &p_value);

// The one kind of Godot's value that comes into a register or field of this class, an AS3 container: an
// Array's Godot Array, a Dictionary's (flash.utils) Godot Dictionary; NIL for any other class, of which no
// object comes from Godot.
godot::Variant::Type container_type(uint32_t p_class);
// That value copied, or null for Godot's null. Nothing if the copy failed: the error has been reported.
std::optional<vm::Slot> container_from_variant(uint32_t p_class, const godot::Variant &p_value);

// The class's public fields, getters and setters as Godot describes properties, and its public methods as
// Godot describes methods, those of its base classes included.
godot::TypedArray<godot::Dictionary> property_list(uint32_t p_class);
godot::TypedArray<godot::Dictionary> method_list(uint32_t p_class);

// An exported field's first value as Godot is told it, to compare a scene's value with. Nil and false when
// it was not written as a constant.
bool field_default(uint32_t p_class, const godot::StringName &p_name, godot::Variant *r_value);

} // namespace kas3
