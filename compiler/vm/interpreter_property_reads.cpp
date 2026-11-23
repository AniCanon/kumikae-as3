// Properties read by name as the program runs (holder[key] as an untyped value), and whether a holder has
// one (`in` and hasOwnProperty()).
#include "engine_named.h"
#include "interpreter_internal.h"

#include "table.h"

#include <array>

namespace kas3 {
namespace vm {

using ir::Kind;
using ir::NONE;

namespace {

constexpr std::array<std::string_view, 7> OBJECT_METHODS = { "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable", "setPropertyIsEnumerable",
	"toLocaleString", "toString", "valueOf" };
constexpr std::array<std::string_view, 3> NUMBER_METHODS = { "toFixed", "toExponential", "toPrecision" };

template <size_t Count>
bool listed(const std::array<std::string_view, Count> &p_names, std::string_view p_name) {
	for (std::string_view each : p_names) {
		if (each == p_name) {
			return true;
		}
	}
	return false;
}

} // namespace

// holder[key] as an untyped value with a reference of its own. False if it raised: a name a sealed object
// does not have is AS3's ReferenceError #1069; one the interpreter can't reach yet says so.
bool Interpreter::get_property(Slot p_holder, const PropertyKey &p_key, Slot *r_value) {
	*r_value = any::undefined();
	Object *object = property_object(host_.handles, p_holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	if (klass && klass->table) {
		if (const std::optional<uint32_t> at = entry_of(entries_of(object), p_key)) {
			*r_value = share(Kind::Any, entries_of(object)->value_at(*at));
			return true;
		}
		if (listed(OBJECT_METHODS, p_key.name())) {
			return method_value(p_holder, p_key.name(), r_value);
		}
		return true; // Nothing there: undefined.
	}
	if (klass && klass->element != Kind::Void) {
		return vector_property(static_cast<Vector *>(object), p_key, p_holder, r_value);
	}
	if (klass) {
		return sealed_property(object, p_key, p_holder, r_value);
	}
	return primitive_property(p_holder, p_key, r_value);
}

// An element of an Array or a Vector held untyped, its length, or another property of an Array.
bool Interpreter::vector_property(Vector *p_vector, const PropertyKey &p_key, Slot p_holder, Slot *r_value) {
	const ir::Class &klass = module_.classes[p_vector->klass];
	if (p_key.index && *p_key.index < p_vector->length) {
		return element_to_any(library_, klass, place(p_vector, *p_key.index), r_value);
	}
	if (p_key.index && !klass.array) {
		raise_range(*p_key.index, p_vector->length);
		return false;
	}
	if (p_key.name() == "length") {
		Slot length;
		set_uint32(length, p_vector->length);
		*r_value = to_any(Kind::U32, length);
		return true;
	}
	if (const std::optional<uint32_t> at = entry_of(p_vector->properties, p_key); at && klass.array) {
		*r_value = share(Kind::Any, p_vector->properties->value_at(*at));
		return true;
	}
	if (p_key.name() == "fixed" && !klass.array && !klass.fixed_array()) {
		Slot fixed;
		fixed.bits = 0;
		fixed.b = p_vector->fixed;
		*r_value = to_any(Kind::Bool, fixed);
		return true;
	}
	if (library_name(p_holder, p_key.name()) || listed(OBJECT_METHODS, p_key.name())) {
		return method_value(p_holder, p_key.name(), r_value);
	}
	if (klass.array) {
		return true; // Nothing there: undefined.
	}
	raise_not_found(p_key, p_holder);
	return false;
}

// A property of a sealed object: one of RegExp's or Date's, a public field, a method as a function value,
// or what a getter gives.
bool Interpreter::sealed_property(Object *p_object, const PropertyKey &p_key, Slot p_holder, Slot *r_value) {
	const ir::Class &klass = module_.classes[p_object->klass];
	if (const std::optional<bool> done = static_get(p_object, p_key, p_holder, r_value)) {
		return *done;
	}
	if (const std::optional<bool> done = library_get(p_object, p_key.name(), r_value)) {
		return *done;
	}
	if (p_object->klass == module_.function_class && p_key.name() == "length") {
		*r_value = any::integer(function_length(p_object));
		return true;
	}
	if (const std::optional<uint32_t> field = klass.field(p_key.name(), true)) {
		*r_value = to_any(klass.fields[*field].kind, vm::field(klass, p_object, *field));
		return true;
	}
	if (const std::optional<uint32_t> slot = named_method(klass, p_key.name())) {
		Object *made = make_function(klass.methods[*slot], p_object);
		*r_value = made ? any::object(made) : any::undefined();
		return made != nullptr;
	}
	if (const std::optional<uint32_t> getter = named_method(klass, p_key.name(), "get ")) {
		return call_getter(p_object, klass.methods[*getter], r_value);
	}
	if (named_method(klass, p_key.name(), "set ")) {
		raise(module_.reference_error_class != NONE ? module_.reference_error_class : module_.error_class,
				"Error #1077: Illegal read of write-only property " + std::string(p_key.name()) + " on " + holder_name(any::object(p_object)) + ".");
		return false;
	}
	if (!klass.native_base.empty()) {
		// A script object attached to one of Godot's objects: what Godot's object has, after its own.
		if (const std::optional<bool> done = EngineNamed(*this).get(p_holder, p_key, r_value)) {
			return *done;
		}
	}
	if (library_name(p_holder, p_key.name()) || listed(OBJECT_METHODS, p_key.name())) {
		return method_value(p_holder, p_key.name(), r_value);
	}
	const bool library_error = klass.built_in && module_.error_class != NONE && is_class(p_object, module_.error_class);
	if (p_object->klass == module_.regexp_class || p_object->klass == module_.date_class || p_object->klass == module_.function_class || library_error) {
		return true; // Dynamic in AS3, and nothing can be added to one here: undefined.
	}
	if (klass.built_in && p_object->klass != module_.class_class) {
		raise_unsupported("reading '" + std::string(p_key.name()) + "' of " + holder_name(p_holder) + " by its name");
		return false;
	}
	raise_not_found(p_key, p_holder);
	return false;
}

// A public getter: called, on the object's class's own (it may be overridden).
bool Interpreter::call_getter(Object *p_object, uint32_t p_function, Slot *r_value) {
	Slot self;
	self.bits = 0;
	self.p = p_object;
	Slot result;
	result.bits = 0;
	retain(p_object); // Held while its getter runs.
	const bool ok = call_within(p_function, &self, 1, &result);
	release_object(p_object);
	if (ok) {
		const Kind kind = module_.functions[p_function].result;
		*r_value = to_any(kind, result);
		drop(kind, result);
	}
	return ok;
}

// A property of a String, a number or a Boolean held untyped; null and undefined have none.
bool Interpreter::primitive_property(Slot p_holder, const PropertyKey &p_key, Slot *r_value) {
	const AnyTag tag = any::tag(p_holder);
	if (tag == AnyTag::Null || tag == AnyTag::Undefined) {
		any_null_check(p_holder);
		return false;
	}
	if (any::is_engine(p_holder)) {
		// One of Godot's values or objects: its member by the AS3 name, or one of Object's own methods.
		if (const std::optional<bool> done = EngineNamed(*this).get(p_holder, p_key, r_value)) {
			return *done;
		}
		if (listed(OBJECT_METHODS, p_key.name())) {
			return method_value(p_holder, p_key.name(), r_value);
		}
		raise_not_found(p_key, p_holder);
		return false;
	}
	if (tag == AnyTag::String && p_key.name() == "length") {
		*r_value = any::integer(utf16_length(static_cast<const Str *>(any::pointer(p_holder))->view()));
		return true;
	}
	const bool number = tag == AnyTag::Int || tag == AnyTag::Number || tag == AnyTag::Wide;
	if (listed(OBJECT_METHODS, p_key.name()) || library_name(p_holder, p_key.name()) || (number && listed(NUMBER_METHODS, p_key.name()))) {
		return method_value(p_holder, p_key.name(), r_value);
	}
	raise_not_found(p_key, p_holder);
	return false;
}

// key in holder (p_prototype: what its prototype has counts), or holder.hasOwnProperty(key).
bool Interpreter::has_property(Slot p_holder, const PropertyKey &p_key, bool p_prototype, bool *r_has) {
	Object *object = property_object(host_.handles, p_holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	const AnyTag tag = any::tag(p_holder);
	if (!klass && (tag == AnyTag::Null || tag == AnyTag::Undefined)) {
		any_null_check(p_holder);
		return false;
	}
	bool has = (p_prototype && listed(OBJECT_METHODS, p_key.name())) || static_has(object, p_key);
	if (klass && klass->table) {
		has = has || entry_of(entries_of(object), p_key).has_value();
	} else if (klass && klass->element != Kind::Void) {
		const Vector *vector = static_cast<const Vector *>(object);
		const bool element = p_key.index && *p_key.index < vector->length &&
				(!klass->array || !any::is_hole(slots_of(vector)[*p_key.index]));
		has = has || element || p_key.name() == "length" || (!klass->array && p_key.name() == "fixed" && !klass->fixed_array()) ||
				(klass->array && entry_of(vector->properties, p_key).has_value()) || (p_prototype && library_name(p_holder, p_key.name()));
	} else if (klass) {
		// A getter or a setter is a property of the object as a field is (avmplus looks through the traits).
		has = has || klass->field(p_key.name(), true) || named_method(*klass, p_key.name()) || named_method(*klass, p_key.name(), "get ") ||
				named_method(*klass, p_key.name(), "set ") || library_has(object, p_key.name()) ||
				(p_prototype && library_name(p_holder, p_key.name()));
	} else if (tag == AnyTag::String) {
		has = has || p_key.name() == "length" || (p_prototype && library_name(p_holder, p_key.name()));
	} else if (tag != AnyTag::Boolean) {
		has = has || (p_prototype && listed(NUMBER_METHODS, p_key.name()));
	}
	if ((!klass && any::is_engine(p_holder)) || (klass && !klass->native_base.empty())) {
		has = has || EngineNamed(*this).has(p_holder, p_key); // What Godot's value or object has by that name.
	}
	*r_has = has;
	return true;
}

} // namespace vm
} // namespace kas3
