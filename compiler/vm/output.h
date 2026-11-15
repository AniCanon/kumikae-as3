// Where a program's traces and uncaught errors go. Every host has one.
#pragma once

#include <cstdint>
#include <string_view>

namespace kas3 {
namespace vm {

struct Output {
	virtual ~Output() {}
	// One traced line, without its line break.
	virtual void print(std::string_view p_text) = 0;
	// An error nothing caught: its text and stack trace, several lines.
	virtual void error(std::string_view p_text) = 0;
	// The same, with where it was thrown: the innermost function, its file and line, as the trace's first
	// line says them. A host that can point at the line does; the others take the text alone.
	virtual void error_at(std::string_view p_text, std::string_view p_function, std::string_view p_file, int32_t p_line) {
		(void)p_function;
		(void)p_file;
		(void)p_line;
		error(p_text);
	}
};

} // namespace vm
} // namespace kas3
