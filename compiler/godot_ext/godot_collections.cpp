// AS3's vectors as Godot's collections.
#include "godot_host.h"
#include "program.h"
#include "any.h"
#include "table.h"
#include "handles.h"

#include <godot_cpp/variant/dictionary.hpp>

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_vector4_array.hpp>

#include <cstring>
#include <limits>
#include <new>

using namespace godot;

namespace kas3 {

// A slot holds one of these whole, as Godot lays it out: a vector's places are a packed array's elements.
static_assert(sizeof(Vector2) == sizeof(vm::Slot) && sizeof(Vector2i) == sizeof(vm::Slot), "a slot holds a Vector2 or a Vector2i whole");

namespace {

// One of Godot's values out of the slot that holds it whole (ir::Bare).
Variant bare_variant(ir::Bare p_type, vm::Slot p_value) {
	if (p_type == ir::Bare::Vector2i) {
		Vector2i value;
		std::memcpy(static_cast<void *>(&value), &p_value, sizeof(value));
		return value;
	}
	Vector2 value;
	std::memcpy(static_cast<void *>(&value), &p_value, sizeof(value));
	return value;
}

// An Array's places, one after another as Godot keeps them (godot-cpp's own iterators go by the same), to
// write each without a call: nothing for an empty one.
Variant *places_of(Array &p_array) {
	return p_array.is_empty() ? nullptr : &p_array[0];
}

// A vector's element written where a new Array holds nothing yet: a number or a Boolean is made there, by one
// call of Godot's, and anything else is what to_variant() gives. The place holds nil, which has nothing to
// let go of, so a value is made over it as it is.
void place(Variant *r_place, ir::Kind p_kind, const vm::Slot &p_slot) {
	switch (p_kind == ir::Kind::Any ? vm::any::tag(p_slot) : vm::AnyTag::Undefined) {
		case vm::AnyTag::Int:
			new (r_place) Variant(static_cast<int64_t>(vm::any::to_int_payload(p_slot)));
			return;
		case vm::AnyTag::Number:
			new (r_place) Variant(vm::any::to_number_payload(p_slot));
			return;
		case vm::AnyTag::Boolean:
			new (r_place) Variant(vm::any::to_bool_payload(p_slot));
			return;
		default:
			break;
	}
	switch (p_kind) {
		case ir::Kind::I32:
			new (r_place) Variant(static_cast<int64_t>(p_slot.i));
			return;
		case ir::Kind::U32:
			new (r_place) Variant(static_cast<int64_t>(p_slot.u));
			return;
		case ir::Kind::I64:
			new (r_place) Variant(p_slot.l);
			return;
		case ir::Kind::F64:
			new (r_place) Variant(p_slot.d);
			return;
		case ir::Kind::Bool:
			new (r_place) Variant(p_slot.b);
			return;
		default:
			*r_place = to_variant(p_kind, p_slot);
	}
}

} // namespace

void *GodotHost::boxed(ir::Bare p_type, vm::Slot p_value, std::string *) {
	return make_handle(bare_variant(p_type, p_value));
}

bool GodotHost::unboxed(ir::Bare p_type, void *p_handle, vm::Slot *r_value) {
	const Variant &held = *static_cast<const Variant *>(p_handle);
	r_value->bits = 0;
	if (p_type == ir::Bare::Vector2 && held.get_type() == Variant::VECTOR2) {
		const Vector2 value = held;
		std::memcpy(r_value, static_cast<const void *>(&value), sizeof(value));
		return true;
	}
	if (p_type == ir::Bare::Vector2i && held.get_type() == Variant::VECTOR2I) {
		const Vector2i value = held;
		std::memcpy(r_value, static_cast<const void *>(&value), sizeof(value));
		return true;
	}
	return false;
}

namespace {

// How deep the containers being made are (vm::MAX_ENGINE_NESTING).
int nesting = 0;

struct Deeper {
	Deeper() { nesting++; }
	~Deeper() { nesting--; }
};

// An Object or a Dictionary as one of Godot's Dictionaries: each key a String, or for an object key what
// that object is to Godot; each value as an untyped value goes.
bool table_element(const vm::Object *p_table, Variant *r_out, std::string *r_error) {
	Dictionary dictionary;
	for (const vm::Entry &entry : vm::entries_of(const_cast<vm::Object *>(p_table))->all()) {
		if (vm::any::tag(entry.key) == vm::AnyTag::Undefined) {
			continue;
		}
		Variant key;
		if (vm::any::tag(entry.key) == vm::AnyTag::Object && !object_element(static_cast<const vm::Object *>(vm::any::pointer(entry.key)), &key, r_error)) {
			return false;
		}
		if (vm::any::tag(entry.key) != vm::AnyTag::Object) {
			key = untyped_to_variant(entry.key);
		}
		Variant value;
		if (vm::any::tag(entry.value) == vm::AnyTag::Object && !object_element(static_cast<const vm::Object *>(vm::any::pointer(entry.value)), &value, r_error)) {
			return false;
		}
		dictionary[key] = vm::any::tag(entry.value) == vm::AnyTag::Object ? value : untyped_to_variant(entry.value);
	}
	*r_out = dictionary;
	return true;
}

} // namespace

// A script object as Godot holds it: one that is one of Godot's as its Godot object, a vector or an Array
// as an Array of its own elements, and an Object or a Dictionary as a Dictionary.
bool object_element(const vm::Object *p_object, Variant *r_out, std::string *r_error) {
	if (!p_object) {
		*r_out = Variant();
		return true;
	}
	const Deeper deeper;
	if (nesting > vm::MAX_ENGINE_NESTING) {
		*r_error = vm::HOLDS_ITSELF;
		return false;
	}
	const ir::Module &module = Project::get().module();
	if (module.classes[p_object->klass].table) {
		return table_element(p_object, r_out, r_error);
	}
	const ir::Kind element = module.classes[p_object->klass].element;
	const ir::Bare bare = module.classes[p_object->klass].element_bare;
	if (element != ir::Kind::Void) {
		const vm::Vector *vector = static_cast<const vm::Vector *>(p_object);
		if (vector->properties && vector->properties->size()) {
			*r_error = vm::ARRAY_PROPERTIES;
			return false;
		}
		Array array;
		array.resize(static_cast<int64_t>(vector->length));
		Variant *const places = places_of(array);
		const vm::Slot *const slots = vector->narrow ? nullptr : vm::slots_of(vector);
		for (uint32_t i = 0; i < vector->length; i++) {
			// An object, typed or untyped, goes this same way, so that what it can't be is an error here.
			const vm::Slot at = slots ? slots[i] : vm::place(vector, i);
			const vm::Slot held = element == ir::Kind::Any ? vm::any::filled(at) : at;
			const bool untyped_object = element == ir::Kind::Any && vm::any::tag(held) == vm::AnyTag::Object;
			const vm::Object *object = element == ir::Kind::Obj ? static_cast<const vm::Object *>(held.p)
					: untyped_object							? static_cast<const vm::Object *>(vm::any::pointer(held))
																: nullptr;
			if (element == ir::Kind::Obj || untyped_object) {
				if (!object_element(object, &places[i], r_error)) {
					return false;
				}
			} else if (bare != ir::Bare::None) {
				places[i] = bare_variant(bare, held);
			} else {
				place(&places[i], element, held);
			}
		}
		*r_out = array;
		return true;
	}
	if (!as_godot_object(p_object, r_out)) {
		*r_error = "a Vector holds an object of '" + module.classes[p_object->klass].name + "', which extends none of Godot's classes: Godot can't hold it";
		return false;
	}
	return true;
}

namespace {

// A packed array of whole numbers: each element must be one, and within what the array's elements hold.
template <typename Packed>
bool whole_numbers(ir::Kind p_kind, const vm::Vector *p_vector, int64_t p_least, int64_t p_most, std::string_view p_holds, Variant *r_made,
		std::string *r_error) {
	const size_t p_count = p_vector->length;
	Packed packed;
	packed.resize(static_cast<int64_t>(p_count));
	// A vector of ints is what a packed array of 32-bit integers holds: its places are copied in one go.
	if (p_kind == ir::Kind::I32 && p_vector->narrow && sizeof(packed[0]) == sizeof(int32_t) && p_least == INT32_MIN) {
		if (p_count) {
			std::memcpy(static_cast<void *>(packed.ptrw()), static_cast<const void *>(vm::numbers_of(p_vector)), p_count * sizeof(int32_t));
		}
		*r_made = packed;
		return true;
	}
	const vm::Slot *const slots = p_vector->narrow ? nullptr : vm::slots_of(p_vector);
	for (size_t i = 0; i < p_count; i++) {
		const Variant value = to_variant(p_kind, slots ? slots[i] : vm::place(p_vector, i));
		if (value.get_type() != Variant::INT) {
			*r_error = "element " + std::to_string(i) + " of the Vector is not a whole number, and Godot takes " + std::string(p_holds) + " here";
			return false;
		}
		const int64_t number = value;
		if (number < p_least || number > p_most) {
			*r_error = "RangeError: " + std::to_string(number) + " does not fit " + std::string(p_holds) + ".";
			return false;
		}
		packed[static_cast<int64_t>(i)] = static_cast<decltype(packed[0] + 0)>(number);
	}
	*r_made = packed;
	return true;
}

// A packed array whose elements are of one of Godot's types (or, for numbers, of either number type).
template <typename Packed, typename Element>
bool values_of(ir::Kind p_kind, const vm::Vector *p_vector, Variant::Type p_type, Variant::Type p_also, std::string_view p_holds, Variant *r_made,
		std::string *r_error) {
	const size_t p_count = p_vector->length;
	Packed packed;
	packed.resize(static_cast<int64_t>(p_count));
	const vm::Slot *const slots = p_vector->narrow ? nullptr : vm::slots_of(p_vector);
	for (size_t i = 0; i < p_count; i++) {
		const Variant value = to_variant(p_kind, slots ? slots[i] : vm::place(p_vector, i));
		const bool missing_text = p_type == Variant::STRING && p_kind == ir::Kind::Str && value.get_type() == Variant::NIL;
		if (value.get_type() != p_type && value.get_type() != p_also && !missing_text) {
			*r_error = "element " + std::to_string(i) + " of the Vector is " + (value.get_type() == Variant::NIL ? std::string("null") : std::string("of another type")) +
					", and Godot takes " + std::string(p_holds) + " here";
			return false;
		}
		// A string that is missing is Godot's empty one: its packed strings have no null.
		packed[static_cast<int64_t>(i)] = missing_text ? Element() : static_cast<Element>(value);
	}
	*r_made = packed;
	return true;
}

// A vector whose places hold one of Godot's values whole: the places themselves are a packed array of
// that type's elements, copied in one go; into an Array, each as a value of its own. Any other packed
// array holds elements of another type, which the checker refuses before a program runs.
bool bare_collection(std::string_view p_type, ir::Bare p_bare, const vm::Vector *p_vector, Variant *r_made, std::string *r_error) {
	const vm::Slot *const p_elements = vm::slots_of(p_vector); // A place that holds a value whole is a slot.
	const size_t p_count = p_vector->length;
	if (p_type == "PackedVector2Array" && p_bare == ir::Bare::Vector2) {
		PackedVector2Array packed;
		packed.resize(static_cast<int64_t>(p_count));
		if (p_count) {
			std::memcpy(static_cast<void *>(packed.ptrw()), static_cast<const void *>(p_elements), p_count * sizeof(Vector2));
		}
		*r_made = packed;
		return true;
	}
	if (p_type.substr(0, 6) == "Packed") {
		*r_error = "the Vector holds " + std::string(ir::bare_name(p_bare)) + "s, and Godot takes a " + std::string(p_type) + " here";
		return false;
	}
	Array array;
	array.resize(static_cast<int64_t>(p_count));
	Variant *const places = places_of(array);
	for (size_t i = 0; i < p_count; i++) {
		places[i] = bare_variant(p_bare, p_elements[i]);
	}
	*r_made = array;
	return true;
}

} // namespace

void *GodotHost::collection(std::string_view p_type, const ir::Class &p_class, const vm::Vector *p_vector, std::string *r_error) {
	const ir::Kind p_kind = p_class.element;
	Variant made;
	bool ok = true;
	if (p_class.element_bare != ir::Bare::None) {
		ok = bare_collection(p_type, p_class.element_bare, p_vector, &made, r_error);
	} else if (p_type == "PackedByteArray") {
		ok = whole_numbers<PackedByteArray>(p_kind, p_vector, 0, 255, "a byte", &made, r_error);
	} else if (p_type == "PackedInt32Array") {
		ok = whole_numbers<PackedInt32Array>(p_kind, p_vector, INT32_MIN, INT32_MAX, "a 32-bit integer", &made, r_error);
	} else if (p_type == "PackedInt64Array") {
		ok = whole_numbers<PackedInt64Array>(p_kind, p_vector, INT64_MIN, INT64_MAX, "a 64-bit integer", &made, r_error);
	} else if (p_type == "PackedFloat32Array" && p_kind == ir::Kind::F64 && !p_vector->narrow) {
		PackedFloat32Array packed;
		if (p_vector->length) {
			if (packed.resize(p_vector->length) != OK) {
				*r_error = "could not allocate a PackedFloat32Array";
				return nullptr;
			}
			float *const elements = packed.ptrw();
			const vm::Slot *const slots = vm::slots_of(p_vector);
			for (uint32_t i = 0; i < p_vector->length; i++) {
				elements[i] = static_cast<float>(slots[i].d);
			}
		}
		made = packed;
	} else if (p_type == "PackedFloat32Array") {
		ok = values_of<PackedFloat32Array, double>(p_kind, p_vector, Variant::FLOAT, Variant::INT, "numbers", &made, r_error);
	} else if (p_type == "PackedFloat64Array") {
		ok = values_of<PackedFloat64Array, double>(p_kind, p_vector, Variant::FLOAT, Variant::INT, "numbers", &made, r_error);
	} else if (p_type == "PackedStringArray") {
		ok = values_of<PackedStringArray, String>(p_kind, p_vector, Variant::STRING, Variant::STRING, "strings", &made, r_error);
	} else if (p_type == "PackedVector2Array") {
		ok = values_of<PackedVector2Array, Vector2>(p_kind, p_vector, Variant::VECTOR2, Variant::VECTOR2, "Vector2s", &made, r_error);
	} else if (p_type == "PackedVector3Array") {
		ok = values_of<PackedVector3Array, Vector3>(p_kind, p_vector, Variant::VECTOR3, Variant::VECTOR3, "Vector3s", &made, r_error);
	} else if (p_type == "PackedVector4Array") {
		ok = values_of<PackedVector4Array, Vector4>(p_kind, p_vector, Variant::VECTOR4, Variant::VECTOR4, "Vector4s", &made, r_error);
	} else if (p_type == "PackedColorArray") {
		ok = values_of<PackedColorArray, Color>(p_kind, p_vector, Variant::COLOR, Variant::COLOR, "Colors", &made, r_error);
	} else if (p_kind == ir::Kind::Obj || p_kind == ir::Kind::Any) {
		ok = object_element(p_vector, &made, r_error);
	} else {
		// An Array, or a place that takes anything: each element as Godot holds a value of its kind.
		Array array;
		array.resize(static_cast<int64_t>(p_vector->length));
		Variant *const places = places_of(array);
		const vm::Slot *const slots = p_vector->narrow ? nullptr : vm::slots_of(p_vector);
		for (uint32_t i = 0; i < p_vector->length && ok; i++) {
			const vm::Slot held = slots ? slots[i] : vm::place(p_vector, i);
			place(&places[i], p_kind, held);
		}
		made = array;
	}
	return ok ? make_handle(made) : nullptr;
}

} // namespace kas3
