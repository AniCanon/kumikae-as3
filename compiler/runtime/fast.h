// The fast paths of operations the library owns. Each handles the usual case and nothing else, and says
// when it did not: generated code then hands the same instruction to the library by thunk, which does the
// rest and raises what there is to raise. Nothing here words an error or decides an unusual case.
#pragma once

#include "program.h"
#include "values.h"

namespace kas3 {
namespace rt {

// A vector's element by an int or a uint index, if the index names one: nothing for no vector, a negative
// index or one past the end.
inline Slot *element(Object *p_vector, int32_t p_index) {
	Vector *const vector = static_cast<Vector *>(p_vector);
	return vector && static_cast<uint32_t>(p_index) < vector->length ? vector->data + static_cast<uint32_t>(p_index) : nullptr;
}
inline Slot *element(Object *p_vector, uint32_t p_index) {
	Vector *const vector = static_cast<Vector *>(p_vector);
	return vector && p_index < vector->length ? vector->data + p_index : nullptr;
}

// A new object of the class, its fields as an object's are before its constructor runs, with one reference,
// the caller's. Null for a class whose objects the cycle collector looks at (they are made on its list,
// which is the library's), and when there is no memory.
Object *new_object(Context *c, uint32_t p_class);

// One String after another, a missing one reading "null". Null if there is no memory for it.
inline Str *join(const Str *p_first, const Str *p_second) {
	return vm::join_strings(p_first ? p_first->view() : std::string_view("null"), p_second ? p_second->view() : std::string_view("null"));
}
// Two Strings are the same text, or both missing.
inline bool same_text(const Str *p_a, const Str *p_b) {
	return p_a == p_b || (p_a && p_b && p_a->view() == p_b->view());
}

// A number or a Boolean as a new String, as the interpreter writes it. Null if there is no memory for it.
Str *text_of(int32_t p_value);
Str *text_of(uint32_t p_value);
Str *text_of(int64_t p_value);
Str *text_of(double p_value);
Str *text_of(bool p_value);

// A typed value as an untyped one, with a reference of its own.
inline Slot to_any(Context *, int32_t p_value) {
	return vm::any::integer(p_value);
}
inline Slot to_any(Context *, bool p_value) {
	return vm::any::boolean(p_value);
}
inline Slot to_any(Context *c, Str *p_value) {
	retain(c, p_value);
	return vm::any::string(p_value);
}
inline Slot to_any(Context *c, Object *p_value) {
	retain(c, p_value);
	return vm::any::object(p_value);
}
inline Slot to_any(Context *, uint32_t p_value) {
	return p_value <= static_cast<uint32_t>(INT32_MAX) ? vm::any::integer(static_cast<int32_t>(p_value)) : vm::any::number(p_value);
}
// A Number as an untyped value, where that needs nothing made (it is not a whole number, or it is one an
// int holds): false for the rest, which the library boxes.
bool number_to_any(double p_value, Slot *r_any);

// An untyped value as a value of the kind (an ir::Kind), where it already holds one: a number for a number,
// a String or nothing for a String, an object of the class wanted (any, for NONE) or nothing for an
// object. The value stays the untyped value's: no reference is taken. False for anything that must be
// converted, checked further or refused.
bool from_any(const Context *c, uint8_t p_kind, uint32_t p_class, Slot p_any, Slot *r_value);
// Whether an untyped value is neither null nor undefined: one that is has no members, which the library says.
bool has_members(Slot p_any);

// The function value a register holds, for call_value(): null for anything else.
inline Object *function_of(Object *p_value) {
	return p_value;
}
Object *function_of(Slot p_any);
// A call through a function value with untyped arguments, when all is as usual: a function value, as many
// arguments as it takes, each already of its parameter's kind (from_any). The result, untyped, is the
// caller's. False, with nothing done, for anything else: the library then makes the call, or says why not.
// True with c->failing set if the function raised.
bool call_value(Context *c, Object *p_function, const Slot *p_arguments, uint32_t p_count, Slot *r_result);

// An object's toString(), when its class has one of its own to call: false for no object, a vector, or a
// class that says nothing, whose words are the library's. True with c->failing set if it raised.
bool object_text(Context *c, Object *p_object, Str **r_text);
// String(x) of an untyped value that holds a String (itself) or such an object.
bool any_text(Context *c, Slot p_any, Str **r_text);

} // namespace rt
} // namespace kas3
