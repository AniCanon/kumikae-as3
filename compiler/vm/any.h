// A value of AS3's untyped type (`*`) or of type `Object`: any value, with what it is kept beside it, in
// one slot like every other register.
//
// The top 16 bits say which: zero for everything that is not a Number, and a Number is held as its own
// bits plus 2^48, which never leaves the top 16 bits zero (a NaN is kept as the one quiet NaN). Among the
// rest: bit 47 set is an `int` in the low 32 bits; otherwise the low 3 bits are a tag on a pointer, which
// is at least 8-aligned, and with no pointer they are `undefined` (all zero: a fresh slot), `null`,
// `false` and `true`. An `Int64` is the one value that does not fit, and is held in a counted box.
#pragma once

#include "runtime.h"

#include <optional>
#include <string>

namespace kas3 {
namespace vm {

// An Int64 held by a `*`.
struct Wide : Counted {
	int64_t value;
};
Wide *new_wide(int64_t p_value); // One reference, the caller's.
void free_wide(Wide *p_wide);

enum class AnyTag : uint8_t { Undefined, Null, Boolean, Number, Int, String, Object, Value, Godot, Wide };

namespace any {

Slot undefined();
Slot null();
Slot boolean(bool p_value);
Slot number(double p_value);
Slot integer(int32_t p_value);
// A pointer is held as it is, with no new reference: the caller decides who owns it.
Slot string(Str *p_string);
Slot object(Object *p_object);
Slot godot_value(void *p_handle); // One of Godot's values (a Vector2).
Slot godot_object(void *p_handle); // One of Godot's objects (a node).
Slot wide(Wide *p_wide);
// What an Array's slot holds where no element was ever written, or one was deleted: a hole, as avmplus keeps
// one (atomNotFound in ArrayObject's dense list), which is not an element that holds undefined. It is only
// ever in an Array's slots. Whatever reads an element reads it through filled(), and so reads undefined; a
// hole that got out all the same is undefined to everything here (tag() says so).
constexpr uint64_t HOLE_BITS = 4 << 3; // No pointer, and none of undefined, null, false and true.
inline Slot hole() {
	Slot slot;
	slot.bits = HOLE_BITS;
	return slot;
}
inline bool is_hole(Slot p_value) {
	return p_value.bits == HOLE_BITS;
}
// An untyped element as whatever reads it is given it: undefined for a hole.
inline Slot filled(Slot p_element) {
	if (p_element.bits == HOLE_BITS) {
		p_element.bits = 0;
	}
	return p_element;
}

AnyTag tag(Slot p_value);
bool to_bool_payload(Slot p_value); // For Boolean.
double to_number_payload(Slot p_value); // For Number.
int32_t to_int_payload(Slot p_value); // For Int.
void *pointer(Slot p_value); // For String, Object, Value, Godot, Wide; null for the rest.
// The counted thing the value holds, if it holds one (a String, a script object or a Wide).
Counted *counted(Slot p_value);
// Whether it holds a handle on an engine value or object.
bool is_engine(Slot p_value);

// ECMAScript's conversions, where they need nothing outside the value. A script object or a Godot value
// is not a number (NaN), and is true.
double to_number(Slot p_value);
bool to_boolean(Slot p_value);
const char *type_of(Slot p_value);
// The text of a value that needs nothing outside it; nothing for a script object or a Godot value or
// object, whose text the interpreter or the engine gives.
std::optional<std::string> text(Slot p_value);
// `===` and `==`, where they need nothing outside the values; nothing where both are Godot's (the engine
// answers) or one is a script object being compared by `==` with a primitive (its text, from the
// interpreter).
std::optional<bool> strict_equal(Slot p_a, Slot p_b);
// Two numbers in order, exactly: -1, 0 or 1; nothing if either is not a number (NaN). An Int64 is never
// rounded to a Number to be compared.
std::optional<int> compare_numbers(Slot p_a, Slot p_b);
std::optional<bool> loose_equal(Slot p_a, Slot p_b);

} // namespace any
} // namespace vm
} // namespace kas3
