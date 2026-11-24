// Count C++ allocations issued by the counted extension during one real Godot field read.
#pragma once

#include <cstddef>
#include <string_view>

class ScriptReadAllocations {
	size_t start_;
	bool report_;

public:
	explicit ScriptReadAllocations(bool p_report);
	~ScriptReadAllocations();
};

void report_computed_lookup(std::string_view p_name);
