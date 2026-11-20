#include "stack.h"

#include <cstdlib>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#endif

namespace kas3 {
namespace vm {

namespace {

// What a thread nobody sized can be trusted to have (the least of the systems' defaults).
constexpr size_t UNSIZED_STACK_BYTES = size_t(512) << 10;

struct Bounds {
	uintptr_t end = 0; // The lowest address: stacks grow downwards on every system this runs on.
	size_t size = 0;
};

Bounds bounds_of_thread() {
	Bounds bounds;
#if defined(__APPLE__)
	const pthread_t self = pthread_self();
	bounds.size = pthread_get_stacksize_np(self);
	bounds.end = reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(self)) - bounds.size;
#elif defined(__linux__)
	pthread_attr_t attributes;
	if (pthread_getattr_np(pthread_self(), &attributes) == 0) {
		void *end = nullptr;
		size_t size = 0;
		if (pthread_attr_getstack(&attributes, &end, &size) == 0) {
			bounds.end = reinterpret_cast<uintptr_t>(end);
			bounds.size = size;
		}
		pthread_attr_destroy(&attributes);
	}
#endif
	return bounds;
}

struct Started {
	void (*run)(void *);
	void *data;
};

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
void *run_started(void *p_started) {
	Started *started = static_cast<Started *>(p_started);
	started->run(started->data);
	return nullptr;
}
#endif

} // namespace

uintptr_t stack_floor() {
	// A thread's stack does not move: where it ends is asked once for each thread.
	static thread_local uintptr_t known = 0;
	if (known) {
		return known;
	}
	const Bounds bounds = bounds_of_thread();
	const uintptr_t here = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
	if (bounds.size == 0 || here < bounds.end || here - bounds.end > bounds.size) {
		// Not said, or not the stack this is running on (a fibre's, a signal's): the first to ask is taken
		// to be near the top of a stack of the least size.
		known = here > UNSIZED_STACK_BYTES ? here - UNSIZED_STACK_BYTES / 2 : 1;
		return known;
	}
	known = bounds.end + (bounds.size > 2 * STACK_MARGIN ? STACK_MARGIN : bounds.size / 2);
	return known;
}

bool run_with_stack(size_t p_bytes, void (*p_run)(void *), void *p_data) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
	pthread_attr_t attributes;
	pthread_t thread;
	if (pthread_attr_init(&attributes) != 0) {
		return false;
	}
	Started started{ p_run, p_data };
	const bool made = pthread_attr_setstacksize(&attributes, p_bytes) == 0 && pthread_create(&thread, &attributes, run_started, &started) == 0;
	pthread_attr_destroy(&attributes);
	if (made) {
		pthread_join(thread, nullptr);
	}
	return made;
#else
	(void)p_bytes;
	(void)p_run;
	(void)p_data;
	return false;
#endif
}

void run_as_program(void (*p_run)(void *), void *p_data) {
	size_t bytes = PROGRAM_STACK_BYTES;
	if (const char *asked = std::getenv("KAS3_STACK_KB")) {
		bytes = static_cast<size_t>(std::strtoull(asked, nullptr, 10)) << 10;
	}
	if (!run_with_stack(bytes, p_run, p_data)) {
		p_run(p_data);
	}
}

} // namespace vm
} // namespace kas3
