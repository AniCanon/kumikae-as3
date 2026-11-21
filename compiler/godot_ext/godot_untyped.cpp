// AS3's untyped values as Godot's Variants, and back.
#include "godot_host.h"

#include "any.h"
#include "handles.h"

#include <godot_cpp/core/object_id.hpp>

using namespace godot;

namespace kas3 {

// An untyped value as Godot holds it: an `int` as Godot's integer, a Number as its float, so that a value
// that came from Godot goes back as the type it was. A script object is refused before this is reached.
Variant untyped_to_variant(const vm::Slot &p_slot) {
	switch (vm::any::tag(p_slot)) {
		case vm::AnyTag::Boolean:
			return Variant(vm::any::to_bool_payload(p_slot));
		case vm::AnyTag::Number:
			return Variant(vm::any::to_number_payload(p_slot));
		case vm::AnyTag::Int:
			return Variant((int64_t)vm::any::to_int_payload(p_slot));
		case vm::AnyTag::Wide:
			return Variant(static_cast<const vm::Wide *>(vm::any::pointer(p_slot))->value);
		case vm::AnyTag::String:
			return Variant(String::utf8(static_cast<const vm::Str *>(vm::any::pointer(p_slot))->data(), static_cast<int64_t>(static_cast<const vm::Str *>(vm::any::pointer(p_slot))->size)));
		case vm::AnyTag::Value:
		case vm::AnyTag::Godot: {
			vm::Slot handle;
			handle.bits = 0;
			handle.p = vm::any::pointer(p_slot);
			return to_variant(ir::Kind::Ref, handle);
		}
		case vm::AnyTag::Object: {
			// A script object, an Array or a vector: as Godot holds each; null for one Godot can't hold.
			Variant made;
			std::string error;
			return object_element(static_cast<const vm::Object *>(vm::any::pointer(p_slot)), &made, &error) ? made : Variant();
		}
		default:
			return Variant();
	}
}

// What Godot gives, as an untyped value: an integer that fits an `int` as one, a larger one as an Int64.
std::optional<vm::Slot> untyped_from_variant(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			return vm::any::null();
		case Variant::BOOL:
			return vm::any::boolean((bool)p_value);
		case Variant::INT: {
			const int64_t value = p_value;
			if (value >= INT32_MIN && value <= INT32_MAX) {
				return vm::any::integer((int32_t)value);
			}
			vm::Wide *wide = vm::new_wide(value);
			if (!wide) {
				return std::nullopt;
			}
			return vm::any::wide(wide);
		}
		case Variant::FLOAT:
			return vm::any::number((double)p_value);
		case Variant::STRING:
		case Variant::STRING_NAME:
		case Variant::NODE_PATH: {
			// AS3 has one kind of text: Godot's names and paths are Strings too. (Godot takes a String
			// back wherever it takes one of them.)
			CharString utf8 = String(p_value).utf8();
			vm::Str *text = vm::new_string(std::string_view(utf8.get_data(), (size_t)utf8.length()));
			if (!text) {
				return std::nullopt;
			}
			return vm::any::string(text);
		}
		case Variant::OBJECT:
			if (((ObjectID)p_value).is_null() || !p_value.get_validated_object()) {
				return vm::any::null();
			}
			return vm::any::godot_object(make_handle(p_value));
		default:
			return vm::any::godot_value(make_handle(p_value));
	}
}

} // namespace kas3
