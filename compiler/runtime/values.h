// What generated code holds: the runtime's own values (vm/runtime.h), the context every generated function
// takes first, and the one place counts change.
#pragma once

#include "any.h"
#include "named_site.h"
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

// Objects let go of, kept to be the next ones made (rule 14: objects from free lists by size): for each
// number of slots (an object's fields, and the room after them where the host remembers what objects are
// constructed with), a list of blocks through `gc_next`. Only objects of classes that hold nothing counted
// and that the collector does not look at are kept (Context::class_cached), so one let go of has nothing
// to give up; each block is the heap's own, and goes back to it when the host ends.
struct ObjectCache {
	static constexpr uint32_t SIZES = 33; // Objects of up to 32 fields.
	static constexpr uint32_t MOST = 8192; // Blocks kept of one size.
	Object *spare[SIZES] = {};
	uint32_t count[SIZES] = {};
};

// One per thread, the host's. Generated code keeps no state of its own: what changes as a program runs is
// reached through this. Main-thread hot fields are packed first; cross-thread atomics written by the time
// limit watch or Godot worker threads sit on their own cache line to prevent false sharing.
struct alignas(64) Context {
	bool failing = false; // An error is on its way out: `pending` holds it.
	// The host remembers what objects are constructed with (vm/constructions.h): a program library built
	// to tell says so as a constructor begins (constructing).
	bool remembers = false;
	// A function the engine called failed: its error may be carried for a call generated code made on bare
	// values, which asks (engine.h, call_failed) and is given it.
	bool listener_failed = false;
	uint32_t depth = 0; // Generated functions in progress.
	Slot pending{}; // What was thrown, untyped, with a reference of its own.
	StackFrame *frames = nullptr; // The innermost of them, where they are kept.
	Slot *globals = nullptr; // Static fields and package variables, by the module's numbers.
	Str *const *strings = nullptr; // The program's string constants.
	Str *null_text = nullptr; // "null": what a missing String reads as.
	uint8_t *class_used = nullptr; // Per class: its static fields have been (or are being) given values.
	const ClassInfo *classes = nullptr; // The program's, here too: a call through a method table starts from it.
	const Program *program = nullptr;
	const Api *api = nullptr; // The runtime's functions, as a program library reaches them.
	// How many of Godot's objects that the host found behind a handle (HandleHead::object) Godot has freed
	// since: the host's own count, which moves from any thread. Null with no engine.
	const std::atomic<uint64_t> *gone = nullptr;
	// The engine, where the host has one (runtime/engine.h): its entry points, what the host worked out
	// for each place the program asks it for something (never null: empty entries with no engine), and
	// Godot's names for the Strings the program gives where a name is taken, each built once.
	const EngineApi *engine = nullptr;
	// Generated code advances the epoch when it changes a script resource directly.
	uint64_t *script_declarations = nullptr;
	const Resolved *resolved = nullptr;
	const void *const *names = nullptr;
	Host *host = nullptr;
	// The host's cache of objects, and for each class 0, or one more than its number of slots if its
	// objects are kept there.
	ObjectCache *cache = nullptr;
	const uint8_t *class_cached = nullptr;
	const uint32_t *class_numbers = nullptr; // For such a class: bit i says its field i is a Number, which starts NaN.
	// For such a class, and for one whose objects' records generated code keeps: how many of its slots
	// are its fields. Any after them are the room where the host remembers what the object was constructed
	// with (vm/constructions.h), whose first slot says whether anything is remembered.
	const uint8_t *class_fields = nullptr;
	// For each class: not 0 once the host has been told of one of its objects being constructed, and
	// keeps of each its arguments as they come and nothing more. The next are remembered where they are
	// made (remembered_here), with no word to the host.
	const uint8_t *class_tells = nullptr;
	// Set by the host's watch on how long a call from outside has run, from a thread of its own
	// (runtime/time_limit.h): a function reads it once in ROUNDS times round its loops (alarmed), and asks
	// overdue() when it is set.
	alignas(64) std::atomic<bool> alarm{ false };
	// How many script instances the engine has freed: a function that keeps its script's own Godot object
	// looks it up again when this has moved (engine.h, owner_object). Counted from any thread: Godot may
	// free an object on one of its workers.
	std::atomic<uint32_t> freed{ 0 };
	// Where the thread's stack is nearly used up, as an address: a function whose frame is below it does
	// not go on (stack_low). 0 where the host does not know the stack's size. The watch puts it above
	// every frame to have the clock looked at where a function is entered, so it is read as it stands.
	std::atomic<uintptr_t> stack_floor{ 0 };
};

// What the first slot of an object's record says (vm::Constructions::Record, whose numbers these are):
// nothing is remembered of the object, or its constructor's arguments are.
constexpr uint32_t RECORD_NOTHING = 0;
constexpr uint32_t RECORD_ARGUMENTS = 1;

// Whether the function this is written in has its frame below the context's floor: the C++ stack is nearly
// used up, which is Error #1023 like calls nested too deep, never a crash. Always inline: it is the frame of
// the function that asks.
__attribute__((always_inline)) inline bool stack_low(const Context *c) {
	return reinterpret_cast<uintptr_t>(__builtin_frame_address(0)) < c->stack_floor.load(std::memory_order_relaxed);
}

// How many times a function goes round its loops between two looks at the alarm: it counts them in a
// local, so that a loop stays one the C++ compiler can count the rounds of (and do several of at once),
// and the alarm's byte is read once for all of them. As many as the interpreter runs statements between
// two looks at its clock.
constexpr uint32_t ROUNDS = 4096;

// Where a counted loop's counter is held to before the next look at the alarm (a stretch's end): ROUNDS
// steps on, no further than the loop's own end, and no further than the last value from which one more
// step stays in the counter's type (p_edge is the type's greatest value, or its least for a counter that
// goes down). A counter short of it has that many rounds to go at most, and the loop's own test holds. No
// stretch (NO_STRETCH_UP, which no counter is short of) with a step that is nothing or goes the other way,
// and where the next step would wrap.
constexpr int64_t NO_STRETCH_UP = INT64_MIN;
constexpr int64_t NO_STRETCH_DOWN = INT64_MAX;
inline int64_t stretch_up(int64_t p_counter, int64_t p_end, int64_t p_step, int64_t p_edge) {
	const int64_t whole = p_edge + 1 - p_step;
	if (p_step <= 0 || p_counter >= whole) {
		return NO_STRETCH_UP;
	}
	const int64_t far = p_counter + p_step * static_cast<int64_t>(ROUNDS);
	const int64_t near = far < p_end ? far : p_end;
	return near < whole ? near : whole;
}
inline int64_t stretch_down(int64_t p_counter, int64_t p_end, int64_t p_step, int64_t p_edge) {
	const int64_t whole = p_edge - 1 + p_step;
	if (p_step <= 0 || p_counter <= whole) {
		return NO_STRETCH_DOWN;
	}
	const int64_t far = p_counter - p_step * static_cast<int64_t>(ROUNDS);
	const int64_t near = far > p_end ? far : p_end;
	return near > whole ? near : whole;
}
// The same for an int that counts up by 1 to an end no int is past: no step wraps short of it, and there
// is always a stretch where the loop's own test holds.
inline int64_t stretch_near(int64_t p_counter, int64_t p_end) {
	const int64_t far = p_counter + static_cast<int64_t>(ROUNDS);
	return far < p_end ? far : p_end;
}
// And for an int that is a vector's index, by any step: with a step that is nothing or goes the other way,
// or a counter below 0, no index is short of what this gives, though it is not 0. A step that wraps leaves
// a counter below 0, which is short of nothing.
inline uint32_t stretch_index_near(int64_t p_counter, int64_t p_end, int64_t p_step) {
	const int64_t far = p_counter + p_step * static_cast<int64_t>(ROUNDS);
	const int64_t near = far < p_end ? far : p_end;
	return static_cast<uint32_t>(near > 0 ? near : 0);
}
// The same for a counter that goes up as the index of a vector, the stretch's end an index too: 0, which
// no index is short of, where there is no stretch, for a counter below 0 and for one at the loop's end.
inline uint32_t stretch_index(int64_t p_counter, int64_t p_end, int64_t p_step, int64_t p_edge) {
	const int64_t whole = p_edge + 1 - p_step;
	if (p_counter < 0 || p_step <= 0 || p_counter >= p_end || p_counter >= whole) {
		return 0;
	}
	const int64_t limit = p_end < whole ? p_end : whole;
	const int64_t most = p_step * static_cast<int64_t>(ROUNDS);
	const uint32_t out = static_cast<uint32_t>(limit - p_counter > most ? p_counter + most : limit);
	// Said outright, for the C++ compiler: an index short of the stretch's end is short of the loop's.
	if (static_cast<int64_t>(out) > p_end) {
		__builtin_unreachable();
	}
	return out;
}

// Whether the host's watch asks for the clock to be looked at.
inline bool alarmed(const Context *c) {
	return KAS3_UNLIKELY(c->alarm.load(std::memory_order_relaxed));
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
	return vm::any::holds(p_any) ? shared_any(c, p_any) : p_any; // A number, a Boolean or nothing: itself.
}
inline void release(Context *c, Str *p_string) {
	if (p_string && --p_string->references == 0) {
		free_string(c, p_string);
	}
}
inline void release(Context *c, Object *p_object) {
	if (p_object && --p_object->references == 0) {
		const uint32_t size = c->class_cached[p_object->klass];
		ObjectCache *const cache = c->cache;
		if (size && cache->count[size] < ObjectCache::MOST) {
			p_object->gc_next = cache->spare[size];
			cache->spare[size] = p_object;
			cache->count[size]++;
		} else {
			free_object(c, p_object);
		}
	}
}
inline void release(Context *c, Slot p_any) {
	if (vm::any::holds(p_any)) {
		release_any_slow(c, p_any);
	}
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
