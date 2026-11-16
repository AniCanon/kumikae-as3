// AS3's standard library as functions of plain values: slots in, a slot out, no frame and no register.
// The interpreter runs them for its instructions, a call by name runs them for a method found by name, and
// native code calls them as they are. What a function needs beyond its values it is handed in a Library:
// the heap the values live on, and whoever runs the program, for the four things only that one can do.
#pragma once

#include "heap.h"
#include "ir.h"
#include "runtime.h"

#include <optional>
#include <string>
#include <string_view>

namespace kas3 {
namespace vm {

// Whoever runs the program: the interpreter, or native code's host. After any of these the caller's frame
// may have moved (the program ran): a library function keeps no pointer into one across them.
struct Runner {
	virtual ~Runner() {}
	// Starts an error of the class on its way out, with the message. Nothing if one is on its way already.
	virtual void raise(uint32_t p_class, const std::string &p_message) = 0;
	// AS3's Error #1000, with nothing more allocated than the error itself.
	virtual void out_of_memory() = 0;
	// Whether an error is on its way out.
	virtual bool failing() const = 0;
	// Calls a function value with untyped arguments, which stay the caller's. What it gives, untyped, is
	// the caller's to let go of. False if it raised, or if the value is no function (raised too).
	[[nodiscard]] virtual bool call(Object *p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) = 0;
	// String(x) of an untyped value: an object's toString() is the program's. False if it raised.
	[[nodiscard]] virtual bool text(Slot p_value, std::string *r_text) = 0;
};

// What the library's functions still ask of the interpreter itself: each is a part of the library that is
// not a function of plain values yet, and goes from here when it is. Where native code runs the program,
// these are its host's interpreter's, and an error one raises is the interpreter's until the host takes it.
struct Unmoved {
	virtual ~Unmoved() {}
	// Whether a vector's element of the kind is the value looked for, as `===` has it.
	virtual bool element_is(ir::Kind p_kind, Slot p_element, Slot p_value) = 0;
	// A typed value as an untyped one with a reference of its own, and an untyped one as a store into a
	// place of the kind makes it (false if it raised); and that store refused, with its TypeError (false).
	virtual Slot to_any(ir::Kind p_kind, Slot p_value) = 0;
	[[nodiscard]] virtual bool from_any(Slot p_value, ir::Kind p_kind, uint32_t p_check, Slot *r_out) = 0;
	virtual bool coercion_failed(Slot p_value, ir::Kind p_kind, uint32_t p_check) = 0;
	// An Array's property under a number that names no element, read (into an empty slot) and written.
	virtual bool array_number_get(Vector *p_array, double p_number, Slot *r_out) = 0;
	virtual bool array_number_set(Vector *p_array, double p_number, Slot p_value) = 0;
	// A vector's elements as text, as join() or toLocaleString() gives them.
	[[nodiscard]] virtual bool vector_text(Vector *p_vector, std::string_view p_separator, std::string *r_text, bool p_locale) = 0;
	// An element's property of that name for sortOn(), or undefined when it has none.
	[[nodiscard]] virtual bool sort_field(Slot p_element, Str *p_name, Slot *r_value) = 0;
};

struct Library {
	Heap &heap;
	Runner &runner;
	Unmoved &unmoved;

	const ir::Module &module() const { return heap.module; }
	const ir::Class &class_of(const Object *p_object) const { return heap.module.classes[p_object->klass]; }
	// A new String. Null, with Error #1000 raised, when there is no memory for it.
	Str *text(std::string_view p_text);
	// A new vector of the class, every element empty. Null, with Error #1000 raised, when it can't be made.
	Vector *fresh_vector(uint32_t p_class, uint64_t p_length);
	// AS3's TypeError #1009: a member of nothing was asked for.
	void raise_null();
};

// A new error of the class, as a script reads one: its message (taken over, or null), its name, and its
// number ("Error #1009: ..." is 1009). Its stack is whoever raises it's to fill in. Null, with the message
// let go of, for no class or no memory.
Object *new_error(Heap &p_heap, uint32_t p_class, Str *p_message);

// What a function below is handed: p_values are the instruction's values in its own order, each a whole
// slot of the kind the instruction says, borrowed from the caller. What it gives is written to r_out
// whole, with a reference that is the caller's (or, where r_out may be null and is, let go of). False if it
// raised.

// A String's methods that may run the program or raise (those ir::reads_text_only() leaves out): split()
// by a delimiter that is not a RegExp, as StrCall lists its values (the String, the delimiter untyped,
// the most pieces as a Number), and the URI functions (the text, where null reads "null").
[[nodiscard]] bool text_call(Library &p_library, ir::StringFunction p_function, const Slot *p_values, size_t p_count, Slot *r_out);

// toString(radix), toFixed, toExponential or toPrecision of a Number, or of an Int64 (p_wide), as text.
// Nothing, with AS3's RangeError raised, for a radix or a count of digits out of range.
std::optional<std::string> number_words(Library &p_library, ir::NumberFormat p_format, Slot p_value, bool p_wide, double p_digits);
// NumText: the same as a String, of its values (the number, then the radix or the digits as a Number).
[[nodiscard]] bool number_text(Library &p_library, ir::NumberFormat p_format, bool p_wide, const Slot *p_values, size_t p_count, Slot *r_out);

// VecCall: one of a vector's or an Array's methods, on the vector and then what the method takes. r_out
// may be null.
[[nodiscard]] bool vector_call(Library &p_library, ir::VectorMethod p_method, const Slot *p_values, size_t p_count, Slot *r_out);
// A vector's element at an index held as an int, a uint or a Number (p_index_kind), read with a reference
// of its own and written; its length set. The vector is not null. False if it raised.
bool vector_get(Library &p_library, Vector *p_vector, ir::Kind p_index_kind, Slot p_index, Slot *r_out);
bool vector_set(Library &p_library, Vector *p_vector, ir::Kind p_index_kind, Slot p_index, Slot p_value);
bool vector_set_length(Library &p_library, Vector *p_vector, uint32_t p_length);
// A new vector of a length, each place at its type's zero; a new Array as `new Array(...)` reads what it
// is given. Null if it raised.
Vector *vector_new(Library &p_library, uint32_t p_class, uint32_t p_length, bool p_fixed);
Vector *array_new(Library &p_library, uint32_t p_class, const Slot *p_values, size_t p_count);
// Vector.<T>(x).
[[nodiscard]] bool vector_from(Library &p_library, uint32_t p_class, Slot p_source, Slot *r_out);
// The parts of those the rest of the library uses: the length changed (false if it raised), new places
// filled, AS3's RangeError #1126 (true: the length is fixed) and #1125.
bool resize_vector(Library &p_library, Vector *p_vector, uint32_t p_length);
bool fill_zeroes(Library &p_library, Vector *p_vector, uint32_t p_from);
bool length_is_fixed(Library &p_library, const Vector *p_vector);
void raise_range(Library &p_library, double p_index, uint32_t p_length);

} // namespace vm
} // namespace kas3
