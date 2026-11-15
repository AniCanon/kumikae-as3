// The values the interpreter works on: numbers, strings and objects, and the rules for converting between
// them. Strings here are a stand-in: inside Godot they will be the engine's own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace kas3 {
namespace vm {

// One register, field or global. What it holds is known from the IR, not from the slot.
union Slot {
	int32_t i;
	uint32_t u;
	double d;
	int64_t l;
	bool b;
	void *p;
	uint64_t bits;
};

// Everything counted starts with this. Counts are plain: one thread. Every change to a count goes through
// retain() and release() below, so that the rule can change in one place.
struct Counted {
	uint32_t references;
};

struct Str : Counted {
	uint32_t size; // In bytes.
	char *data() { return reinterpret_cast<char *>(this + 1); }
	const char *data() const { return reinterpret_cast<const char *>(this + 1); }
	std::string_view view() const { return std::string_view(data(), size); }
};

struct Object : Counted {
	uint32_t klass; // An index into the module's classes.
	Slot *fields() { return reinterpret_cast<Slot *>(this + 1); }
};

// A Vector.<T>: an object whose class says what its elements are. They are held like fields: untagged
// slots of that one kind.
struct Vector : Object {
	uint32_t length;
	uint32_t capacity; // Slots there is room for; those from `length` on are zero.
	bool fixed; // Its length may not change.
	Slot *data;
};

inline void retain(Counted *p_value) {
	if (p_value) {
		p_value->references++;
	}
}
// True if that was the last reference: the caller frees the value.
inline bool release(Counted *p_value) {
	return p_value && --p_value->references == 0;
}

// What a field or a static field holds before anything is written to it: NaN for a Number, `null` for a
// value declared `Object`, and zero (no reference, `undefined`, 0, false) for the rest.
Slot empty_value(bool p_number, bool p_null);

// The most elements a vector may have: more is refused as memory running out (a length that came from a
// negative number, most likely). Every way a vector grows checks this one number.
constexpr uint32_t MAX_VECTOR_LENGTH = 1u << 30;

// What AS3 says when memory runs out, wherever it does.
constexpr std::string_view OUT_OF_MEMORY = "Error #1000: The system is out of memory.";

// For tests: the next p_count allocations of strings, objects, vectors and Int64 boxes fail, as they
// would with no memory left.
void fail_allocations(uint32_t p_count);
bool allocation_fails(); // True, and one fewer to fail, while any are still to fail.

// How many strings and objects are alive, for finding leaks.
size_t live_strings();
size_t live_objects();

Str *new_string(std::string_view p_text); // One reference, the caller's.
void free_string(Str *p_string);
Object *new_object(uint32_t p_class, size_t p_fields); // One reference; fields zeroed. Null if out of memory.
void free_object(Object *p_object); // The fields must have been released.
// One reference; `p_length` elements, all zero. Null if out of memory.
Vector *new_vector(uint32_t p_class, uint32_t p_length, bool p_fixed);
// Room for at least that many elements, keeping the ones there are. False if out of memory.
bool vector_reserve(Vector *p_vector, uint32_t p_capacity);
void free_vector(Vector *p_vector); // The elements must have been released.

// Conversions, as ECMAScript and AS3 define them.

int32_t to_int32(double p_value);
uint32_t to_uint32(double p_value);
// "0.1", "1e+21", "NaN", "-Infinity": the shortest text that reads back as the same number.
std::string number_to_string(double p_value);
// Leading and trailing white space is allowed; an empty string is 0; anything else that is not a number
// is NaN.
double string_to_number(std::string_view p_text);
// The number of characters (Unicode code points) in UTF-8 text.
int32_t character_count(std::string_view p_text);

} // namespace vm
} // namespace kas3
