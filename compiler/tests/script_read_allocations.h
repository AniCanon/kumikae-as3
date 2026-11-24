// Count C++ allocations issued by the counted extension during one real Godot field read.
#pragma once

#include <cstddef>
#include <string_view>

class ScriptReadAllocations {
	size_t start_;
	size_t gets_;
	const char *label_;

public:
	explicit ScriptReadAllocations(bool p_report, const char *p_label = "field");
	~ScriptReadAllocations();
};

void count_vm_allocation();
void count_named_get();
bool count_method_read(std::string_view p_name);
void report_computed_lookup(std::string_view p_name);
