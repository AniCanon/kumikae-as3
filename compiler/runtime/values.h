// What generated code holds: the runtime's own values (vm/runtime.h), the context every generated function
// takes first, and the one place counts change.
#pragma once

#include "any.h"
#include "runtime.h"

#include <atomic>
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
struct Api; // runtime/api.h
struct EngineApi; // runtime/engine.h
struct Resolved;
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
	// Where the thread's stack is nearly used up, as an address: a function whose frame is below it does
	// not go on (stack_low). 0 where the host does not know the stack's size.
	uintptr_t stack_floor = 0;
	StackFrame *frames = nullptr; // The innermost of them, where they are kept.
	Slot *globals = nullptr; // Static fields and package variables, by the module's numbers.
	Str *const *strings = nullptr; // The program's string constants.
	Str *null_text = nullptr; // "null": what a missing String reads as.
	uint8_t *class_used = nullptr; // Per class: its static fields have been (or are being) given values.
	const ClassInfo *classes = nullptr; // The program's, here too: a call through a method table starts from it.
	const Program *program = nullptr;
	const Api *api = nullptr; // The runtime's functions, as a program library reaches them.
	// The engine, where the host has one (runtime/engine.h): its entry points, what the host worked out
	// for each place the program asks it for something (never null: empty entries with no engine), and
	// Godot's names for the Strings the program gives where a name is taken, each built once.
	// How many script instances the engine has freed: a function that keeps its script's own Godot object
	// looks it up again when this has moved (engine.h, owner_object). Counted from any thread: Godot may
	// free an object on one of its workers.
	std::atomic<uint32_t> freed{ 0 };
	// A function the engine called failed: its error may be carried for a call generated code made on bare
	// values, which asks (engine.h, call_failed) and is given it.
	bool listener_failed = false;
	const EngineApi *engine = nullptr;
	const Resolved *resolved = nullptr;
	const void *const *names = nullptr;
	Host *host = nullptr;
};

// Whether the function this is written in has its frame below the context's floor: the C++ stack is nearly
// used up, which is Error #1023 like calls nested too deep, never a crash. Always inline: it is the frame of
// the function that asks.
__attribute__((always_inline)) inline bool stack_low(const Context *c) {
	return reinterpret_cast<uintptr_t>(__builtin_frame_address(0)) < c->stack_floor;
}

} // namespace rt
} // namespace kas3

#include "api.h"

namespace kas3 {
namespace rt {

// A slot with nothing in it. (`Slot{}` clears the union's first member only: half of a pointer.)
inline Slot empty() {
	Slot slot;
	slot.bits = 0;
	return slot;
}

// An object whose last reference went: what it holds is let go of, and it is freed.
//     void free_object(Context *c, Object *p_object);
// The same for what an untyped value holds, if it holds something counted.
//     void release_any_slow(Context *c, Slot p_value);

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
// An untyped value with a reference of its own: what it holds counted once more, or, for one of Godot's,
// another handle on it.
//     Slot shared_any(Context *c, Slot p_any);
// A value as another holder takes it: the same String or object, counted once more.
inline Str *shared(Context *c, Str *p_string) {
	retain(c, p_string);
	return p_string;
}
inline Object *shared(Context *c, Object *p_object) {
	retain(c, p_object);
	return p_object;
}
inline Slot shared(Context *c, Slot p_any) {
	return shared_any(c, p_any);
}
inline void release(Context *c, Str *p_string) {
	if (p_string && --p_string->references == 0) {
		free_string(c, p_string);
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
	put<T>(c, r_place, shared(c, value));
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
inline Object *object_of(Context *c, Slot p_any) {
	return static_cast<Object *>(any_pointer(c, p_any));
}

// A typed field of an object, for generated code, which knows the object's class as it is written: by the
// slot the class's description gives the field (FieldInfo::offset says the same place in bytes; vm/fields.h
// is the same rule for everything that has the class only as the program runs).
template <typename T>
inline T &field(Object *p_object, uint32_t p_slot) {
	return *reinterpret_cast<T *>(&vm::slot_at(p_object, p_slot));
}

} // namespace rt
} // namespace kas3
