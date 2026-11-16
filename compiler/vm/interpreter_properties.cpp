// Properties reached by name as the program runs: of an Object or a Dictionary, of an Array beyond its
// indices, and of a sealed object (its public fields and methods). What holds the property is always an
// untyped value here, with a reference of its own while the property is worked on.
#include "interpreter_internal.h"

#include "table.h"

#include <array>
#include <cmath>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

constexpr std::array<std::string_view, 7> OBJECT_METHODS = { "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable", "setPropertyIsEnumerable",
	"toLocaleString", "toString", "valueOf" };
constexpr std::array<std::string_view, 20> ARRAY_METHODS = { "concat", "every", "filter", "forEach", "indexOf", "join", "lastIndexOf", "map", "pop",
	"push", "reverse", "shift", "slice", "some", "sort", "sortOn", "splice", "unshift", "insertAt", "removeAt" };
constexpr std::array<std::string_view, 16> STRING_METHODS = { "charAt", "charCodeAt", "concat", "indexOf", "lastIndexOf", "localeCompare", "match",
	"replace", "search", "slice", "split", "substr", "substring", "toLowerCase", "toUpperCase", "fromCharCode" };
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

// "12" names element 12 of an Array; "012", "1e1" and "-1" name properties, as in ECMAScript.
std::optional<uint32_t> index_text(std::string_view p_text) {
	if (p_text.empty() || p_text.size() > 10 || (p_text.size() > 1 && p_text[0] == '0')) {
		return std::nullopt;
	}
	uint64_t value = 0;
	for (char c : p_text) {
		if (c < '0' || c > '9') {
			return std::nullopt;
		}
		value = value * 10 + static_cast<uint64_t>(c - '0');
	}
	return value < 4294967295ull ? std::optional<uint32_t>(static_cast<uint32_t>(value)) : std::nullopt;
}

Slot of_bool(bool p_value) {
	Slot out;
	out.bits = 0;
	out.b = p_value;
	return out;
}

std::optional<uint32_t> index_number(double p_value) {
	if (!(p_value >= 0 && p_value < 4294967295.0) || p_value != std::floor(p_value)) {
		return std::nullopt;
	}
	return static_cast<uint32_t>(p_value);
}

} // namespace

Object *Interpreter::held_object(Slot p_value) const {
	return any::tag(p_value) == AnyTag::Object ? static_cast<Object *>(any::pointer(p_value)) : nullptr;
}

// The name Flash's messages give what holds a property.
std::string Interpreter::holder_name(Slot p_holder) const {
	if (const Object *object = held_object(p_holder)) {
		const ir::Class &klass = module_.classes[object->klass];
		return klass.element != Kind::Void && !klass.array ? "__AS3__.vec." + klass.name : klass.name;
	}
	switch (any::tag(p_holder)) {
		case AnyTag::Int:
			return "int";
		case AnyTag::Number:
		case AnyTag::Wide:
			return "Number";
		case AnyTag::Boolean:
			return "Boolean";
		case AnyTag::String:
			return "String";
		default:
			return "Object";
	}
}

void Interpreter::raise_unsupported(const std::string &p_what) {
	raise(module_.error_class, p_what + " is not supported by the interpreter yet");
}

void Interpreter::raise_not_found(const PropertyKey &p_key, Slot p_holder) {
	raise(module_.reference_error_class != NONE ? module_.reference_error_class : module_.error_class,
			"Error #1069: Property " + p_key.text + " not found on " + holder_name(p_holder) + " and there is no default value.");
}

// The key as the holder keeps it: an element's index for an Array or a Vector, an object itself for a
// Dictionary, and otherwise the key's text (`o[1]`, `o[1.0]` and `o["1"]` are one property). Making an
// object's text may run the program. False if that raised.
bool Interpreter::property_key(Slot p_holder, Slot p_key, PropertyKey *r_key) {
	const Object *object = held_object(p_holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	const AnyTag tag = any::tag(p_key);
	r_key->text.clear();
	r_key->index.reset();
	r_key->object = nullptr;
	if (klass && klass->element != Kind::Void) {
		if (tag == AnyTag::Int || tag == AnyTag::Number || tag == AnyTag::Wide) {
			r_key->index = index_number(any::to_number(p_key));
		} else if (tag == AnyTag::String) {
			r_key->index = index_text(static_cast<const Str *>(any::pointer(p_key))->view());
		}
		if (r_key->index) {
			r_key->text = std::to_string(*r_key->index);
			return true;
		}
	}
	if (klass && klass->dictionary && tag == AnyTag::Object) {
		r_key->object = static_cast<Object *>(any::pointer(p_key));
		return true;
	}
	if (klass && klass->dictionary && any::is_engine(p_key)) {
		raise_unsupported("one of Godot's values or objects as the key of a Dictionary");
		return false;
	}
	if (tag == AnyTag::String) {
		r_key->text = static_cast<const Str *>(any::pointer(p_key))->view();
		return true;
	}
	return any_text(p_key, &r_key->text);
}

std::optional<uint32_t> Interpreter::entry_of(const Entries *p_entries, const PropertyKey &p_key) const {
	if (!p_entries) {
		return std::nullopt;
	}
	if (!p_key.object) {
		return p_entries->find_text(p_key.text);
	}
	if (const std::optional<uint32_t> found = p_entries->find_object(p_key.object)) {
		return found;
	}
	// A method taken from its object twice is two function values and one function, as `==` has it: as a
	// key, the one already there.
	for (size_t i = 0; p_key.object->klass == module_.function_class && i < p_entries->all().size(); i++) {
		const Object *key = held_object(p_entries->all()[i].key);
		if (key && same_function(key, p_key.object)) {
			return static_cast<uint32_t>(i);
		}
	}
	return std::nullopt;
}

// Sets a property of a table or of an Array's properties to a value (which is shared, not taken). False if
// memory ran out.
bool Interpreter::set_entry(Entries *p_entries, const PropertyKey &p_key, Slot p_value) {
	if (!p_entries) {
		out_of_memory();
		return false;
	}
	if (const std::optional<uint32_t> at = entry_of(p_entries, p_key)) {
		put(Kind::Any, p_entries->value_at(*at), share(Kind::Any, p_value));
		return true;
	}
	Slot key;
	if (p_key.object) {
		retain(p_key.object);
		key = any::object(p_key.object);
	} else {
		Str *made = text(p_key.text);
		if (!made) {
			return false;
		}
		key = any::string(made);
	}
	p_entries->add(key, share(Kind::Any, p_value));
	return true;
}

// array[n] for a number that is no index (-1, 1.5, NaN): the property named by its text, as in AS3, or
// undefined. Out of the loop's way: the elements' own path stays short.
void Interpreter::array_number_get(Vector *p_array, double p_number, Slot &r_out) {
	PropertyKey key;
	key.text = number_to_string(p_number);
	const std::optional<uint32_t> found = entry_of(p_array->properties, key);
	put(Kind::Any, r_out, found ? share(Kind::Any, p_array->properties->value_at(*found)) : any::undefined());
}

void Interpreter::array_number_set(Vector *p_array, double p_number, Slot p_value) {
	PropertyKey key;
	key.text = number_to_string(p_number);
	set_entry(properties_of(p_array), key, p_value);
}

// The public method of this name, or with `p_prefix` ("get ", "set ") the accessor: compared in place, so a
// by-name read builds no text.
std::optional<uint32_t> Interpreter::named_method(const ir::Class &p_class, std::string_view p_name, std::string_view p_prefix) const {
	for (const auto &[name, slot] : p_class.named_methods) {
		if (name.size() == p_prefix.size() + p_name.size() && name.compare(0, p_prefix.size(), p_prefix) == 0 &&
				name.compare(p_prefix.size(), std::string::npos, p_name) == 0) {
			return slot;
		}
	}
	return std::nullopt;
}

// holder[key] as an untyped value with a reference of its own. False if it raised: a name a sealed object
// does not have is AS3's ReferenceError #1069; one the interpreter can't reach yet says so.
bool Interpreter::get_property(Slot p_holder, const PropertyKey &p_key, Slot *r_value) {
	*r_value = any::undefined();
	Object *object = held_object(p_holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	if (klass && klass->table) {
		if (const std::optional<uint32_t> at = entry_of(entries_of(object), p_key)) {
			*r_value = share(Kind::Any, entries_of(object)->value_at(*at));
			return true;
		}
		if (listed(OBJECT_METHODS, p_key.text)) {
			raise_unsupported("reading '" + p_key.text + "' of an Object by its name");
			return false;
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
		*r_value = to_any(klass.element, p_vector->data[*p_key.index]);
		return true;
	}
	if (p_key.index && !klass.array) {
		raise_range(*p_key.index, p_vector->length);
		return false;
	}
	if (p_key.text == "length") {
		Slot length;
		set_uint32(length, p_vector->length);
		*r_value = to_any(Kind::U32, length);
		return true;
	}
	if (const std::optional<uint32_t> at = entry_of(p_vector->properties, p_key); at && klass.array) {
		*r_value = share(Kind::Any, p_vector->properties->value_at(*at));
		return true;
	}
	if (listed(ARRAY_METHODS, p_key.text) || listed(OBJECT_METHODS, p_key.text) || p_key.text == "fixed") {
		raise_unsupported("reading '" + p_key.text + "' of " + (klass.array ? "an Array" : "a Vector") + " by its name");
		return false;
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
	if (const std::optional<bool> done = library_get(p_object, p_key.text, r_value)) {
		return *done;
	}
	if (const std::optional<uint32_t> field = klass.field(p_key.text, true)) {
		*r_value = to_any(klass.fields[*field].kind, p_object->fields()[*field]);
		return true;
	}
	if (const std::optional<uint32_t> slot = named_method(klass, p_key.text)) {
		Object *made = make_function(klass.methods[*slot], p_object);
		*r_value = made ? any::object(made) : any::undefined();
		return made != nullptr;
	}
	if (const std::optional<uint32_t> getter = named_method(klass, p_key.text, "get ")) {
		return call_getter(p_object, klass.methods[*getter], r_value);
	}
	if (named_method(klass, p_key.text, "set ")) {
		raise(module_.reference_error_class != NONE ? module_.reference_error_class : module_.error_class,
				"Error #1077: Illegal read of write-only property " + p_key.text + " on " + holder_name(p_holder) + ".");
		return false;
	}
	if (klass.built_in || listed(OBJECT_METHODS, p_key.text)) {
		raise_unsupported("reading '" + p_key.text + "' of " + holder_name(p_holder) + " by its name");
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
		raise_unsupported("a property of one of Godot's values or objects held untyped, by its name");
		return false;
	}
	if (tag == AnyTag::String && p_key.text == "length") {
		*r_value = any::integer(utf16_length(static_cast<const Str *>(any::pointer(p_holder))->view()));
		return true;
	}
	const bool number = tag == AnyTag::Int || tag == AnyTag::Number || tag == AnyTag::Wide;
	if (listed(OBJECT_METHODS, p_key.text) || (tag == AnyTag::String && listed(STRING_METHODS, p_key.text)) ||
			(number && listed(NUMBER_METHODS, p_key.text))) {
		raise_unsupported("reading '" + p_key.text + "' of a " + holder_name(p_holder) + " by its name");
		return false;
	}
	raise_not_found(p_key, p_holder);
	return false;
}

// AS3's ReferenceError for a write a sealed object refuses: "Error #1056: Cannot create property x on C.".
bool Interpreter::refuse_write(const char *p_number, const char *p_words, std::string_view p_name, Slot p_holder) {
	raise(module_.reference_error_class != NONE ? module_.reference_error_class : module_.error_class,
			std::string("Error #") + p_number + ": " + p_words + std::string(p_name) + " on " + holder_name(p_holder) + ".");
	return false;
}

// holder[key] = value. A sealed object takes a value into a public field, coerced as a store into it is; a
// name it lacks is AS3's ReferenceError #1056, and one of its methods #1037.
bool Interpreter::set_property(Slot p_holder, const PropertyKey &p_key, Slot p_value) {
	Object *object = held_object(p_holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	const auto cannot = [&](const char *p_number, const char *p_words) { return refuse_write(p_number, p_words, p_key.text, p_holder); };
	if (klass && klass->table) {
		return set_entry(entries_of(object), p_key, p_value);
	}
	if (klass && klass->element != Kind::Void) {
		return set_element(static_cast<Vector *>(object), p_key, p_value);
	}
	if (klass) {
		return set_sealed_property(object, p_key, p_value, p_holder);
	}
	const AnyTag tag = any::tag(p_holder);
	if (tag == AnyTag::Null || tag == AnyTag::Undefined) {
		any_null_check(p_holder);
		return false;
	}
	if (any::is_engine(p_holder)) {
		raise_unsupported("a property of one of Godot's values or objects held untyped, by its name");
		return false;
	}
	return cannot("1056", "Cannot create property ");
}

// holder[key] = value on a sealed object: one of RegExp's or Date's properties, a public field, or a setter.
bool Interpreter::set_sealed_property(Object *p_object, const PropertyKey &p_key, Slot p_value, Slot p_holder) {
	const ir::Class &klass = module_.classes[p_object->klass];
	const auto cannot = [&](const char *p_number, const char *p_words) { return refuse_write(p_number, p_words, p_key.text, p_holder); };
	if (const std::optional<bool> done = library_set(p_object, p_key.text, p_value)) {
		return *done;
	}
	if (const std::optional<uint32_t> field = klass.field(p_key.text, true)) {
		const ir::Field &declared = klass.fields[*field];
		if (ir::is_engine(declared.kind)) {
			raise_unsupported("writing a field of one of Godot's types by its name");
			return false;
		}
		Slot stored;
		if (!from_any(p_value, declared.kind, declared.kind == Kind::Obj ? declared.klass : declared.starts_null ? 0 : NONE, &stored)) {
			return false;
		}
		put(declared.kind, p_object->fields()[*field], stored);
		return true;
	}
	if (named_method(klass, p_key.text)) {
		return cannot("1037", "Cannot assign to a method ");
	}
	if (const std::optional<uint32_t> setter = named_method(klass, p_key.text, "set ")) {
		return call_setter(p_object, klass.methods[*setter], p_value);
	}
	if (named_method(klass, p_key.text, "get ")) {
		return cannot("1074", "Illegal write to read-only property ");
	}
	if (klass.built_in) {
		raise_unsupported("adding a property to " + holder_name(p_holder));
		return false;
	}
	return cannot("1056", "Cannot create property ");
}

// A public setter: given the value as its parameter takes it.
bool Interpreter::call_setter(Object *p_object, uint32_t p_function, Slot p_value) {
	const ir::Function &callee = module_.functions[p_function];
	Slot both[2];
	both[0].bits = 0;
	both[0].p = p_object;
	if (!argument_as(callee, 1, p_value, &both[1])) {
		return false;
	}
	retain(p_object);
	const bool ok = call_within(p_function, both, 2, nullptr);
	release_object(p_object);
	drop(callee.registers[1], both[1]);
	return ok;
}

// An element of an Array or a Vector held untyped, its length, or another property of an Array.
bool Interpreter::set_element(Vector *p_vector, const PropertyKey &p_key, Slot p_value) {
	const ir::Class &klass = module_.classes[p_vector->klass];
	if (!p_key.index && p_key.text != "length") {
		if (klass.array) {
			return set_entry(properties_of(p_vector), p_key, p_value);
		}
		raise_unsupported("a property of a Vector other than its elements and length, by its name");
		return false;
	}
	if (klass.element == Kind::Obj || ir::is_engine(klass.element)) {
		raise_unsupported("writing an element of a Vector of objects held untyped");
		return false;
	}
	Slot value;
	if (!from_any(p_value, p_key.index ? klass.element : Kind::F64, NONE, &value)) {
		return false;
	}
	if (!p_key.index) {
		const std::optional<uint32_t> length = index_number(value.d);
		if (!length) {
			raise(module_.range_error_class, "Error #1005: Array index is not a positive integer (" + number_to_string(value.d) + ").");
			return false;
		}
		return length_is_fixed(p_vector) || *length == p_vector->length || resize_vector(p_vector, *length);
	}
	const uint32_t at = *p_key.index;
	if (!klass.array && (at > p_vector->length || (p_vector->fixed && at == p_vector->length))) {
		drop(klass.element, value);
		raise_range(at, p_vector->length);
		return false;
	}
	if (at >= p_vector->length && !resize_vector(p_vector, at + 1)) {
		drop(klass.element, value);
		return false;
	}
	put(klass.element, p_vector->data[at], value);
	return true;
}

// delete holder[key]: true unless the property is one that can't go (a sealed object's field or method,
// a vector's length). An element of an Array becomes undefined.
bool Interpreter::delete_property(Slot p_holder, const PropertyKey &p_key, bool *r_done) {
	*r_done = true;
	Object *object = held_object(p_holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	Entries *entries = klass && klass->table ? entries_of(object) : nullptr;
	if (klass && klass->element != Kind::Void) {
		Vector *vector = static_cast<Vector *>(object);
		if (p_key.index && klass->array) {
			if (*p_key.index < vector->length) {
				put(Kind::Any, vector->data[*p_key.index], any::undefined());
			}
			return true;
		}
		*r_done = klass->array && !p_key.index && p_key.text != "length";
		entries = klass->array ? vector->properties : nullptr;
	} else if (klass && !klass->table) {
		*r_done = !klass->field(p_key.text, true) && !named_method(*klass, p_key.text);
	} else if (!klass && (any::tag(p_holder) == AnyTag::Null || any::tag(p_holder) == AnyTag::Undefined)) {
		any_null_check(p_holder);
		return false;
	}
	if (const std::optional<uint32_t> at = entry_of(entries, p_key)) {
		const Entry gone = entries->remove(*at);
		drop_any(gone.key);
		drop_any(gone.value);
	}
	return true;
}

// key in holder (p_prototype: what its prototype has counts), or holder.hasOwnProperty(key).
bool Interpreter::has_property(Slot p_holder, const PropertyKey &p_key, bool p_prototype, bool *r_has) {
	Object *object = held_object(p_holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	const AnyTag tag = any::tag(p_holder);
	if (!klass && (tag == AnyTag::Null || tag == AnyTag::Undefined)) {
		any_null_check(p_holder);
		return false;
	}
	if (!klass && any::is_engine(p_holder)) {
		raise_unsupported("a property of one of Godot's values or objects held untyped, by its name");
		return false;
	}
	bool has = p_prototype && listed(OBJECT_METHODS, p_key.text);
	if (klass && klass->table) {
		has = has || entry_of(entries_of(object), p_key).has_value();
	} else if (klass && klass->element != Kind::Void) {
		const Vector *vector = static_cast<const Vector *>(object);
		const bool element = p_key.index && *p_key.index < vector->length &&
				(!klass->array || any::tag(vector->data[*p_key.index]) != AnyTag::Undefined);
		has = has || element || p_key.text == "length" || (!klass->array && p_key.text == "fixed") ||
				(klass->array && entry_of(vector->properties, p_key).has_value()) || (p_prototype && listed(ARRAY_METHODS, p_key.text));
	} else if (klass) {
		has = has || klass->field(p_key.text, true) || named_method(*klass, p_key.text) || library_has(object, p_key.text);
	} else if (tag == AnyTag::String) {
		has = has || p_key.text == "length" || (p_prototype && listed(STRING_METHODS, p_key.text));
	} else if (tag != AnyTag::Boolean) {
		has = has || (p_prototype && listed(NUMBER_METHODS, p_key.text));
	}
	*r_has = has;
	return true;
}

// DynGet, DynSet, DynDelete and DynIn. Making the key's text may run the program, which may move
// the registers: the operands are held first, and the result is written through the frame's place.
void Interpreter::property_op(const Function &p_function, const Instruction &p_in) {
	const size_t base = frames_.back().base;
	const bool setting = p_in.op == Op::DynSet;
	const uint32_t holder_reg = setting ? p_in.a : p_in.b;
	const uint32_t key_reg = setting ? p_in.b : p_in.c;
	const Slot holder = to_any(p_function.registers[holder_reg], stack_[base + holder_reg]);
	const Slot key = to_any(p_function.registers[key_reg], stack_[base + key_reg]);
	const Slot value = setting ? share(Kind::Any, stack_[base + p_in.c]) : any::undefined();
	PropertyKey found;
	const bool missing = !held_object(holder) && (any::tag(holder) == AnyTag::Null || any::tag(holder) == AnyTag::Undefined);
	if (missing) {
		any_null_check(holder); // Before the key's text: a missing object is the error, as in Flash.
	} else if (property_key(holder, key, &found)) {
		Slot out;
		bool answer = false;
		switch (p_in.op) {
			case Op::DynGet:
				if (get_property(holder, found, &out)) {
					put(Kind::Any, stack_[base + p_in.a], out);
				}
				break;
			case Op::DynSet:
				set_property(holder, found, value);
				break;
			case Op::DynDelete:
				if (delete_property(holder, found, &answer)) {
					put(Kind::Bool, stack_[base + p_in.a], of_bool(answer));
				}
				break;
			default:
				if (has_property(holder, found, true, &answer)) {
					put(Kind::Bool, stack_[base + p_in.a], of_bool(answer));
				}
				break;
		}
	}
	drop_any(holder);
	drop_any(key);
	drop_any(value);
}

} // namespace vm
} // namespace kas3
