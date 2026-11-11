#include "runtime/as3rt.h"
#include "runtime/as3rt2.h"

#include <cstdlib>

#include <cstdio>

namespace as3 {

Error *pending_error = nullptr;
Frame *top_frame = nullptr;

static Error error_storage;
static char trace_storage[4096];

int capture_stack(char *r_buffer, int p_capacity) {
	int length = 0;
	r_buffer[0] = 0;
	for (Frame *frame = top_frame; frame && length < p_capacity - 1; frame = frame->up) {
		int written = std::snprintf(r_buffer + length, p_capacity - length, "\tat %s() [%s:%d]\n", frame->function,
				frame->file, frame->line);
		if (written < 0 || written >= p_capacity - length) {
			length = p_capacity - 1;
			break;
		}
		length += written;
	}
	return length;
}

void throw_error(const char *p_type, const char *p_message) {
	error_storage.type = p_type;
	error_storage.message = p_message;
	capture_stack(trace_storage, sizeof(trace_storage));
	pending_error = &error_storage;
}

void throw_null() {
	throw_error("TypeError", "Error #1009: Cannot access a property or method of a null object reference.");
}

void throw_range() {
	throw_error("RangeError", "Error #1125: The index is out of range.");
}

void clear_error() {
	pending_error = nullptr;
}

const char *pending_trace() {
	return trace_storage;
}

// ---- version 2: the trace is written only on the error path ----

static int trace_length = 0;

void trace_add(const Site *p_site) {
	int capacity = (int)sizeof(trace_storage) - trace_length;
	if (capacity <= 1) {
		return;
	}
	int written = std::snprintf(trace_storage + trace_length, capacity, "\tat %s() [%s:%d]\n", p_site->function,
			p_site->file, p_site->line);
	if (written > 0) {
		trace_length += written < capacity ? written : capacity - 1;
	}
}

void throw_at(const Site *p_site, const char *p_type, const char *p_message) {
	error_storage.type = p_type;
	error_storage.message = p_message;
	trace_length = 0;
	trace_storage[0] = 0;
	trace_add(p_site);
	pending_error = &error_storage;
}

void throw_null_at(const Site *p_site) {
	throw_at(p_site, "TypeError", "Error #1009: Cannot access a property or method of a null object reference.");
}

void throw_range_at(const Site *p_site) {
	throw_at(p_site, "RangeError", "Error #1125: The index is out of range.");
}

void throw_coercion_at(const Site *p_site) {
	throw_at(p_site, "TypeError", "Error #1034: Type Coercion failed.");
}

// ---- version 2: size-class allocator (fast paths are inline in as3rt2.h) ----

static const size_t POOL_CHUNK = 64 * 1024;

FreeBlock *pool_free_lists[POOL_MAX / POOL_STEP + 1];
static char *chunk_cursor = nullptr;
static char *chunk_end = nullptr;

void *pool_alloc_slow(size_t p_size) {
	if (p_size > POOL_MAX) {
		return std::malloc(p_size);
	}
	size_t bytes = ((p_size + POOL_STEP - 1) / POOL_STEP) * POOL_STEP;
	if ((size_t)(chunk_end - chunk_cursor) < bytes) {
		chunk_cursor = static_cast<char *>(std::malloc(POOL_CHUNK));
		chunk_end = chunk_cursor + POOL_CHUNK;
	}
	void *result = chunk_cursor;
	chunk_cursor += bytes;
	return result;
}

void pool_free_large(void *p_block) {
	std::free(p_block);
}

} // namespace as3
