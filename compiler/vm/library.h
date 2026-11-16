// AS3's standard library as functions of plain values: slots in, a slot out, no frame and no register.
// The interpreter runs them for its instructions, a call by name runs them for a method found by name, and
// native code calls them as they are. What a function needs beyond its values it is handed in a Library:
// the heap the values live on, and whoever runs the program, for the four things only that one can do.
#pragma once

#include "heap.h"
#include "ir.h"
#include "runtime.h"

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
	// Calls a function value with untyped arguments, which stay the caller's. What it gives, untyped, is
	// the caller's to let go of. False if it raised, or if the value is no function (raised too).
	[[nodiscard]] virtual bool call(Object *p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) = 0;
	// String(x) of an untyped value: an object's toString() is the program's. False if it raised.
	[[nodiscard]] virtual bool text(Slot p_value, std::string *r_text) = 0;
};

struct Library {
	Heap &heap;
	Runner &runner;

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
// whole, with a reference that is the caller's. False if it raised: nothing is written.

// A String's methods that may run the program or raise (those ir::reads_text_only() leaves out): split()
// by a delimiter that is not a RegExp, as StrCall lists its values (the String, the delimiter untyped,
// the most pieces as a Number), and the URI functions (the text, where null reads "null").
[[nodiscard]] bool text_call(Library &p_library, ir::StringFunction p_function, const Slot *p_values, size_t p_count, Slot *r_out);

} // namespace vm
} // namespace kas3
