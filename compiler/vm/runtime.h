// The values the interpreter works on: numbers, strings and objects, and the rules for converting between
// them. Strings here are a stand-in: inside Godot they will be the engine's own.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
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
	mutable std::optional<int32_t> cached_units;
	int32_t length() const; // The UTF-16 length of these immutable bytes.
	char *data() { return reinterpret_cast<char *>(this + 1); }
	const char *data() const { return reinterpret_cast<const char *>(this + 1); }
	std::string_view view() const { return std::string_view(data(), size); }
};

struct Object : Counted {
	uint32_t klass; // An index into the module's classes.
	// For the cycle collector (vm/collector.h): the list it is on, or null for an object no collector looks
	// at (its class can hold nothing that leads back to it), and what the collector last found of it.
	Object *gc_previous;
	Object *gc_next;
	int32_t gc_count;
	uint32_t gc_state;
};

// A slot of an object, by its number: they follow the header. Which slot a field is in is not said here:
// vm/fields.h says it for the interpreter, the library and the hosts, and generated code has its class's
// description (runtime/values.h). Nothing else reads an object's slots.
inline Slot &slot_at(Object *p_object, size_t p_slot) {
	return reinterpret_cast<Slot *>(p_object + 1)[p_slot];
}

class Entries; // vm/table.h

// A Vector.<T> or fixed array: an object whose class says what its elements are. They are held like fields: untagged
// slots of that one kind, except where a byte, int, uint or float fits in a smaller place.
struct Vector : Object {
	uint32_t length;
	uint32_t capacity; // Places there is room for; those from `length` on are zero.
	bool fixed; // Its length may not change.
	// Which of Godot's value types each place holds whole, in the slot itself (ir::Bare, as its number): a
	// Vector2's two reals. 0 for every other vector, whose places hold what a field of that kind holds. It
	// is the vector's class that says so (ir::Class::element_bare); here it is beside the length for code
	// that reads an element without the class.
	uint8_t bare;
	bool narrow; // Elements use fewer bytes than a Slot.
	bool floating; // Four-byte float places.
	bool bytes; // One-byte unsigned places.
	bool inline_places; // The element storage is part of the same allocation.
	// Access elements through place() and set_place() when their layout is not known.
	void *places;
	Entries *properties; // An Array's properties other than its indices, once it has any (vm/table.h).
};

// The places of a vector that is not narrow (any that holds something counted is not), and of one that is.
inline Slot *slots_of(const Vector *p_vector) {
	return static_cast<Slot *>(p_vector->places);
}
inline int32_t *numbers_of(const Vector *p_vector) {
	return static_cast<int32_t *>(p_vector->places);
}
inline uint8_t *bytes_of(const Vector *p_vector) {
	return static_cast<uint8_t *>(p_vector->places);
}
inline float *floats_of(const Vector *p_vector) {
	return static_cast<float *>(p_vector->places);
}
// How many bytes a place of the vector takes.
inline size_t place_size(bool p_bytes, bool p_narrow) {
	return p_bytes ? sizeof(uint8_t) : p_narrow ? sizeof(int32_t) : sizeof(Slot);
}
inline size_t place_size(const Vector *p_vector) {
	return place_size(p_vector->bytes, p_vector->narrow);
}
// What a place holds, as a slot of the element's kind holds it (an int or a uint in the slot's first four
// bytes, the rest zero), and a place given what such a slot holds. Nothing is counted by either.
inline Slot place(const Vector *p_vector, size_t p_index) {
	if (!p_vector->narrow) {
		return slots_of(p_vector)[p_index];
	}
	Slot slot;
	slot.bits = 0;
	if (p_vector->bytes) {
		slot.u = bytes_of(p_vector)[p_index];
	} else if (p_vector->floating) {
		slot.d = floats_of(p_vector)[p_index];
	} else {
		slot.i = numbers_of(p_vector)[p_index];
	}
	return slot;
}
inline void set_place(Vector *p_vector, size_t p_index, Slot p_value) {
	if (p_vector->bytes) {
		bytes_of(p_vector)[p_index] = static_cast<uint8_t>(p_value.u);
	} else if (p_vector->floating) {
		floats_of(p_vector)[p_index] = static_cast<float>(p_value.d);
	} else if (p_vector->narrow) {
		numbers_of(p_vector)[p_index] = p_value.i;
	} else {
		slots_of(p_vector)[p_index] = p_value;
	}
}
// The places from p_from up to p_to made empty (zero), and p_count places moved from p_from to p_to within
// one vector or between two whose places are the same size. Nothing is counted. Here, and not called: a
// push() empties one place and moves none.
inline void clear_places(Vector *p_vector, size_t p_from, size_t p_to) {
	if (p_vector->bytes) {
		std::fill(bytes_of(p_vector) + p_from, bytes_of(p_vector) + p_to, uint8_t{0});
	} else if (p_vector->floating) {
		std::fill(floats_of(p_vector) + p_from, floats_of(p_vector) + p_to, 0.0f);
	} else if (p_vector->narrow) {
		std::fill(numbers_of(p_vector) + p_from, numbers_of(p_vector) + p_to, 0);
	} else {
		std::fill(slots_of(p_vector) + p_from, slots_of(p_vector) + p_to, Slot());
	}
}
inline void move_places(Vector *p_into, size_t p_to, const Vector *p_out_of, size_t p_from, size_t p_count) {
	if (p_count) {
		const size_t size = place_size(p_into);
		std::memmove(static_cast<char *>(p_into->places) + p_to * size, static_cast<const char *>(p_out_of->places) + p_from * size, p_count * size);
	}
}

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

Str *new_string(std::string_view p_text); // One reference, the caller's. Null if out of memory.
// The two, one after the other, made at once: nothing else is allocated. Two halves of a character that
// meet there become the character (vm/text_units.h). Null if out of memory.
Str *join_strings(std::string_view p_first, std::string_view p_second);
void free_string(Str *p_string);
// One reference; fields zeroed. Null if out of memory. It is put on the list (the collector's list_for() its
// class), or on none if that is null: the list is never left out by mistake.
Object *new_object(uint32_t p_class, size_t p_fields, Object *p_list);
void free_object(Object *p_object); // The fields must have been released. It leaves the list it is on.
// One reference; `p_length` elements, all zero; p_bare is what its class says its places hold whole
// (Vector::bare), p_narrow whether they are four bytes each (Vector::narrow). Null if out of memory.
enum class VectorPlaces : uint8_t { Separate, Inline };
Vector *new_vector(uint32_t p_class, uint32_t p_length, bool p_fixed, uint8_t p_bare, bool p_narrow, Object *p_list,
		bool p_bytes = false, VectorPlaces p_places = VectorPlaces::Separate);
// Room for at least that many elements, keeping the ones there are. False if out of memory.
bool vector_reserve(Vector *p_vector, uint32_t p_capacity);
void free_vector(Vector *p_vector); // The elements and properties must have been released. It leaves its list.

// The cycle collector's lists: circular, through gc_previous and gc_next, each with an Object of its own as
// its head, which is never freed. An object is on one list at most.
void list_init(Object *p_head);
inline bool list_empty(const Object *p_head) {
	return p_head->gc_next == p_head;
}
void list_insert(Object *p_head, Object *p_object); // At the end.
void list_remove(Object *p_object); // From whichever list it is on; nothing if none.
void list_splice(Object *p_head, Object *p_from); // All of p_from's at the end of p_head's; p_from is left empty.

// Conversions, as ECMAScript and AS3 define them.

int32_t to_int32(double p_value);
uint32_t to_uint32(double p_value);
// "0.1", "1e+21", "NaN", "-Infinity": the shortest text that reads back as the same number.
std::string number_to_string(double p_value);
// Leading and trailing white space (ECMAScript's, vm/number_text.h) is allowed; an empty string is 0;
// anything else that is not a number is NaN.
double string_to_number(std::string_view p_text);
// AS3's length of UTF-8 text: its UTF-16 units, so a character past U+FFFF counts two.
int32_t utf16_length(std::string_view p_text);
// AS3's order of two texts held as UTF-8: by their UTF-16 units. Below 0, 0 or above.
int compare_utf16(std::string_view p_a, std::string_view p_b);

} // namespace vm
} // namespace kas3
