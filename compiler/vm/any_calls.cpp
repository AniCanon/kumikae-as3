// The untyped value made of a typed one and stored into a typed place, and `is`, as functions of plain
// values (vm/library.h).
#include "library.h"

#include "any.h"
#include "numeric.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kas3 {
namespace vm {

using ir::Kind;
using ir::NONE;

namespace {

const char *tag_name(AnyTag p_tag) {
	switch (p_tag) {
		case AnyTag::Undefined:
			return "undefined";
		case AnyTag::Null:
			return "null";
		case AnyTag::Boolean:
			return "Boolean";
		case AnyTag::Number:
		case AnyTag::Int:
		case AnyTag::Wide:
			return "Number";
		case AnyTag::String:
			return "String";
		default:
			return "Object";
	}
}

bool wide_from_any(Library &p_library, Slot p_value, Slot *r_out);
bool text_from_any(Library &p_library, Slot p_value, Slot *r_out);
bool object_from_any(Library &p_library, Slot p_value, uint32_t p_check, Slot *r_out);
bool engine_from_any(Library &p_library, Slot p_value, Kind p_kind, uint32_t p_check, Slot *r_out);

} // namespace

bool is_class(const ir::Module &p_module, const Object *p_object, uint32_t p_class) {
	if (!p_object) {
		return false;
	}
	if (p_module.classes[p_class].is_interface) {
		const std::vector<uint32_t> &implemented = p_module.classes[p_object->klass].interfaces;
		return std::find(implemented.begin(), implemented.end(), p_class) != implemented.end();
	}
	for (uint32_t at = p_object->klass; at != NONE; at = p_module.classes[at].base) {
		if (at == p_class) {
			return true;
		}
	}
	return false;
}

// The value of a register of any kind as an untyped value, with a reference of its own.
Slot to_any(Library &p_library, Kind p_kind, Slot p_value) {
	switch (p_kind) {
		case Kind::I32:
			return any::integer(p_value.i);
		case Kind::U8:
		case Kind::U32:
			return p_value.u <= static_cast<uint32_t>(INT32_MAX) ? any::integer(static_cast<int32_t>(p_value.u)) : any::number(p_value.u);
		case Kind::F32:
		case Kind::F64:
			return any_number(p_library, p_value.d);
		case Kind::Bool:
			return any::boolean(p_value.b);
		case Kind::I64: {
			Wide *wide = new_wide(p_value.l);
			if (!wide) {
				p_library.runner.out_of_memory();
				return any::undefined();
			}
			return any::wide(wide);
		}
		case Kind::Str:
			retain(static_cast<Str *>(p_value.p));
			return any::string(static_cast<Str *>(p_value.p));
		case Kind::Obj:
			retain(static_cast<Object *>(p_value.p));
			return any::object(static_cast<Object *>(p_value.p));
		case Kind::Val:
			return p_value.p ? any::godot_value(p_library.heap.handles.copy(p_value.p)) : any::null();
		case Kind::Ref:
			return p_value.p ? any::godot_object(p_library.heap.handles.copy(p_value.p)) : any::null();
		case Kind::Any:
			return p_library.heap.share(Kind::Any, p_value);
		default:
			return any::undefined();
	}
}

// A store of an untyped value into a register of another kind: AS3's coercion. False if it raised.
bool from_any(Library &p_library, Slot p_value, Kind p_kind, uint32_t p_check, Slot *r_out) {
	r_out->bits = 0;
	const bool numeric = p_kind == Kind::U8 || p_kind == Kind::I32 || p_kind == Kind::U32 || ir::is_floating(p_kind) || p_kind == Kind::I64;
	if (numeric && (any::tag(p_value) == AnyTag::Object || any::is_engine(p_value))) {
		// An object as a number: its text, then that as a number (ECMAScript's ToPrimitive).
		Slot primitive;
		if (!any_primitive(p_library, p_value, &primitive, true)) {
			return false;
		}
		const bool done = from_any(p_library, primitive, p_kind, p_check, r_out);
		p_library.heap.drop_any(primitive);
		return done;
	}
	if (p_kind == Kind::Bool && any::is_engine(p_value) && !p_library.heap.handles.present(any::pointer(p_value))) {
		return true; // A Godot object that has been freed is null, so false.
	}
	const AnyTag tag = any::tag(p_value);
	switch (p_kind) {
		// A large integer wraps (the owner's ruling): an Int64 by its own low bits, not a rounded Number's.
		case Kind::I32:
			r_out->i = tag == AnyTag::Wide ? (int32_t)(uint32_t)(uint64_t) static_cast<Wide *>(any::pointer(p_value))->value
										   : to_int32(any::to_number(p_value));
			return true;
		case Kind::U8:
			r_out->u = tag == AnyTag::Wide ? static_cast<uint8_t>(static_cast<Wide *>(any::pointer(p_value))->value) : static_cast<uint8_t>(to_uint32(any::to_number(p_value)));
			return true;
		case Kind::U32:
			r_out->u = tag == AnyTag::Wide ? (uint32_t)(uint64_t) static_cast<Wide *>(any::pointer(p_value))->value
										   : to_uint32(any::to_number(p_value));
			return true;
		case Kind::F32:
			r_out->d = any::to_float(p_value);
			return true;
		case Kind::F64:
			r_out->d = any::to_number(p_value);
			return true;
		case Kind::Bool:
			r_out->b = any::to_boolean(p_value);
			return true;
		case Kind::I64:
			return wide_from_any(p_library, p_value, r_out);
		case Kind::Str:
			return text_from_any(p_library, p_value, r_out);
		case Kind::Obj:
			return object_from_any(p_library, p_value, p_check, r_out);
		case Kind::Val:
		case Kind::Ref:
			return engine_from_any(p_library, p_value, p_kind, p_check, r_out);
		case Kind::Any:
			// Into an `Object` (any check given): undefined becomes null. Into a `*`: as it is.
			*r_out = p_check != NONE && tag == AnyTag::Undefined ? any::null() : p_library.heap.share(Kind::Any, p_value);
			return true;
		default:
			return true;
	}
}

namespace {

// An untyped value into an Int64: one held as an Int64 as it is, text parsed exactly when it holds one,
// and a Number only if it is a whole one an Int64 holds exactly.
bool wide_from_any(Library &p_library, Slot p_value, Slot *r_out) {
	if (any::tag(p_value) == AnyTag::Wide) {
		r_out->l = static_cast<Wide *>(any::pointer(p_value))->value;
		return true;
	}
	if (any::tag(p_value) == AnyTag::String) {
		const std::string_view text = static_cast<Str *>(any::pointer(p_value))->view();
		if (const std::optional<int64_t> exact = string_to_wide_exact(text)) {
			r_out->l = *exact;
			return true;
		}
		p_library.runner.raise(p_library.module().range_error_class, "\"" + std::string(text) + "\" does not fit an Int64.");
		return false;
	}
	const double number = any::to_number(p_value);
	const std::optional<int64_t> whole = number_to_wide(number);
	if (!whole || static_cast<double>(*whole) != number) {
		p_library.runner.raise(p_library.module().range_error_class, number_to_string(number) + " does not fit an Int64.");
		return false;
	}
	r_out->l = *whole;
	return true;
}

// An untyped value into a String: null and undefined are null, and anything else is its text.
bool text_from_any(Library &p_library, Slot p_value, Slot *r_out) {
	const AnyTag tag = any::tag(p_value);
	if (tag == AnyTag::Undefined || tag == AnyTag::Null) {
		return true;
	}
	if (tag == AnyTag::String) {
		retain(static_cast<Str *>(any::pointer(p_value)));
		r_out->p = any::pointer(p_value);
		return true;
	}
	std::string words;
	if (!any_text(p_library, p_value, &words)) {
		return false;
	}
	r_out->p = p_library.text(words);
	return r_out->p != nullptr;
}

// An untyped value into an object of the class p_check (any object for NONE).
bool object_from_any(Library &p_library, Slot p_value, uint32_t p_check, Slot *r_out) {
	const AnyTag tag = any::tag(p_value);
	const bool nothing = tag == AnyTag::Undefined || tag == AnyTag::Null;
	Object *object = tag == AnyTag::Object ? static_cast<Object *>(any::pointer(p_value)) : nullptr;
	if (nothing || (object && (p_check == NONE || is_class(p_library.module(), object, p_check)))) {
		retain(object);
		r_out->p = object;
		return true;
	}
	// One of Godot's objects that carries a script of the class, or of one that extends it: the script's own
	// object. One that has been freed is null, as everywhere.
	if (p_check != NONE && tag == AnyTag::Godot) {
		if (!p_library.heap.handles.present(any::pointer(p_value))) {
			return true;
		}
		if (Object *const script = p_library.heap.handles.script_of(any::pointer(p_value), p_check, true)) {
			r_out->p = script;
			return true;
		}
	}
	// One of Godot's Arrays where an Array is wanted, or of its Dictionaries where a Dictionary is: a new one
	// of what it holds.
	if (p_check != NONE && any::tag(p_value) == AnyTag::Value) {
		bool taken = false;
		const bool made = p_library.unmoved.object_of_engine(p_value, p_check, &taken, r_out);
		if (taken) {
			return made;
		}
	}
	return coercion_failed(p_library, p_value, Kind::Obj, p_check);
}

// An untyped value into one of Godot's values or objects, of the class named by p_check if it is not NONE.
bool engine_from_any(Library &p_library, Slot p_value, Kind p_kind, uint32_t p_check, Slot *r_out) {
	const AnyTag tag = any::tag(p_value);
	// A script object is the Godot object it is attached to, where one of Godot's objects is declared.
	void *const held = p_kind == Kind::Ref ? engine_object_in(p_library.module(), p_value) : tag == AnyTag::Value ? any::pointer(p_value) : nullptr;
	// One of Godot's objects that has been freed is null where an object is declared, as everywhere.
	const bool nothing = tag == AnyTag::Undefined || tag == AnyTag::Null ||
			(tag == AnyTag::Godot && p_kind == Kind::Ref && !p_library.heap.handles.present(any::pointer(p_value)));
	const bool fits = held && (p_check == NONE || p_library.heap.handles.is_class(held, p_library.module().engine_names[p_check].name));
	if (nothing || fits) {
		r_out->p = nothing ? nullptr : p_library.heap.handles.copy(held);
		return true;
	}
	return coercion_failed(p_library, p_value, p_kind, p_check);
}

} // namespace

void *engine_object_in(const ir::Module &p_module, Slot p_value) {
	const AnyTag tag = any::tag(p_value);
	if (tag == AnyTag::Godot) {
		return any::pointer(p_value);
	}
	if (tag != AnyTag::Object) {
		return nullptr;
	}
	Object *const object = static_cast<Object *>(any::pointer(p_value));
	const ir::Class &klass = p_module.classes[object->klass];
	const bool attached = !klass.native_base.empty() && !klass.fields.empty() && klass.fields[0].kind == Kind::Ref;
	return attached ? owner_field(object).p : nullptr;
}

// AS3's TypeError #1034, in Flash's words: the value as text where it has one, else its type, and the type
// it was to be. False.
bool coercion_failed(Library &p_library, Slot p_value, Kind p_kind, uint32_t p_check) {
	std::string wanted = ir::kind_name(p_kind);
	if (p_check != NONE) {
		wanted = p_kind == Kind::Obj ? p_library.module().classes[p_check].name : p_library.module().engine_names[p_check].name;
	}
	const std::optional<std::string> shown = any::text(p_value);
	p_library.runner.raise(p_library.module().type_error_class, "Error #1034: Type Coercion failed: cannot convert " +
			(shown ? *shown : std::string(tag_name(any::tag(p_value)))) + " to " + wanted + ".");
	return false;
}

// `is` on an untyped value: of a class, or a test of the value itself (5.0 is an int).
bool any_is(Library &p_library, Slot p_value, uint32_t p_type) {
	const AnyTag tag = any::tag(p_value);
	const bool numeric = tag == AnyTag::Number || tag == AnyTag::Int || tag == AnyTag::Wide;
	const double number = numeric ? any::to_number(p_value) : 0;
	switch (p_type) {
		case ir::ANY_IS_INT:
			return numeric && number == std::trunc(number) && number >= INT32_MIN && number <= INT32_MAX;
		case ir::ANY_IS_UINT:
			return numeric && number == std::trunc(number) && number >= 0 && number <= UINT32_MAX;
		case ir::ANY_IS_NUMBER:
			return numeric;
		case ir::ANY_IS_STRING:
			return tag == AnyTag::String;
		case ir::ANY_IS_BOOLEAN:
			return tag == AnyTag::Boolean;
		case ir::ANY_IS_OBJECT:
			return tag != AnyTag::Null && tag != AnyTag::Undefined;
		default:
			return (tag == AnyTag::Object && is_class(p_library.module(), static_cast<const Object *>(any::pointer(p_value)), p_type)) ||
					(tag == AnyTag::Value && p_library.unmoved.engine_is(p_value, p_type)) ||
					(tag == AnyTag::Godot && p_library.heap.handles.script_of(any::pointer(p_value), p_type, false));
	}
}

// A number as an untyped value, held as AVM2 holds one: a whole number as an `int` where it fits one, and
// as an Int64 up to 2^53, where a Number is still exact; anything else (a fraction, -0, NaN, the very
// large) as a Number. So `v - 1` with `v` from Godot's integer 5 goes back to Godot as the integer 4.
// Undefined, with the error raised, when there is no memory for an Int64.
Slot any_number(Library &p_library, double p_value) {
	constexpr double EXACT = 9007199254740992.0; // 2^53
	if (p_value != std::trunc(p_value) || std::fabs(p_value) > EXACT || (p_value == 0 && std::signbit(p_value))) {
		return any::number(p_value); // NaN fails the first test.
	}
	if (p_value >= INT32_MIN && p_value <= INT32_MAX) {
		return any::integer(static_cast<int32_t>(p_value));
	}
	return any_wide(p_library, static_cast<int64_t>(p_value));
}

Slot any_wide(Library &p_library, int64_t p_value) {
	if (p_value >= INT32_MIN && p_value <= INT32_MAX) {
		return any::integer(static_cast<int32_t>(p_value));
	}
	Wide *wide = new_wide(p_value);
	if (!wide) {
		p_library.runner.out_of_memory();
		return any::undefined();
	}
	return any::wide(wide);
}

} // namespace vm
} // namespace kas3
