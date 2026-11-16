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

#include <limits>

using namespace godot;

namespace kas3 {

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
	if (element != ir::Kind::Void) {
		const vm::Vector *vector = static_cast<const vm::Vector *>(p_object);
		if (vector->properties && vector->properties->size()) {
			*r_error = vm::ARRAY_PROPERTIES;
			return false;
		}
		Array array;
		array.resize(static_cast<int64_t>(vector->length));
		for (uint32_t i = 0; i < vector->length; i++) {
			// An object, typed or untyped, goes this same way, so that what it can't be is an error here.
			const vm::Slot held = vector->data[i];
			const bool untyped_object = element == ir::Kind::Any && vm::any::tag(held) == vm::AnyTag::Object;
			const vm::Object *object = element == ir::Kind::Obj ? static_cast<const vm::Object *>(held.p)
					: untyped_object							? static_cast<const vm::Object *>(vm::any::pointer(held))
																: nullptr;
			Variant value;
			if ((element == ir::Kind::Obj || untyped_object) && !object_element(object, &value, r_error)) {
				return false;
			}
			array[static_cast<int64_t>(i)] = element == ir::Kind::Obj || untyped_object ? value : to_variant(element, held);
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
bool whole_numbers(ir::Kind p_kind, const vm::Slot *p_elements, size_t p_count, int64_t p_least, int64_t p_most, std::string_view p_holds, Variant *r_made,
		std::string *r_error) {
	Packed packed;
	packed.resize(static_cast<int64_t>(p_count));
	for (size_t i = 0; i < p_count; i++) {
		const Variant value = to_variant(p_kind, p_elements[i]);
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
bool values_of(ir::Kind p_kind, const vm::Slot *p_elements, size_t p_count, Variant::Type p_type, Variant::Type p_also, std::string_view p_holds, Variant *r_made,
		std::string *r_error) {
	Packed packed;
	packed.resize(static_cast<int64_t>(p_count));
	for (size_t i = 0; i < p_count; i++) {
		const Variant value = to_variant(p_kind, p_elements[i]);
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

} // namespace

void *GodotHost::collection(std::string_view p_type, ir::Kind p_kind, const vm::Slot *p_elements, size_t p_count, std::string *r_error) {
	Variant made;
	bool ok = true;
	if (p_type == "PackedByteArray") {
		ok = whole_numbers<PackedByteArray>(p_kind, p_elements, p_count, 0, 255, "a byte", &made, r_error);
	} else if (p_type == "PackedInt32Array") {
		ok = whole_numbers<PackedInt32Array>(p_kind, p_elements, p_count, INT32_MIN, INT32_MAX, "a 32-bit integer", &made, r_error);
	} else if (p_type == "PackedInt64Array") {
		ok = whole_numbers<PackedInt64Array>(p_kind, p_elements, p_count, INT64_MIN, INT64_MAX, "a 64-bit integer", &made, r_error);
	} else if (p_type == "PackedFloat32Array") {
		ok = values_of<PackedFloat32Array, double>(p_kind, p_elements, p_count, Variant::FLOAT, Variant::INT, "numbers", &made, r_error);
	} else if (p_type == "PackedFloat64Array") {
		ok = values_of<PackedFloat64Array, double>(p_kind, p_elements, p_count, Variant::FLOAT, Variant::INT, "numbers", &made, r_error);
	} else if (p_type == "PackedStringArray") {
		ok = values_of<PackedStringArray, String>(p_kind, p_elements, p_count, Variant::STRING, Variant::STRING, "strings", &made, r_error);
	} else if (p_type == "PackedVector2Array") {
		ok = values_of<PackedVector2Array, Vector2>(p_kind, p_elements, p_count, Variant::VECTOR2, Variant::VECTOR2, "Vector2s", &made, r_error);
	} else if (p_type == "PackedVector3Array") {
		ok = values_of<PackedVector3Array, Vector3>(p_kind, p_elements, p_count, Variant::VECTOR3, Variant::VECTOR3, "Vector3s", &made, r_error);
	} else if (p_type == "PackedVector4Array") {
		ok = values_of<PackedVector4Array, Vector4>(p_kind, p_elements, p_count, Variant::VECTOR4, Variant::VECTOR4, "Vector4s", &made, r_error);
	} else if (p_type == "PackedColorArray") {
		ok = values_of<PackedColorArray, Color>(p_kind, p_elements, p_count, Variant::COLOR, Variant::COLOR, "Colors", &made, r_error);
	} else {
		// An Array, or a place that takes anything: each element as Godot holds a value of its kind.
		Array array;
		array.resize(static_cast<int64_t>(p_count));
		for (size_t i = 0; i < p_count && ok; i++) {
			Variant value;
			ok = p_kind != ir::Kind::Obj || object_element(static_cast<const vm::Object *>(p_elements[i].p), &value, r_error);
			array[static_cast<int64_t>(i)] = p_kind == ir::Kind::Obj ? value : to_variant(p_kind, p_elements[i]);
		}
		made = array;
	}
	return ok ? make_handle(made) : nullptr;
}

} // namespace kas3
