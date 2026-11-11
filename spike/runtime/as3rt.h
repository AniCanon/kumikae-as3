// The AS3-shaped runtime core: what compiled AS3 code would link against.
// Throwaway spike. Main thread only: nothing here is atomic or thread-safe.
#pragma once

#include <cstdint>

namespace as3 {

struct Object;

struct Class {
	const char *name;
	void (*destroy)(Object *);
};

// Every AS3 object starts with this header.
struct Object {
	uint32_t refcount;
	const Class *cls;
};

inline void retain(Object *p_object) {
	if (p_object) {
		p_object->refcount++;
	}
}

inline void release(Object *p_object) {
	if (p_object && --p_object->refcount == 0) {
		p_object->cls->destroy(p_object);
	}
}

// An owning reference. T must derive from Object.
template <class T>
class Ref {
	T *ptr = nullptr;

public:
	Ref() {}
	explicit Ref(T *p_ptr) :
			ptr(p_ptr) {
		retain(ptr);
	}
	Ref(const Ref &p_other) :
			ptr(p_other.ptr) {
		retain(ptr);
	}
	Ref(Ref &&p_other) :
			ptr(p_other.ptr) {
		p_other.ptr = nullptr;
	}
	~Ref() { release(ptr); }
	Ref &operator=(const Ref &p_other) {
		retain(p_other.ptr);
		release(ptr);
		ptr = p_other.ptr;
		return *this;
	}
	Ref &operator=(Ref &&p_other) {
		if (this != &p_other) {
			release(ptr);
			ptr = p_other.ptr;
			p_other.ptr = nullptr;
		}
		return *this;
	}
	T *get() const { return ptr; }
	T *operator->() const { return ptr; }
	explicit operator bool() const { return ptr != nullptr; }
};

// Errors travel as a pending flag checked after every call that can throw; there are no C++ exceptions.
struct Error {
	const char *type;
	const char *message;
};

extern Error *pending_error;

// Records the error and the stack trace of the throw site.
void throw_error(const char *p_type, const char *p_message);
void throw_null();
void throw_range();
void clear_error();
const char *pending_trace();

// The shadow stack that makes release-build stack traces possible.
struct Frame;
extern Frame *top_frame;

struct Frame {
	const char *function;
	const char *file;
	int line = 0;
	Frame *up;

	Frame(const char *p_function, const char *p_file) :
			function(p_function), file(p_file), up(top_frame) {
		top_frame = this;
	}
	~Frame() { top_frame = up; }
};

// Writes "\tat <function>() [<file>:<line>]\n" per frame, innermost first. Returns the length written.
int capture_stack(char *r_buffer, int p_capacity);

inline int32_t add_int(int32_t p_a, int32_t p_b) {
	return (int32_t)((uint32_t)p_a + (uint32_t)p_b);
}
inline int32_t sub_int(int32_t p_a, int32_t p_b) {
	return (int32_t)((uint32_t)p_a - (uint32_t)p_b);
}
inline int32_t mul_int(int32_t p_a, int32_t p_b) {
	return (int32_t)((uint32_t)p_a * (uint32_t)p_b);
}

} // namespace as3

#define AS3_UNLIKELY(cond) __builtin_expect(!!(cond), 0)
#define AS3_FRAME(function, file) ::as3::Frame as3_frame_(function, file)
#define AS3_LINE(n) (as3_frame_.line = (n))
#define AS3_PENDING() AS3_UNLIKELY(::as3::pending_error != nullptr)

// Null check for a function that returns a value: throws TypeError and returns that type's zero value.
#define AS3_NULLCHECK(p) \
	do { \
		if (AS3_UNLIKELY((p) == nullptr)) { \
			::as3::throw_null(); \
			return {}; \
		} \
	} while (0)
// The same for a function that returns void.
#define AS3_NULLCHECK_VOID(p) \
	do { \
		if (AS3_UNLIKELY((p) == nullptr)) { \
			::as3::throw_null(); \
			return; \
		} \
	} while (0)
