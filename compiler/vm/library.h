// AS3's standard library as functions of plain values: slots in, a slot out, no frame and no register.
// The interpreter runs them for its instructions, a call by name runs them for a method found by name, and
// native code calls them as they are. What a function needs beyond its values it is handed in a Library:
// the heap the values live on, and whoever runs the program, for the few things only that one can do.
#pragma once

#include "any.h"
#include "fields.h"
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
	// Runs a function of the program by its number, on its parameters as its registers hold them (a method's
	// object first, each parameter given by its kind, and past the last one that is not a rest parameter
	// the others untyped). They stay the caller's; one left out takes its default. What it gives is of the
	// function's result kind, and the caller's. False if it raised.
	[[nodiscard]] virtual bool run(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) = 0;
	// How many errors of scripts the engine ran have been carried back to the program so far: one more
	// after something asked of the engine says that what it answered is not to be used.
	virtual uint32_t carries() const = 0;
	// How many of the library's functions that put a vector into words are in progress, one inside another.
	uint32_t wording = 0;
};

// What the library's functions still ask of the interpreter itself: each is a part of the library that is
// not a function of plain values yet, and goes from here when it is. Where native code runs the program,
// these are its host's interpreter's, and an error one raises is the interpreter's until the host takes it.
struct Unmoved {
	virtual ~Unmoved() {}
	// One of Godot's Arrays or Dictionaries where an object of the class is wanted and the class is Array
	// or Dictionary: *r_taken, and a new one of what it holds in r_out (false if the engine refused).
	virtual bool object_of_engine(Slot p_value, uint32_t p_class, bool *r_taken, Slot *r_out) = 0;
	// Whether one of Godot's values is such an Array or Dictionary for such a class.
	virtual bool engine_is(Slot p_value, uint32_t p_class) = 0;
	// An Array's property under a number that names no element, read (into an empty slot) and written.
	virtual bool array_number_get(Vector *p_array, double p_number, Slot *r_out) = 0;
	virtual bool array_number_set(Vector *p_array, double p_number, Slot p_value) = 0;
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
	// AS3's Error #1023: calls, or what they put into words, nest deeper than they may or than the stack holds.
	void raise_overflow();
	// An Error that says the thing is not supported yet.
	void raise_unsupported(const std::string &p_what);
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

// A typed value as an untyped one with a reference of its own (undefined, with Error #1000 raised, when an
// Int64 finds no memory); a Number as AVM2 holds one untyped (an int where it fits, an Int64 up to 2^53).
Slot to_any(Library &p_library, ir::Kind p_kind, Slot p_value);
Slot any_number(Library &p_library, double p_value);
Slot any_wide(Library &p_library, int64_t p_value);
// An untyped value as a store into a place of the kind makes it: AS3's coercion, checked against the class
// (or, for one of Godot's, the engine name) p_check unless that is NONE. The value made has a reference
// of its own. False if it raised; coercion_failed() raises AS3's TypeError #1034 for it, and gives false.
[[nodiscard]] bool from_any(Library &p_library, Slot p_value, ir::Kind p_kind, uint32_t p_check, Slot *r_out);
bool coercion_failed(Library &p_library, Slot p_value, ir::Kind p_kind, uint32_t p_check);
// `is` on an untyped value: of a class, or one of the ANY_IS_ tests of the value itself (5.0 is an int).
bool any_is(Library &p_library, Slot p_value, uint32_t p_type);
// Whether an object (null: no) is of the class, or of one that extends or implements it.
bool is_class(const ir::Module &p_module, const Object *p_object, uint32_t p_class);

// New: an object of the class, its fields as they are before its constructor runs (a Number not yet a
// number); for a script class that extends one of Godot's, with its Godot object made. TableNew: an Object
// or a Dictionary of the class, with nothing in it. One reference, the caller's; null if it raised.
Object *object_new(Library &p_library, uint32_t p_class);
Object *table_new(Library &p_library, uint32_t p_class, bool p_weak);

// VecCall: one of a vector's or an Array's methods, on the vector and then what the method takes. r_out
// may be null.
[[nodiscard]] bool vector_call(Library &p_library, ir::VectorMethod p_method, const Slot *p_values, size_t p_count, Slot *r_out);
// A vector's element at an index held as an int, a uint or a Number (p_index_kind), read with a reference
// of its own and written; its length set. The vector is not null. False if it raised.
bool vector_get(Library &p_library, Vector *p_vector, ir::Kind p_index_kind, Slot p_index, Slot *r_out);
bool vector_set(Library &p_library, Vector *p_vector, ir::Kind p_index_kind, Slot p_index, Slot p_value);
bool vector_set_length(Library &p_library, Vector *p_vector, uint32_t p_length);
// What a place of a vector holds, as a value of its element's kind with a reference of its own: an Array's
// hole as undefined, a counted value counted once more, and one of Godot's values that the place holds whole
// (ir::Class::element_bare) as a new handle on a copy of it. False, with the error raised, where the engine
// can make no handle.
// p_place is the place as a slot holds it (place(), runtime.h).
[[nodiscard]] bool read_element(Library &p_library, const ir::Class &p_class, Slot p_place, Slot *r_value);
// A value of the element's kind stored in a place of the vector, by the place's index (which is one the
// vector has). write_element() takes a reference of its own to a value that stays the caller's;
// put_element() takes the caller's over. What the place held is let go of. False, with AS3's TypeError
// #1034 raised and the place as it was, for no value (null) where the place holds one whole;
// put_element() has let go of what it was given all the same.
[[nodiscard]] bool write_element(Library &p_library, Vector *p_vector, uint32_t p_index, Slot p_value);
[[nodiscard]] bool put_element(Library &p_library, Vector *p_vector, uint32_t p_index, Slot p_owned);
// What a place that holds nothing yet comes to hold for a value of the element's kind, with a reference of
// its own where one is counted: for set_place(), once every value to be stored has passed. False, with
// TypeError #1034 raised, for no value where a place holds one whole.
[[nodiscard]] bool place_for(Library &p_library, const ir::Class &p_class, Slot p_value, Slot *r_place);
// The same of an untyped value: what a place holds as one with a reference of its own, and one stored into
// a place as a store into the vector converts it (from_any(), by the class's check). False if it raised.
[[nodiscard]] bool element_to_any(Library &p_library, const ir::Class &p_class, Slot p_place, Slot *r_any);
[[nodiscard]] bool element_from_any(Library &p_library, Vector *p_vector, uint32_t p_index, Slot p_any);
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

// A function value called with untyped arguments, which stay the caller's: each as the parameter it goes to
// holds it, one left out its default, too many or too few AS3's ArgumentError #1063. What it gives,
// untyped, is the caller's to let go of. False if it raised.
[[nodiscard]] bool function_call(Library &p_library, Object *p_function, const Slot *p_arguments, size_t p_count, Slot *r_result);
// A function value: the function, and what it is called with first (a method's object, a closure's
// environment, or nothing). A function that takes nothing first has one value, the same each time, as AVM2
// gives one for a static method: adding and removing it as a listener then match. One reference, the
// caller's; null if it raised. make_method(): a method taken from an object by its number in the class's
// table, the object's class's own, bound to it (#1009 for no object).
Object *make_function(Library &p_library, uint32_t p_function, Object *p_first);
Object *make_method(Library &p_library, uint32_t p_method, Object *p_object);
// A function value's `length`: the parameters it declares, without what it takes first or `...rest`.
int32_t function_length(const ir::Module &p_module, Object *p_function);
// The function value a register of the kind holds. Null, with AS3's TypeError raised, for anything else:
// #1009 for nothing, #1006 for a value that is no function.
Object *function_in(Library &p_library, ir::Kind p_kind, Slot p_value);
// What such a call starts with, written to r_values (room for one more than p_count): what the function
// takes first, if anything, then each argument with a reference of its own. How many were written, or
// nothing if it raised. let_go_arguments() gives those references up once the callee has its own.
std::optional<size_t> function_arguments(Library &p_library, uint32_t p_target, Object *p_first, size_t p_count, const Slot *p_values, Slot *r_values);
void let_go_arguments(Library &p_library, const ir::Function &p_callee, Slot *p_values, size_t p_count);
// One untyped argument as the callee's parameter in that register holds it: AS3's coercion, the class
// checked for an object and the type for one of Godot's. False if it raised.
[[nodiscard]] bool argument_as(Library &p_library, const ir::Function &p_callee, uint32_t p_register, Slot p_value, Slot *r_out);
// The function a class runs for a method of that name, its own or inherited, or NONE.
uint32_t find_method(const Library &p_library, uint32_t p_class, std::string_view p_name);

// One of the untyped value's operators on untyped values (two, or one for AnyNeg). With one of Godot's
// values on a side and no object of the program's, Godot's own operator where it has one for what they hold,
// as typed code has it (Vector2 * 3, Vector2 < Vector2); otherwise as ECMAScript has them: AnyEq, AnyStrictEq,
// AnyLt, AnyLe, AnyGt and AnyGe give a Boolean, AnyAdd, AnySub, AnyMul, AnyDiv, AnyMod and AnyNeg an untyped
// value that is the caller's (any_operator_gives() says which).
[[nodiscard]] bool any_operate(Library &p_library, ir::Op p_op, const Slot *p_values, size_t p_count, Slot *r_out);
inline ir::Kind any_operator_gives(ir::Op p_op) {
	switch (p_op) {
		case ir::Op::AnyEq:
		case ir::Op::AnyStrictEq:
		case ir::Op::AnyLt:
		case ir::Op::AnyLe:
		case ir::Op::AnyGt:
		case ir::Op::AnyGe:
			return ir::Kind::Bool;
		default:
			return ir::Kind::Any;
	}
}
// Whether a vector's element of the kind is the value looked for, as `===` has it.
bool element_is(Library &p_library, ir::Kind p_kind, Slot p_element, Slot p_value);
// Two function values of one method on one object are equal, as AVM2 has it (`f == obj.method`).
bool same_function(const ir::Module &p_module, const Object *p_a, const Object *p_b);

// String(x) of an untyped value, added to r_text: an object's toString() is the program's, and Godot's
// text for one of its own can run a script. False if it raised.
[[nodiscard]] bool any_text(Library &p_library, Slot p_value, std::string *r_text);
// A vector's elements as text with the separator between them, as join() and toString() give it, or with
// p_locale as toLocaleString() does.
[[nodiscard]] bool vector_text(Library &p_library, Vector *p_vector, std::string_view p_separator, std::string *r_text, bool p_locale = false);
// A value of the kind as toLocaleString() asks for it, added to r_text: a vector, or a Date, in its own
// locale words; anything else as its text.
[[nodiscard]] bool locale_text(Library &p_library, ir::Kind p_kind, Slot p_value, std::string *r_text);
// An untyped value as a primitive (ECMAScript's ToPrimitive; with p_number, where a Number is wanted),
// with a reference of its own: an object's valueOf() or its text. False if it raised.
[[nodiscard]] bool any_primitive(Library &p_library, Slot p_value, Slot *r_out, bool p_number = false);
// Godot's text for one of its values or objects (null for no handle), added to r_text.
[[nodiscard]] bool engine_text(Library &p_library, void *p_handle, std::string *r_text);
// What an object with no toString() of its own says: "[object Name]", a Date's or a RegExp's words, an
// Error's heading ("Name: message"); a class as `[class Name]` names it, without its package.
std::string object_text(const Library &p_library, Object *p_object);
std::string error_heading(const ir::Module &p_module, const Object *p_error);
std::string class_name(const ir::Module &p_module, uint32_t p_code);
// A Date's text of that kind, in the host's local time.
std::string date_text(const Library &p_library, const Object *p_date, ir::DateText p_which);
// The object an untyped value holds, or null; a freed Godot object as null, anything else as it is.
inline Object *held_object(Slot p_value) {
	return any::tag(p_value) == AnyTag::Object ? static_cast<Object *>(any::pointer(p_value)) : nullptr;
}
Slot present_or_null(const Library &p_library, Slot p_value);
// AS3's ArgumentError #1063, for a function called with a number of arguments it does not take.
void raise_argument_count(Library &p_library, const std::string &p_name, size_t p_expected, size_t p_given);

} // namespace vm
} // namespace kas3
