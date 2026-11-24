#include "script_read_allocations.h"

#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
thread_local size_t allocations = 0;
}

void *operator new(size_t p_size) {
	if (void *memory = std::malloc(p_size ? p_size : 1)) {
		++allocations;
		return memory;
	}
	std::abort();
}

void operator delete(void *p_memory) noexcept {
	std::free(p_memory);
}

void operator delete(void *p_memory, size_t) noexcept {
	std::free(p_memory);
}

ScriptReadAllocations::ScriptReadAllocations(bool p_report) : start_(allocations), report_(p_report) {}

ScriptReadAllocations::~ScriptReadAllocations() {
	if (report_) {
		std::fprintf(stderr, "kas3: field allocations %zu\n", allocations - start_);
	}
}
