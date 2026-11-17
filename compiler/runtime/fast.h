// The fast paths of operations the library owns. Each handles the usual case and nothing else, and says
// when it did not: generated code then hands the same instruction to the library by thunk, which does the
// rest and raises what there is to raise. Nothing here words an error or decides an unusual case.
#pragma once

#include "program.h"
#include "values.h"

#include <cmath>
#include <limits>

namespace kas3 {
namespace rt {

// A vector's element by an int or a uint index, if the index names one: nothing for no vector, a negative
// index or one past the end. element() is for places that are slots, number() for the four bytes of an int
// or a uint (Vector::narrow). Which a vector's are is not asked here for a slot: generated code calls
// element() where the element's register is of any kind but an int's or a uint's, the register's kind is
// the kind the vector's class holds (the lowerer types an element by the vector's declared type, as the
// interpreter relies on when it counts what it reads), and a register declared to hold a vector holds no
// vector of another class: vector types do not convert to one another, and an untyped value is checked
// against the class as it is stored (tests/run/vector_narrow.as). It is the same trust a pointer read
// out of a place has always had. number() does ask, which costs a vector of ints one test and keeps a
// read of four bytes inside a vector whose places are eight.
inline Slot *element(Object *p_vector, uint32_t p_index) {
	Vector *const vector = static_cast<Vector *>(p_vector);
	return vector && p_index < vector->length ? vm::slots_of(vector) + p_index : nullptr;
}
inline Slot *element(Object *p_vector, int32_t p_index) {
	return element(p_vector, static_cast<uint32_t>(p_index));
}
inline int32_t *number(Object *p_vector, uint32_t p_index) {
	Vector *const vector = static_cast<Vector *>(p_vector);
	return vector && vector->narrow && p_index < vector->length ? vm::numbers_of(vector) + p_index : nullptr;
}
inline int32_t *number(Object *p_vector, int32_t p_index) {
	return number(p_vector, static_cast<uint32_t>(p_index));
}
// The places of a vector, for a loop that keeps them in a local: nothing for no vector, and for one whose
// places are of the other sort.
inline Slot *slots_in(Object *p_vector) {
	return p_vector && !static_cast<Vector *>(p_vector)->narrow ? vm::slots_of(static_cast<Vector *>(p_vector)) : nullptr;
}
inline int32_t *numbers_in(Object *p_vector) {
	return p_vector && static_cast<Vector *>(p_vector)->narrow ? vm::numbers_of(static_cast<Vector *>(p_vector)) : nullptr;
}

// The vector, if its places hold what the code that asks reads and writes there: one of Godot's values
// whole (p_whole: the type's number, which generated code is written with, as Vector::bare has it), or, for
// 0, what a field of the element's kind holds. Nothing for a vector of the other sort, and for none: the
// library reads and writes those. Only a place of one of Godot's values is ever asked about: no other
// vector holds anything whole.
inline Object *vector_of(Object *p_vector, uint8_t p_whole) {
	return p_vector && static_cast<Vector *>(p_vector)->bare == p_whole ? p_vector : nullptr;
}

// A new object of the class, its fields as an object's are before its constructor runs, with one reference,
// the caller's. Null for a class whose objects are more than fields (a vector, a table, one that extends one
// of Godot's: the library makes those), and when there is no memory.
//     Object *new_object(Context *c, uint32_t p_class);

// The same from the host's cache, where the class's objects are kept there and one is: its fields are
// numbers and Booleans only, each as an object's is before its constructor runs.
inline Object *made_object(Context *c, uint32_t p_class) {
	const uint32_t size = c->class_cached[p_class];
	Object *const found = size ? c->cache->spare[size] : nullptr;
	if (KAS3_UNLIKELY(!found)) {
		return new_object(c, p_class);
	}
	c->cache->spare[size] = found->gc_next;
	c->cache->count[size]--;
	found->references = 1;
	found->klass = p_class;
	found->gc_next = nullptr;
	const uint32_t numbers = c->class_numbers[p_class];
	for (uint32_t i = 0; i + 1 < size; i++) {
		field<Slot>(found, i) = (numbers >> i) & 1 ? slot(std::numeric_limits<double>::quiet_NaN()) : empty();
	}
	return found;
}

// The element VecSet writes: one there already is, or the one just past the end of a vector that may grow,
// which is added (empty). Nothing for any other index, a fixed vector or no memory.
//     void *element_added(Object *p_vector, uint32_t p_index);
inline Slot *element_to_write(Context *c, Object *p_vector, uint32_t p_index) {
	Slot *const there = element(p_vector, p_index);
	return there ? there : static_cast<Slot *>(element_added(c, p_vector, p_index));
}
inline Slot *element_to_write(Context *c, Object *p_vector, int32_t p_index) {
	return p_index < 0 ? nullptr : element_to_write(c, p_vector, static_cast<uint32_t>(p_index));
}
inline int32_t *number_to_write(Context *c, Object *p_vector, uint32_t p_index) {
	if (int32_t *const there = number(p_vector, p_index)) {
		return there;
	}
	return p_vector && static_cast<Vector *>(p_vector)->narrow ? static_cast<int32_t *>(element_added(c, p_vector, p_index)) : nullptr;
}
inline int32_t *number_to_write(Context *c, Object *p_vector, int32_t p_index) {
	return p_index < 0 ? nullptr : number_to_write(c, p_vector, static_cast<uint32_t>(p_index));
}

// Where push() of one value writes it, in a vector with room for one more: the room past a vector's end is
// empty, so nothing is let go of there, and whoever writes it then counts the element in (`length`).
// Nothing for no vector, a fixed one, or one that must grow first: the library's.
inline bool has_room(const Vector *p_vector) {
	return p_vector && !p_vector->fixed && p_vector->length < p_vector->capacity && p_vector->length < vm::MAX_VECTOR_LENGTH;
}
inline Slot *push_room(Object *p_vector) {
	Vector *const vector = static_cast<Vector *>(p_vector);
	return KAS3_LIKELY(has_room(vector)) ? vm::slots_of(vector) + vector->length : nullptr;
}
// The same in a vector of ints or of uints.
inline int32_t *number_room(Object *p_vector) {
	Vector *const vector = static_cast<Vector *>(p_vector);
	return KAS3_LIKELY(has_room(vector) && vector->narrow) ? vm::numbers_of(vector) + vector->length : nullptr;
}

// What holds a property, and a key that is a String: null for a register that holds anything else.
inline Object *holder_of(Context *, Object *p_object) {
	return p_object;
}
inline Object *holder_of(Context *c, Slot p_any) {
	return holder_of_any(c, p_any);
}
//     Object *holder_of(Slot p_any);
inline Str *key_of(Context *, Str *p_text) {
	return p_text;
}
inline Str *key_of(Context *c, Slot p_any) {
	return key_of_any(c, p_any);
}
//     Str *key_of(Slot p_any);
// The property an Object or a Dictionary has under a String key: where its value is held. Nothing for no
// holder, another kind of object, no key, or no such property, all of which the library answers.
//     Slot *property(const Context *c, Object *p_holder, const Str *p_key);
// Writes that property, making it if it is not there. False, with nothing done, for no holder, another kind
// of object or no key.
//     bool set_property(Context *c, Object *p_holder, Str *p_key, Slot p_value);

// A new Array of untyped values, each with a reference of its own: one reference, the caller's. Null with
// an error on its way out if it could not be made.
//     Object *new_array(Context *c, const Slot *p_values, uint32_t p_count);
// `arguments`: an Array of what the caller gave the function (by its number), from every parameter's slot
// and how many of them were given, with p_callee (the function's own value, whose reference is taken over)
// as its hidden `callee`. Null with an error on its way out if it could not be made.
//     Object *arguments_of(Context *c, uint32_t p_function, const Slot *p_parameters, uint32_t p_given, Object *p_callee);

// One String after another, a missing one reading "null". Null if there is no memory for it.
inline Str *join(Context *c, const Str *p_first, const Str *p_second) {
	return join_text(c, p_first, p_second);
}
// One of a String's methods that only read text (vm/text_methods.h; p_which is StrCall's second operand),
// on the values StrCall lists: what it gives, whole. False, with nothing given, if there is no memory.
//     bool text_method(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
template <class... Given>
inline bool text_method_of(Context *c, uint32_t p_which, Slot *r_out, Given... p_given) {
	const Slot values[] = { p_given..., empty() }; // One more than given: no array is empty.
	return text_method(c, p_which, values, static_cast<uint32_t>(sizeof...(Given)), r_out);
}
// Two Strings are the same text, or both missing.
inline bool same_text(const Str *p_a, const Str *p_b) {
	return p_a == p_b || (p_a && p_b && p_a->view() == p_b->view());
}

// A number or a Boolean as a new String, as the interpreter writes it. Null if there is no memory for it.
//     Str *text_of(int32_t p_value);
//     Str *text_of(uint32_t p_value);
//     Str *text_of(int64_t p_value);
//     Str *text_of(double p_value);
//     Str *text_of(bool p_value);

inline Str *text_of(Context *c, int32_t p_value) {
	return text_of_int(c, p_value);
}
inline Str *text_of(Context *c, uint32_t p_value) {
	return text_of_uint(c, p_value);
}
inline Str *text_of(Context *c, int64_t p_value) {
	return text_of_wide(c, p_value);
}
inline Str *text_of(Context *c, double p_value) {
	return text_of_number(c, p_value);
}
inline Str *text_of(Context *c, bool p_value) {
	return text_of_flag(c, p_value);
}

// A typed value as an untyped one, with a reference of its own.
inline Slot to_any(Context *c, int32_t p_value) {
	return any_integer(c, p_value);
}
inline Slot to_any(Context *c, bool p_value) {
	return any_boolean(c, p_value);
}
inline Slot to_any(Context *c, Str *p_value) {
	retain(c, p_value);
	return any_string(c, p_value);
}
inline Slot to_any(Context *c, Object *p_value) {
	retain(c, p_value);
	return any_object(c, p_value);
}
inline Slot to_any(Context *c, uint32_t p_value) {
	return p_value <= static_cast<uint32_t>(INT32_MAX) ? any_integer(c, static_cast<int32_t>(p_value)) : any_number(c, p_value);
}
// A Number as an untyped value, as the library makes one (vm/interpreter_any.cpp, any_number): a whole
// number an int holds as an int, a larger one as an Int64 in a box. False only with no memory for the box.
//     bool number_to_any(double p_value, Slot *r_any);
// The same, done here where nothing is made: a whole number an int holds, and a fraction in an int's range.
// The rest (a larger number, which may want a box, -0 and what is no number) are number_to_any()'s.
inline bool number_any(Context *c, double p_value, Slot *r_any) {
	if (KAS3_LIKELY(p_value >= INT32_MIN && p_value <= INT32_MAX)) {
		const int32_t whole = static_cast<int32_t>(p_value);
		if (whole != p_value) {
			*r_any = vm::any::number(p_value);
			return true;
		}
		if (whole != 0 || !std::signbit(p_value)) {
			*r_any = vm::any::integer(whole);
			return true;
		}
	}
	return number_to_any(c, p_value, r_any);
}

// An untyped value as a value of the kind (an ir::Kind), where it already holds one: a number for a number,
// a String or nothing for a String, an object of the class wanted (any, for NONE) or nothing for an
// object. The value stays the untyped value's: no reference is taken. False for anything that must be
// converted, checked further or refused.
//     bool from_any(const Context *c, uint8_t p_kind, uint32_t p_class, Slot p_any, Slot *r_value);
// Whether an untyped value is neither null nor undefined: one that is has no members, which the library says.
//     bool has_members(Slot p_any);

// Whether a register holds a function value, which the library's function_call() calls. Anything else is
// the interpreter's to answer: a class called as a function converts what it is given, and the rest is an
// error it words.
inline bool holds_function(const Context *c, const Object *p_value) {
	return p_value && p_value->klass == c->program->known.function;
}
inline bool holds_function(Context *c, Slot p_any) {
	return holds_function(c, function_of_any(c, p_any));
}

// The function value a register holds, for call_value(): null for anything else.
inline Object *function_of(Context *, Object *p_value) {
	return p_value;
}
inline Object *function_of(Context *c, Slot p_any) {
	return function_of_any(c, p_any);
}
//     Object *function_of(Slot p_any);
// A call through a function value with untyped arguments, when all is as usual: a function value, as many
// arguments as it takes, each already of its parameter's kind (from_any). The result, untyped, is the
// caller's. False, with nothing done, for anything else: the library then makes the call, or says why not.
// True with c->failing set if the function raised.
//     bool call_value(Context *c, Object *p_function, const Slot *p_arguments, uint32_t p_count, Slot *r_result);

// An object's toString(), when its class has one of its own to call: false for no object, a vector, or a
// class that says nothing, whose words are the library's. True with c->failing set if it raised.
//     bool object_text(Context *c, Object *p_object, Str **r_text);
// String(x) of an untyped value that holds a String (itself) or such an object.
//     bool any_text(Context *c, Slot p_any, Str **r_text);

} // namespace rt
} // namespace kas3
