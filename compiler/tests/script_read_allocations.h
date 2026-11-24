// Count C++ allocations issued by the counted extension during one real Godot field read.
#pragma once

#include <cstddef>

class ScriptReadAllocations {
	size_t start_;
	bool report_;

public:
	explicit ScriptReadAllocations(bool p_report);
	~ScriptReadAllocations();
};
