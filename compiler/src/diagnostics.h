// Collecting and formatting the errors found in one source file.
#pragma once

#include "source.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kas3 {

struct Diagnostic {
	uint32_t offset;
	std::string message;
};

class Diagnostics {
	std::vector<Diagnostic> items_;
	int count_ = 0;

public:
	// Errors beyond this many are counted but not stored.
	static const int LIMIT = 100;

	void error(uint32_t p_offset, std::string p_message);
	bool has_errors() const { return count_ > 0; }
	// Every error reported, including those past the limit.
	int count() const { return count_; }
	// At most LIMIT items, ordered by offset, then by arrival.
	const std::vector<Diagnostic> &items() const { return items_; }
	// One "<file>:<line>:<column>: error: <message>" line per item, then a note if any were dropped.
	std::string format(const Source &p_source) const;
};

} // namespace kas3
