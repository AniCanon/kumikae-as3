// What the interpreter needs from where it runs. Inside Godot the engine supplies it; `kas3c run` and the
// tests use the stand-in in standalone_host.cpp. Nothing else in vm/ may name the engine.
#pragma once

#include <string_view>

namespace kas3 {
namespace vm {

struct Host {
	virtual ~Host() {}
	// One traced line, without its line break.
	virtual void print(std::string_view p_text) = 0;
	// An error nothing caught: its text and stack trace, several lines.
	virtual void error(std::string_view p_text) = 0;
};

// Writes traces to standard output and errors to standard error.
struct StandaloneHost : Host {
	void print(std::string_view p_text) override;
	void error(std::string_view p_text) override;
};

} // namespace vm
} // namespace kas3
