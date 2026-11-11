#include "runtime/as3rt.h"

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

} // namespace as3
