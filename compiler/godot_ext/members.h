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

// What Godot reaches of a script by a name: the member find_member() gives, and the public getter and
// setter of that AS3 name ("get x", "set x"), each NONE if there is none. Made once per class for the
// program that runs, on the main thread, for every name of the class that reaches something: Godot asks the
// script about every property set or read on its object (`position` too, and any name a game makes up),
// and the answer must not cost a conversion of the name each time, nor keep the name.
struct Reach {
	Member member;
	uint32_t getter = ir::NONE;
	uint32_t setter = ir::NONE;
};
Reach reach(uint32_t p_class, const godot::StringName &p_name);

// A field as Godot is given it. Godot gets null for a script object it can't hold, and is told so.
godot::Variant read_field(const vm::Object *p_object, uint32_t p_field);
// A value from Godot stored in a field, converted as a store into the field converts: an Array field takes
// a copy of Godot's Array, a Dictionary field a copy of Godot's Dictionary, or null. False, the field
// unchanged, when the value does not fit or the field can't be written from Godot (a const, a signal, an
// object of a class that extends none of Godot's, a Date). A field of a script's class takes the Godot
// object that carries such a script, as that script's object.
bool write_field(vm::Object *p_object, uint32_t p_field, const godot::Variant &p_value);

// The one kind of Godot's value that comes into a register or field of this class, an AS3 container: an
// Array's Godot Array, a Dictionary's (flash.utils) Godot Dictionary; NIL for any other class, of which no
// object comes from Godot.
godot::Variant::Type container_type(uint32_t p_class);
// That value copied, or null for Godot's null. Nothing if the copy failed: the error has been reported.
std::optional<vm::Slot> container_from_variant(uint32_t p_class, const godot::Variant &p_value);
// Whether a value of Godot's may be taken where an object of the class is declared (a parameter, a field):
// for an Array or a Dictionary, Godot's own or null; for a class that extends one of Godot's, the Godot
// object that carries a script of that class or of one that extends it (program.h, arriving_script), null,
// or an object that has been freed, which is null. Nothing else: an object of any other class of the
// program has no Godot object to come by.
bool object_arrives(uint32_t p_class, const godot::Variant &p_value);
// What object_arrives() said yes to, as the place holds it, the caller's to let go of. Nothing if a copy
// failed: the error has been reported.
std::optional<vm::Slot> object_from_variant(uint32_t p_class, const godot::Variant &p_value);

// The class's public fields, getters and setters as Godot describes properties, and its public methods as
// Godot describes methods, those of its base classes included.
godot::TypedArray<godot::Dictionary> property_list(uint32_t p_class);
godot::TypedArray<godot::Dictionary> method_list(uint32_t p_class);

// An exported field's first value as Godot is told it, to compare a scene's value with. Nil and false when
// it was not written as a constant.
bool field_default(uint32_t p_class, const godot::StringName &p_name, godot::Variant *r_value);

} // namespace kas3
