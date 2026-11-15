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
	bool warning = false; // Said, but the program still compiles.
};

class Diagnostics {
	std::vector<Diagnostic> items_;
	int count_ = 0; // Errors reported.
	int stored_errors_ = 0; // Errors among items_.
	bool muted_ = false;
	void store(Diagnostic p_item);

public:
	// Errors beyond this many are counted but not stored.
	static const int LIMIT = 100;

	void error(uint32_t p_offset, std::string p_message);
	// Something that is allowed and works as AS3 says, but likely not what was meant.
	void warning(uint32_t p_offset, std::string p_message);
	bool has_warnings() const { return (int)items_.size() > stored_errors_; }
	// Errors reported from now on are dropped.
	void mute() { muted_ = true; }
	bool has_errors() const { return count_ > 0; }
	// Every error reported, including those past the limit.
	int count() const { return count_; }
	// The errors reported that are not among items().
	int dropped() const { return count_ - stored_errors_; }
	// At most LIMIT items, ordered by offset, then by arrival.
	const std::vector<Diagnostic> &items() const { return items_; }
	// One "<file>:<line>:<column>: error: <message>" (or "warning:") line per item, then a note if any were
	// dropped.
	std::string format(const Source &p_source, bool p_warnings = true) const;
};

} // namespace kas3
