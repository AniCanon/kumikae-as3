// Where a program's traces and uncaught errors go. Every host has one.
#pragma once

#include <string_view>

namespace kas3 {
namespace vm {

struct Output {
	virtual ~Output() {}
	// One traced line, without its line break.
	virtual void print(std::string_view p_text) = 0;
	// An error nothing caught: its text and stack trace, several lines.
	virtual void error(std::string_view p_text) = 0;
};

} // namespace vm
} // namespace kas3
