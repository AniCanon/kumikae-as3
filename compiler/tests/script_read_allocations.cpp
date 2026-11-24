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

void count_vm_allocation() {
	++allocations;
}

bool count_method_read(std::string_view p_name) {
	static const bool enabled = std::getenv("KAS3_METHOD_ALLOCATIONS") != nullptr;
	return enabled && (p_name == "isVisibleInTree" || p_name == "isProcessing" || p_name == "probe_method");
}

ScriptReadAllocations::ScriptReadAllocations(bool p_report, const char *p_label) : start_(allocations), label_(p_report ? p_label : nullptr) {}

ScriptReadAllocations::~ScriptReadAllocations() {
	if (label_) {
		std::fprintf(stderr, "kas3: %s allocations %zu\n", label_, allocations - start_);
	}
}

void report_computed_lookup(std::string_view p_name) {
	static const char *watched = std::getenv("KAS3_TRACE_COMPUTED_NAME");
	if (watched && p_name == watched) {
		std::fprintf(stderr, "kas3: resolved computed name %s\n", watched);
	}
}
