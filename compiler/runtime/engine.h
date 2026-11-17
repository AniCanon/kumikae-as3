// Godot as generated code reaches it, with no header of Godot's: handles on its objects and values (the
// host's, as the interpreter holds them), the values with parts held inline (godot_values.h) and boxed into
// a handle where one must be shared, and what the host worked out once for each place the program asks
// the engine for something, with the few entry points needed to use it.
#pragma once

#include "godot_values.h"
#include "values.h"

#include <cstring>

namespace kas3 {
namespace rt {

// A handle on one of Godot's objects, or null: "freed is null" is the host's rule, asked each time.
struct Ref {
	void *handle = nullptr;
};
// A handle on one of Godot's values that is not held inline (a String, an Array, a Callable, any value).
struct Val {
	void *handle = nullptr;
};

// What the host keeps just before every handle it makes: what generated code reads of a handle on one of
// Godot's objects with no call. (A value lent for a call, on the caller's stack, has none: only a handle on
// an object is ever looked at this way, and every one of those is the host's.)
struct HandleHead {
	// How many hold the handle. One on an object that is not reference-counted is shared, never copied:
	// another holder is one more here (HANDLE_SHARED, once the host has looked at what it is on). A handle
	// on anything else is the host's to copy (copy_handle).
	std::atomic<uint32_t> holders;
	uint8_t shared;
	uint8_t asked; // The host has been asked for the object behind it before (EngineApi::object).
	uint16_t unused;
	// The object behind the handle, as the host found it when Context::gone read `seen`. The host is told of
	// the end of every object it has found this way, and counts it there: while the count reads the same,
	// none of them has been freed, so this one is still there. 0: not found yet.
	uint64_t seen;
	void *object;
	uint64_t unused_too; // The handle after it is sixteen bytes on, as an untyped value's tag needs.
};
inline constexpr uint8_t HANDLE_UNKNOWN = 0;
inline constexpr uint8_t HANDLE_SHARED = 1;
inline constexpr uint8_t HANDLE_COPIED = 2;
inline HandleHead *head_of(void *p_handle) {
	return static_cast<HandleHead *>(p_handle) - 1;
}

// The engine's entry points, given by the host that has an engine; a host with none gives no table.
struct EngineApi {
	// Godot's call of a method bind on bare values (object_method_bind_ptrcall): each argument by its
	// address, as its type is held bare; the result written where r_result points.
	void (*call)(const void *p_bind, void *p_object, const void *const *p_arguments, void *r_result);
	// The object behind a handle: null for no handle, and for an object Godot has freed.
	void *(*object)(void *p_handle);
	// A value with parts as a new handle, the caller's; and such a handle's value, copied out (its zero
	// for no handle or a value of another type).
	void *(*box)(Parts p_type, const void *p_value);
	void (*unbox)(const void *p_handle, Parts p_type, void *r_value);
	// A signal's emit() on values held as generated code holds them: `p_emitter` is what the host worked out
	// for the place (Resolved::bind), `p_signal` the handle on the signal, and each value is given as its
	// code in `p_held` says (GIVEN_WHOLE and the rest). False, with nothing done, where the handle holds no
	// signal or the signal's object is gone: the instruction's thunk then says what that is.
	bool (*emit)(void *p_emitter, void *p_signal, const uint8_t *p_held, const void *const *p_values, uint32_t p_count);
	// For each value with parts: where a handle on one keeps it, in bytes from the handle's start (0 for a
	// type a handle keeps elsewhere, and for every type where the host could not make sure), and the number
	// a handle on one starts with. Found by the host as it starts, from what Godot itself says of a handle
	// (unbox() reads a value there; anything else is the host's unbox).
	uint8_t value_at[static_cast<size_t>(Parts::Count)];
	int32_t value_type[static_cast<size_t>(Parts::Count)];
	// `call`, for a method that gives one of Godot's values that a handle keeps (a Dictionary, a RID): the
	// value is written where a new handle keeps it, and the handle is the caller's. `p_zero` is the handle
	// the host keeps on such a value for the place (Resolved::handle), which the new one starts as.
	void *(*call_kept)(const void *p_bind, void *p_object, const void *const *p_arguments, const void *p_zero);
	// What a Dictionary in a handle has under a key that is text the program wrote (`p_name`: the name the
	// host built for it, Context::names), where that is a value with parts of the type asked for: in a new
	// handle, the caller's. Null for anything else (no Dictionary, no such key, a value of another type).
	void *(*keyed)(const void *p_handle, const void *p_name, Parts p_type);
};

// How a value handed to EngineApi::emit is held: a whole number as an int64_t, a Number as a double, a
// Boolean as a byte, each by its address; one of Godot's values or objects behind a handle as the handle
// itself (null for none); a value with parts as its struct, by its address (given_parts()).
inline constexpr uint8_t GIVEN_WHOLE = 0;
inline constexpr uint8_t GIVEN_NUMBER = 1;
inline constexpr uint8_t GIVEN_FLAG = 2;
inline constexpr uint8_t GIVEN_HANDLE = 3;
inline constexpr uint8_t GIVEN_PARTS = 4; // And the number of the type (Parts) added to it.

// What the host worked out for one place the program asks the engine for something (Program::uses). Empty
// where it could not, or where the place is not one generated code calls directly: the library's way,
// through its thunk, is then the only one.
struct Resolved {
	// The method bind of a method or of a property's accessor, callable on bare values; or, for one of
	// Godot's value types, its own code for an operator, a method or a part (operate() and the rest, below).
	void *bind = nullptr;
	void *object = nullptr; // A singleton's object.
	// A handle on a singleton, on a constant's value, or on the zero of the type a method gives in a handle
	// (EngineApi::call_kept): the host's own, borrowed, never let go of by generated code.
	void *handle = nullptr;
	uint64_t defaults[2] = { 0, 0 }; // The parameters the call leaves out, as Godot declares them, bare.
	// For a method of one of Godot's values that a handle keeps (a Dictionary's `is_empty`): the number a
	// handle on such a value starts with, and where the handle keeps the value, in bytes from its start
	// (value_in()). 0 where the host could not make sure of either.
	int32_t type = 0;
	uint32_t at = 0;
};

//     void *copy_handle(Context *c, void *p_handle);
//     void drop_handle(Context *c, void *p_handle);

inline Ref shared(Context *c, Ref p_value) {
	if (!p_value.handle) {
		return Ref{};
	}
	// A handle on an object that is not reference-counted is held by one more: no other handle is made.
	HandleHead *const head = head_of(p_value.handle);
	if (KAS3_LIKELY(head->shared == HANDLE_SHARED)) {
		head->holders.fetch_add(1, std::memory_order_relaxed);
		return p_value;
	}
	return Ref{ copy_handle(c, p_value.handle) };
}
inline Val shared(Context *c, Val p_value) {
	return Val{ p_value.handle ? copy_handle(c, p_value.handle) : nullptr };
}
inline void release(Context *c, Ref p_value) {
	if (!p_value.handle) {
		return;
	}
	// One holder fewer of a shared handle that has others; the last one's is the host's to free.
	HandleHead *const head = head_of(p_value.handle);
	uint32_t holders = head->holders.load(std::memory_order_relaxed);
	if (head->shared == HANDLE_SHARED && holders > 1 &&
			head->holders.compare_exchange_strong(holders, holders - 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
		return;
	}
	drop_handle(c, p_value.handle);
}
inline void release(Context *c, Val p_value) {
	if (p_value.handle) {
		drop_handle(c, p_value.handle);
	}
}
inline Slot slot(Ref p_value) {
	return slot(p_value.handle);
}
inline Slot slot(Val p_value) {
	return slot(p_value.handle);
}
inline void store_slot(Context *c, Slot &r_place, Ref p_borrowed) {
	void *const old = r_place.p;
	r_place.p = shared(c, p_borrowed).handle;
	release(c, Ref{ old });
}
inline void store_slot(Context *c, Slot &r_place, Val p_borrowed) {
	void *const old = r_place.p;
	r_place.p = shared(c, p_borrowed).handle;
	release(c, Val{ old });
}

// The object behind a handle, for a call on bare values: null with no engine, no handle or a freed object.
// The one the host found for the handle before, while Godot has freed none of the objects the host found
// (HandleHead::seen); the host's look by the object's id otherwise, which is where a freed one is null.
inline void *object_of(const Context *c, Ref p_value) {
	if (!c->engine || !p_value.handle) {
		return nullptr;
	}
	const HandleHead *const head = head_of(p_value.handle);
	if (KAS3_LIKELY(head->seen == c->gone->load(std::memory_order_relaxed))) {
		return head->object;
	}
	return c->engine->object(p_value.handle);
}

// A register given the handle a field holds, where it may have one on the same object already: the field
// of a script read again each time round a loop to call the object in it. A register that has the very
// handle (one that is shared, which the register's own count keeps) or another handle on the same object,
// which Godot still has, keeps what it has: it holds that object either way, with a reference of its own
// where Godot counts them, and no handle is made or let go of. The object behind each handle is the one
// the host found for it (object_of), asked for once and read from the handle after that; a handle on none
// or on an object Godot has freed is never the same as another.
inline void store_again(Context *c, Ref &r_register, Ref p_borrowed) {
	if (r_register.handle && p_borrowed.handle) {
		if (r_register.handle == p_borrowed.handle) {
			return;
		}
		void *const there = object_of(c, p_borrowed);
		if (there && there == object_of(c, r_register)) {
			return;
		}
	}
	store(c, r_register, p_borrowed);
}

// The script's own Godot object, which a function finds once: it is the same for as long as no script
// instance has been freed, and is looked up again (null, if it was this one's) when one has.
inline void *owner_object(const Context *c, Object *p_this, void *&r_found, uint32_t &r_seen) {
	const uint32_t now = c->freed.load(std::memory_order_relaxed);
	if (KAS3_UNLIKELY(now != r_seen)) {
		r_found = object_of(c, field<Ref>(p_this, 0));
		r_seen = now;
	}
	return r_found;
}

// After a call of Godot's that generated code made on bare values: whether a listener the call ran raised
// an error that is this function's now (c->failing), as it is when the library makes the call.
//     bool call_failed(Context *c);

// Godot's own code for one of its value types, as the host found it (Resolved::bind): an operator's
// evaluator, a method of the type, the getter or the setter of a part. Each takes bare values by their
// addresses, as the type is held bare, and writes what it gives the same way.
inline void operate(void *p_evaluator, const void *p_left, const void *p_right, void *r_result) {
	reinterpret_cast<void (*)(const void *, const void *, void *)>(p_evaluator)(p_left, p_right, r_result);
}
inline void call_part(void *p_method, void *p_on, const void *const *p_arguments, void *r_result, int p_count) {
	reinterpret_cast<void (*)(void *, const void *const *, void *, int)>(p_method)(p_on, p_arguments, r_result, p_count);
}
inline void get_part(void *p_getter, const void *p_of, void *r_value) {
	reinterpret_cast<void (*)(const void *, void *)>(p_getter)(p_of, r_value);
}
inline void set_part(void *p_setter, void *p_of, const void *p_value) {
	reinterpret_cast<void (*)(void *, const void *)>(p_setter)(p_of, p_value);
}

// Where a handle keeps its value, for a method of the value's type that the host found for a place: null
// for no handle, a handle on a value of another type, and a type the host could not place.
inline void *value_in(const Resolved &p_use, void *p_handle) {
	if (!p_handle || !p_use.at) {
		return nullptr;
	}
	int32_t held;
	std::memcpy(&held, p_handle, sizeof(held));
	return held == p_use.type ? static_cast<char *>(p_handle) + p_use.at : nullptr;
}

template <typename T>
struct PartsOf;
#define KAS3_PARTS(type) \
	template <> \
	struct PartsOf<type> { \
		static constexpr Parts code = Parts::type; \
	};
KAS3_PARTS(Vector2) KAS3_PARTS(Vector2i) KAS3_PARTS(Rect2) KAS3_PARTS(Rect2i) KAS3_PARTS(Vector3) KAS3_PARTS(Vector3i) KAS3_PARTS(Transform2D)
KAS3_PARTS(Vector4) KAS3_PARTS(Vector4i) KAS3_PARTS(Plane) KAS3_PARTS(Quaternion) KAS3_PARTS(AABB) KAS3_PARTS(Basis) KAS3_PARTS(Transform3D)
KAS3_PARTS(Projection) KAS3_PARTS(Color)
#undef KAS3_PARTS

template <typename T>
constexpr uint8_t given_parts() {
	return static_cast<uint8_t>(GIVEN_PARTS + static_cast<uint8_t>(PartsOf<T>::code));
}

// A value held inline as a handle of its own, and a handle's value copied out: where one crosses into what
// the host and the library hold (a field, an untyped value, an Array, an instruction the library runs).
template <typename T>
inline Val boxed(const Context *c, const T &p_value) {
	return Val{ c->engine ? c->engine->box(PartsOf<T>::code, &p_value) : nullptr };
}
template <typename T>
inline T unbox(const Context *c, const void *p_handle) {
	T value{};
	if (c->engine && p_handle) {
		// Read where the handle keeps it, if it is a handle on that type and the host found where.
		const size_t type = static_cast<size_t>(PartsOf<T>::code);
		const uint8_t at = c->engine->value_at[type];
		int32_t held;
		std::memcpy(&held, p_handle, sizeof(held));
		if (KAS3_LIKELY(at != 0 && held == c->engine->value_type[type])) {
			std::memcpy(&value, static_cast<const char *>(p_handle) + at, sizeof(T));
		} else {
			c->engine->unbox(p_handle, PartsOf<T>::code, &value);
		}
	}
	return value;
}
// One of Godot's values that a slot holds whole, read out of a vector's place and as such a place holds it
// (fast.h, vector_of): the bytes are the same, as Godot lays the type out.
template <typename T>
inline T place_value(const Slot &p_place) {
	static_assert(sizeof(T) == sizeof(Slot), "a slot holds the value whole");
	T value;
	std::memcpy(&value, &p_place, sizeof(T));
	return value;
}
template <typename T>
inline Slot value_place(const T &p_value) {
	static_assert(sizeof(T) == sizeof(Slot), "a slot holds the value whole");
	Slot place;
	std::memcpy(&place, &p_value, sizeof(T));
	return place;
}

// A value held inline, as a handle for as long as one instruction the library runs needs it; what the
// instruction leaves in the handle is the caller's to copy back out.
template <typename T>
struct Boxed {
	Context *c;
	Val v;
	Boxed(Context *p_context, const T &p_value) :
			c(p_context), v(boxed(p_context, p_value)) {}
	~Boxed() { release(c, v); }
	Boxed(const Boxed &) = delete;
	Boxed &operator=(const Boxed &) = delete;
};

// A handle lent by a field, as a handle of its own for as long as one instruction the library runs reads
// it: what the field holds may be let go of meanwhile.
struct Held {
	Context *c;
	Val v;
	Held(Context *p_context, Val p_lent) :
			c(p_context), v(shared(p_context, p_lent)) {}
	~Held() { release(c, v); }
	Held(const Held &) = delete;
	Held &operator=(const Held &) = delete;
};

} // namespace rt
} // namespace kas3
