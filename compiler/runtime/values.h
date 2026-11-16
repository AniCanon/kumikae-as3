// What generated code holds: the runtime's own values (vm/runtime.h), the context every generated function
// takes first, and the one place counts change.
#pragma once

#include "any.h"
#include "runtime.h"

#include <cstdint>

#define KAS3_UNLIKELY(condition) __builtin_expect(!!(condition), 0)
#define KAS3_LIKELY(condition) __builtin_expect(!!(condition), 1)

namespace kas3 {
namespace rt {

using vm::Object;
using vm::Slot;
using vm::Str;
using vm::Vector;

struct Program; // runtime/program.h
struct ClassInfo;
struct Host; // The host's own state: nothing generated code reads.

// Calls may nest this deep; one more is AS3's Error #1023, as in the interpreter.
constexpr uint32_t MAX_DEPTH = 10000;

// A generated function in progress, in a program that reads stack traces of errors nothing threw (an
// Error's getStackTrace()): the function, the line of the call it is making, and its caller's. Other
// programs keep none: a trace of an error on its way out is built as functions are left.
struct StackFrame {
	uint32_t function;
	uint32_t line;
	StackFrame *below;
};

// One per thread, the host's. Generated code keeps no state of its own: what changes as a program runs is
// reached through this.
struct Context {
	bool failing = false; // An error is on its way out: `pending` holds it.
	Slot pending{}; // What was thrown, untyped, with a reference of its own.
	uint32_t depth = 0; // Generated functions in progress.
	StackFrame *frames = nullptr; // The innermost of them, where they are kept.
	Slot *globals = nullptr; // Static fields and package variables, by the module's numbers.
	Str *const *strings = nullptr; // The program's string constants.
	Str *null_text = nullptr; // "null": what a missing String reads as.
	uint8_t *class_used = nullptr; // Per class: its static fields have been (or are being) given values.
	const ClassInfo *classes = nullptr; // The program's, here too: a call through a method table starts from it.
	const Program *program = nullptr;
	Host *host = nullptr;
};

// A slot with nothing in it. (`Slot{}` clears the union's first member only: half of a pointer.)
inline Slot empty() {
	Slot slot;
	slot.bits = 0;
	return slot;
}

// An object whose last reference went: what it holds is let go of, and it is freed.
void free_object(Context *c, Object *p_object);
// The same for what an untyped value holds, if it holds something counted.
void release_any_slow(Context *c, Slot p_value);

// Every change to a count in generated code is one of these, so that the rule (plain counts now) changes
// in one place.
inline void retain(Context *, Str *p_string) {
	if (p_string) {
		p_string->references++;
	}
}
inline void retain(Context *, Object *p_object) {
	if (p_object) {
		p_object->references++;
	}
}
inline void retain(Context *, Slot p_any) {
	vm::retain(vm::any::counted(p_any));
}
inline void release(Context *, Str *p_string) {
	if (p_string && --p_string->references == 0) {
		vm::free_string(p_string);
	}
}
inline void release(Context *c, Object *p_object) {
	if (p_object && --p_object->references == 0) {
		free_object(c, p_object);
	}
}
inline void release(Context *c, Slot p_any) {
	release_any_slow(c, p_any);
}

// A register, field or static field that holds a reference is written by one of these. put() takes over a
// reference the caller owns; store() takes one of its own to a value someone else holds. What was there
// is let go of last: until then it may be what keeps the new value alive.
template <typename T>
struct As {
	using type = T; // Names T without letting an argument decide it: `nullptr` then fits a pointer's place.
};
template <typename T>
inline void put(Context *c, T &r_place, typename As<T>::type p_owned) {
	const T old = r_place;
	r_place = p_owned;
	release(c, old);
}
template <typename T>
inline void store(Context *c, T &r_place, typename As<T>::type p_borrowed) {
	const T value = p_borrowed;
	retain(c, value);
	put<T>(c, r_place, value);
}
inline void store_slot(Context *c, Slot &r_place, Str *p_borrowed) {
	Str *const old = static_cast<Str *>(r_place.p);
	retain(c, p_borrowed);
	r_place.p = p_borrowed;
	release(c, old);
}
inline void store_slot(Context *c, Slot &r_place, Object *p_borrowed) {
	Object *const old = static_cast<Object *>(r_place.p);
	retain(c, p_borrowed);
	r_place.p = p_borrowed;
	release(c, old);
}
inline void store_slot(Context *c, Slot &r_place, Slot p_borrowed) {
	store(c, r_place, p_borrowed);
}

// A value as a whole slot, the rest of it clear: how a value is handed to the host.
inline Slot slot(int32_t p_value) {
	Slot out = empty();
	out.i = p_value;
	return out;
}
inline Slot slot(uint32_t p_value) {
	Slot out = empty();
	out.u = p_value;
	return out;
}
inline Slot slot(double p_value) {
	Slot out = empty();
	out.d = p_value;
	return out;
}
inline Slot slot(bool p_value) {
	Slot out = empty();
	out.b = p_value;
	return out;
}
inline Slot slot(int64_t p_value) {
	Slot out = empty();
	out.l = p_value;
	return out;
}
inline Slot slot(void *p_value) {
	Slot out = empty();
	out.p = p_value;
	return out;
}
inline Slot slot(Slot p_value) {
	return p_value;
}

// The object an untyped value holds, as it is held: for a catch that takes an object.
inline Object *object_of(Slot p_any) {
	return static_cast<Object *>(vm::any::pointer(p_any));
}

// A typed field of an object: the fields follow the header, eight bytes each (vm::Object::fields()).
template <typename T>
inline T &field(Object *p_object, uint32_t p_index) {
	return *reinterpret_cast<T *>(reinterpret_cast<char *>(p_object) + sizeof(Object) + 8 * static_cast<size_t>(p_index));
}

} // namespace rt
} // namespace kas3
