// Godot as generated code reaches it, with no header of Godot's: handles on its objects and values (the
// host's, as the interpreter holds them), the values with parts held inline (godot_values.h) and boxed into
// a handle where one must be shared, and what the host worked out once for each place the program asks
// the engine for something, with the few entry points needed to use it.
#pragma once

#include "godot_values.h"
#include "values.h"

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
};

// What the host worked out for one place the program asks the engine for something (Program::uses). Empty
// where it could not, or where the place is not one generated code calls directly: the library's way,
// through its thunk, is then the only one.
struct Resolved {
	// The method bind of a method or of a property's accessor, callable on bare values; or, for one of
	// Godot's value types, its own code for an operator, a method or a part (operate() and the rest, below).
	void *bind = nullptr;
	void *object = nullptr; // A singleton's object.
	// A handle on a singleton, or on a constant's value: the host's own, borrowed, never let go of by
	// generated code.
	void *handle = nullptr;
	uint64_t defaults[2] = { 0, 0 }; // The parameters the call leaves out, as Godot declares them, bare.
};

//     void *copy_handle(Context *c, void *p_handle);
//     void drop_handle(Context *c, void *p_handle);

inline Ref shared(Context *c, Ref p_value) {
	return Ref{ p_value.handle ? copy_handle(c, p_value.handle) : nullptr };
}
inline Val shared(Context *c, Val p_value) {
	return Val{ p_value.handle ? copy_handle(c, p_value.handle) : nullptr };
}
inline void release(Context *c, Ref p_value) {
	if (p_value.handle) {
		drop_handle(c, p_value.handle);
	}
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
	store_slot(c, r_place, Ref{ p_borrowed.handle });
}

// The object behind a handle, for a call on bare values: null with no engine, no handle or a freed object.
inline void *object_of(const Context *c, Ref p_value) {
	return c->engine && p_value.handle ? c->engine->object(p_value.handle) : nullptr;
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
		c->engine->unbox(p_handle, PartsOf<T>::code, &value);
	}
	return value;
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

} // namespace rt
} // namespace kas3
