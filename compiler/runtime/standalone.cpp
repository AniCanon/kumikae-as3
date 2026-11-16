// A program library run as a plain program, as `kas3c run` runs one under the interpreter.
//
// The interpreter keeps its frames on the heap, so 10,000 calls deep is an Error whatever each call
// holds. Generated functions are C++ functions and their frames are the thread's stack: the program runs
// on a thread with a stack large enough for 10,000 of any frame a program is likely to have, and a
// function that finds the stack nearly used up all the same raises the same Error (values.h, stack_low)
// instead of running off its end.
#include "native_host.h"

#include <cstdio>
#include <cstdlib>

#ifndef _WIN32
#include <pthread.h>
#endif

namespace kas3 {
namespace rt {

namespace {

constexpr size_t STACK_BYTES = size_t(512) << 20; // Address space, not memory: a page is used when touched.
// Kept free below generated code: the library's own calls beneath it, and an error's way out.
constexpr size_t STACK_MARGIN = size_t(1) << 20;
// What a thread nobody sized can be trusted to have (the least of the systems' defaults for a main thread).
constexpr size_t UNSIZED_STACK_BYTES = size_t(1) << 20;

struct Run {
	const Program *program;
	size_t stack; // Bytes of stack the thread it runs on has.
	int status;
};

void run_here(Run *r_run) {
	bool ran = false;
	{
		// KAS3_NOW fixes the time Date takes as now, as for `kas3c run`.
		const char *fixed = std::getenv("KAS3_NOW");
		vm::StandaloneOutput output;
		vm::SystemClock clock(fixed ? std::optional<double>(std::strtod(fixed, nullptr)) : std::nullopt);
		Host host(*r_run->program, Embedder{ output, vm::NoEngine::instance(), vm::NoEngine::instance(), clock });
		// A local's address is about where the stack is now: nearly all of it is still below.
		const uintptr_t here = reinterpret_cast<uintptr_t>(&ran);
		const size_t usable = r_run->stack > 2 * STACK_MARGIN ? r_run->stack - STACK_MARGIN : r_run->stack / 2;
		host.context.stack_floor = here > usable ? here - usable : 0;
		ran = host.run();
	}
	std::fflush(stdout);
	if (std::getenv("KAS3_REPORT_LEAKS") && (vm::live_objects() || vm::live_strings())) {
		std::fprintf(stderr, "kas3: %zu objects and %zu strings were never freed\n", vm::live_objects(), vm::live_strings());
		r_run->status = 3;
		return;
	}
	r_run->status = ran ? 0 : 1;
}

#ifndef _WIN32
void *run_on_thread(void *p_run) {
	run_here(static_cast<Run *>(p_run));
	return nullptr;
}
#endif

} // namespace

int run_standalone(const Program *p_program) {
	Run run{ p_program, STACK_BYTES, 1 };
	// For the tests: a stack small enough to run out of, in kilobytes.
	if (const char *asked = std::getenv("KAS3_STACK_KB")) {
		run.stack = static_cast<size_t>(std::strtoull(asked, nullptr, 10)) << 10;
	}
#ifndef _WIN32
	pthread_attr_t attributes;
	pthread_t thread;
	if (pthread_attr_init(&attributes) == 0) {
		const bool started = pthread_attr_setstacksize(&attributes, run.stack) == 0 && pthread_create(&thread, &attributes, run_on_thread, &run) == 0;
		pthread_attr_destroy(&attributes);
		if (started) {
			pthread_join(thread, nullptr);
			return run.status;
		}
	}
#endif
	// No thread of that size could be made: the program runs here, on a stack of unknown size.
	run.stack = UNSIZED_STACK_BYTES;
	run_here(&run);
	return run.status;
}

} // namespace rt
} // namespace kas3
