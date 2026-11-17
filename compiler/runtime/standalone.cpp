// A program library run as a plain program, as `kas3c run` runs one under the interpreter: on a thread
// with a stack large enough for 10,000 calls of any frame a program is likely to have (vm/stack.h). A
// function that finds the stack nearly used up all the same raises the same Error (values.h, stack_low)
// instead of running off its end.
#include "native_host.h"
#include "stack.h"

#include <cstdio>
#include <cstdlib>

namespace kas3 {
namespace rt {

namespace {

struct Run {
	const Program *program;
	int status;
};

void run_here(void *p_run) {
	Run *r_run = static_cast<Run *>(p_run);
	bool ran = false;
	{
		// KAS3_NOW fixes the time Date takes as now, as for `kas3c run`.
		const char *fixed = std::getenv("KAS3_NOW");
		vm::StandaloneOutput output;
		vm::SystemClock clock(fixed ? std::optional<double>(std::strtod(fixed, nullptr)) : std::nullopt);
		Host host(*r_run->program, Embedder{ output, vm::NoEngine::instance(), vm::NoEngine::instance(), clock });
		host.context.stack_floor = vm::stack_floor();
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

} // namespace

int run_standalone(const Program *p_program) {
	if (!same_layout(*p_program)) {
		std::fprintf(stderr, "kas3: the program is not run: %s\n", OTHER_LAYOUT);
		return 2;
	}
	Run run{ p_program, 1 };
	vm::run_as_program(run_here, &run);
	return run.status;
}

} // namespace rt
} // namespace kas3
