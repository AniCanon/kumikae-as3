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
	bool unresolved = false; // Says that a name was not found: see Diagnostics::unresolved().
};

class Diagnostics {
	std::vector<Diagnostic> items_;
	int count_ = 0; // Errors reported.
	int unresolved_ = 0; // How many of count_ say that a name was not found.
	int stored_errors_ = 0; // Errors among items_.
	bool muted_ = false;
	void store(Diagnostic p_item);
	void add(uint32_t p_offset, std::string p_message, bool p_unresolved);

public:
	// Errors beyond this many are counted but not stored.
	static const int LIMIT = 100;

	void error(uint32_t p_offset, std::string p_message);
	// An error that says a name was not found, or was found where it can't be reached. It is true only if
	// every file was read whole: a file with a syntax error may be where the name is declared.
	void unresolved(uint32_t p_offset, std::string p_message);
	// Takes back the errors reported with unresolved().
	void drop_unresolved();
	// Takes back every error and warning.
	void clear();
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
	// At most LIMIT items, ordered by offset, then by arrival: past the limit, those first in the file.
	const std::vector<Diagnostic> &items() const { return items_; }
	// One "<file>:<line>:<column>: error: <message>" (or "warning:") line per item, then a note if any were
	// dropped.
	std::string format(const Source &p_source, bool p_warnings = true) const;
};

} // namespace kas3
