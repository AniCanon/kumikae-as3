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

#include <cmath>
#include <cstring>
#include <limits>
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

// The encoding above, in numbers. What needs no more than these is inline here: a value is told from
// another, made of a number or a Boolean, and found to hold nothing, wherever it is handled, generated code
// included, with no call.
inline constexpr uint64_t NUMBER_OFFSET = uint64_t(1) << 48; // Added to a Number's bits: its top 16 bits are never 0.
inline constexpr uint64_t INT_FLAG = uint64_t(1) << 47;
inline constexpr uint64_t TAG_MASK = 7;
// 64-bit addresses reserve another low bit to remember a heap tag in their top byte.
inline constexpr size_t POINTER_ALIGNMENT = sizeof(uintptr_t) == 4 ? 8 : 16;
inline constexpr uint64_t HEAP_TAGGED = POINTER_ALIGNMENT == 16 ? uint64_t(1) << 3 : 0;
inline constexpr uint64_t NULL_BITS = 1 << 3;
inline constexpr uint64_t FALSE_BITS = 2 << 3;
inline constexpr uint64_t TRUE_BITS = 3 << 3;
// The low 3 bits of a pointer that is held.
enum PointerTag : uint64_t { STRING = 1, OBJECT = 2, VALUE = 3, GODOT = 4, WIDE = 5 };

inline Slot of_bits(uint64_t p_bits) {
	Slot slot;
	slot.bits = p_bits;
	return slot;
}
inline bool is_number(Slot p_value) {
	return (p_value.bits >> 48) != 0;
}

inline Slot undefined() {
	return of_bits(0);
}
inline Slot null() {
	return of_bits(NULL_BITS);
}
inline Slot boolean(bool p_value) {
	return of_bits(p_value ? TRUE_BITS : FALSE_BITS);
}
inline Slot number(double p_value) {
	if (std::isnan(p_value)) {
		p_value = std::numeric_limits<double>::quiet_NaN();
	}
	uint64_t raw = 0;
	std::memcpy(&raw, &p_value, sizeof(raw));
	return of_bits(raw + NUMBER_OFFSET);
}
inline Slot integer(int32_t p_value) {
	return of_bits(INT_FLAG | static_cast<uint32_t>(p_value));
}
// A pointer is held as it is, with no new reference: the caller decides who owns it.
Slot string(Str *p_string);
Slot object(Object *p_object);
// A borrowed handle can come from a host stack with weaker alignment than the heap.
inline bool can_borrow(const void *p_handle) {
	const auto address = reinterpret_cast<uintptr_t>(p_handle);
	return address && !(address & (POINTER_ALIGNMENT - 1)) && static_cast<uint64_t>(address) < INT_FLAG;
}
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

inline AnyTag tag(Slot p_value) {
	if (is_number(p_value)) {
		return AnyTag::Number;
	}
	if (p_value.bits & INT_FLAG) {
		return AnyTag::Int;
	}
	switch (p_value.bits & TAG_MASK) {
		case STRING:
			return AnyTag::String;
		case OBJECT:
			return AnyTag::Object;
		case VALUE:
			return AnyTag::Value;
		case GODOT:
			return AnyTag::Godot;
		case WIDE:
			return AnyTag::Wide;
		default:
			break;
	}
	if (p_value.bits == NULL_BITS) {
		return AnyTag::Null;
	}
	return p_value.bits == FALSE_BITS || p_value.bits == TRUE_BITS ? AnyTag::Boolean : AnyTag::Undefined;
}
// Whether the value holds something that is counted or a handle (a String, an object, an Int64's box, one
// of Godot's): a number, a Boolean, null and undefined hold nothing, and are shared and let go of for free.
inline bool holds(Slot p_value) {
	return !is_number(p_value) && !(p_value.bits & INT_FLAG) && (p_value.bits & TAG_MASK) != 0;
}
inline bool to_bool_payload(Slot p_value) { // For Boolean.
	return p_value.bits == TRUE_BITS;
}
inline double to_number_payload(Slot p_value) { // For Number.
	const uint64_t raw = p_value.bits - NUMBER_OFFSET;
	double out = 0;
	std::memcpy(&out, &raw, sizeof(out));
	return out;
}
inline int32_t to_int_payload(Slot p_value) { // For Int.
	return static_cast<int32_t>(static_cast<uint32_t>(p_value.bits));
}
// Only a pointer marked with a saved top byte needs the platform heap tag restored.
void *tagged_pointer(Slot p_value);
inline void *pointer(Slot p_value) {
	if (!holds(p_value)) {
		return nullptr;
	}
	if (p_value.bits & HEAP_TAGGED) {
		return tagged_pointer(p_value);
	}
	return reinterpret_cast<void *>(static_cast<uintptr_t>(p_value.bits & ~TAG_MASK));
}
// The counted thing the value holds, if it holds one (a String, a script object or a Wide).
Counted *counted(Slot p_value);
// Whether it holds a handle on an engine value or object.
inline bool is_engine(Slot p_value) {
	const AnyTag kind = tag(p_value);
	return kind == AnyTag::Value || kind == AnyTag::Godot;
}

// ECMAScript's conversions, where they need nothing outside the value. A script object or a Godot value
// is not a number (NaN), and is true.
double to_number(Slot p_value);
float to_float(Slot p_value);
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
