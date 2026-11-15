// What the interpreter needs from where it runs: somewhere to print, the engine, and a clock. Inside Godot
// the extension supplies all four; `kas3c run` and tests that only print supply an Output, the engine is
// then NoEngine and the clock the system's. Nothing else in vm/ may name the engine.
#pragma once

#include "clock.h"
#include "engine_handles.h"
#include "engine_source.h"
#include "no_engine.h"
#include "output.h"

#include <type_traits>

namespace kas3 {
namespace vm {

// The four are references: a host always has each.
struct Host {
	Output &output;
	EngineHandles &handles;
	EngineSource &source;
	Clock &clock;

	Host(Output &p_output, EngineHandles &p_handles, EngineSource &p_source, Clock &p_clock = SystemClock::instance()) :
			output(p_output), handles(p_handles), source(p_source), clock(p_clock) {}

	// One object that is the whole host, or one that only prints (and may keep the time). An object that
	// is half an engine is a mistake, and does not compile.
	template <typename T, typename = std::enable_if_t<std::is_base_of<Output, T>::value>>
	Host(T &p_host) :
			output(p_host), handles(engine<EngineHandles>(p_host)), source(engine<EngineSource>(p_host)), clock(clock_of(p_host)) {
		static_assert(std::is_base_of<EngineHandles, T>::value == std::is_base_of<EngineSource, T>::value,
				"a host implements both engine interfaces or neither");
	}

private:
	template <typename Interface, typename T>
	static Interface &engine(T &p_host) {
		if constexpr (std::is_base_of<Interface, T>::value) {
			return p_host;
		} else {
			return NoEngine::instance();
		}
	}
	template <typename T>
	static Clock &clock_of(T &p_host) {
		if constexpr (std::is_base_of<Clock, T>::value) {
			return p_host;
		} else {
			return SystemClock::instance();
		}
	}
};

// Writes traces to standard output and errors to standard error.
struct StandaloneOutput final : Output {
	void print(std::string_view p_text) override;
	void error(std::string_view p_text) override;
};

} // namespace vm
} // namespace kas3
