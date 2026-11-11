// Version 2 of the runtime's hot-path contract (docs/superpowers/specs/2026-11-11-performance-redesign.md).
// Nothing is recorded during normal execution: a stack trace is assembled on the error path, from static
// call-site records appended as the error propagates outward.
#pragma once

#include "runtime/as3rt.h"

#include <cstddef>
#include <new>

namespace as3 {

// A place in AS3 source: where an error was thrown, or a call it propagated through.
struct Site {
	const char *function;
	const char *file;
	int line;
};

// Sets the pending error and starts a new trace at this site.
void throw_at(const Site *p_site, const char *p_type, const char *p_message);
void throw_null_at(const Site *p_site);
void throw_range_at(const Site *p_site);
void throw_coercion_at(const Site *p_site);
// Appends a call site to the pending error's trace.
void trace_add(const Site *p_site);

// Size-class allocator for AS3 objects. Main thread only. Blocks up to 256 bytes come from per-size free
// lists carved out of chunks; larger ones from malloc. The fast paths are inline.
struct FreeBlock {
	FreeBlock *next;
};
const size_t POOL_STEP = 16;
const size_t POOL_MAX = 256;
extern FreeBlock *pool_free_lists[POOL_MAX / POOL_STEP + 1];
void *pool_alloc_slow(size_t p_size);
void pool_free_large(void *p_block);

inline void *pool_alloc(size_t p_size) {
	if (AS3_UNLIKELY(p_size > POOL_MAX)) {
		return pool_alloc_slow(p_size);
	}
	size_t size_class = (p_size + POOL_STEP - 1) / POOL_STEP;
	FreeBlock *block = pool_free_lists[size_class];
	if (AS3_UNLIKELY(block == nullptr)) {
		return pool_alloc_slow(p_size);
	}
	pool_free_lists[size_class] = block->next;
	return block;
}

inline void pool_free(void *p_block, size_t p_size) {
	if (AS3_UNLIKELY(p_size > POOL_MAX)) {
		pool_free_large(p_block);
		return;
	}
	size_t size_class = (p_size + POOL_STEP - 1) / POOL_STEP;
	FreeBlock *block = static_cast<FreeBlock *>(p_block);
	block->next = pool_free_lists[size_class];
	pool_free_lists[size_class] = block;
}

template <class T, class... Args>
inline T *pool_new(Args &&...p_args) {
	return new (pool_alloc(sizeof(T))) T(static_cast<Args &&>(p_args)...);
}
template <class T>
inline void pool_delete(T *p_object) {
	p_object->~T();
	pool_free(p_object, sizeof(T));
}

} // namespace as3

// Names the enclosing AS3 function for the sites declared in it. Costs nothing at run time.
#define AS3_FN(function, file) \
	static const char *const as3_fn_ = function; \
	static const char *const as3_file_ = file; \
	(void)as3_fn_; \
	(void)as3_file_

#define AS3_SITE_(line) \
	static const ::as3::Site as3_site_ = { as3_fn_, as3_file_, line }

// After a call to an AS3 function that can throw: if an error is pending, add this call site and return.
#define AS3_PROPAGATE(line) \
	do { \
		if (AS3_PENDING()) { \
			AS3_SITE_(line); \
			::as3::trace_add(&as3_site_); \
			return {}; \
		} \
	} while (0)
#define AS3_PROPAGATE_VOID(line) \
	do { \
		if (AS3_PENDING()) { \
			AS3_SITE_(line); \
			::as3::trace_add(&as3_site_); \
			return; \
		} \
	} while (0)

#define AS3_CHECK_NULL(p, line) \
	do { \
		if (AS3_UNLIKELY((p) == nullptr)) { \
			AS3_SITE_(line); \
			::as3::throw_null_at(&as3_site_); \
			return {}; \
		} \
	} while (0)
#define AS3_CHECK_NULL_VOID(p, line) \
	do { \
		if (AS3_UNLIKELY((p) == nullptr)) { \
			AS3_SITE_(line); \
			::as3::throw_null_at(&as3_site_); \
			return; \
		} \
	} while (0)

#define AS3_CHECK_BOUNDS(index, length, line) \
	do { \
		if (AS3_UNLIKELY((uint32_t)(index) >= (uint32_t)(length))) { \
			AS3_SITE_(line); \
			::as3::throw_range_at(&as3_site_); \
			return {}; \
		} \
	} while (0)
#define AS3_CHECK_BOUNDS_VOID(index, length, line) \
	do { \
		if (AS3_UNLIKELY((uint32_t)(index) >= (uint32_t)(length))) { \
			AS3_SITE_(line); \
			::as3::throw_range_at(&as3_site_); \
			return; \
		} \
	} while (0)

#define AS3_THROW_VOID(type, message, line) \
	do { \
		AS3_SITE_(line); \
		::as3::throw_at(&as3_site_, type, message); \
		return; \
	} while (0)
