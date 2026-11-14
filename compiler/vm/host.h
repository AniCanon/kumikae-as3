// What the interpreter needs from where it runs: somewhere to print, and the engine. Inside Godot the
// extension supplies all three; `kas3c run` and tests that only print supply an Output, and the engine is
// then NoEngine. Nothing else in vm/ may name the engine.
#pragma once

#include "engine_objects.h"
#include "engine_values.h"
#include "no_engine.h"
#include "output.h"

#include <type_traits>

namespace kas3 {
namespace vm {

// The three are references: a host always has each.
struct Host {
	Output &output;
	EngineValues &values;
	EngineObjects &objects;

	// No engine.
	Host(Output &p_output) :
			output(p_output), values(NoEngine::instance()), objects(NoEngine::instance()) {}
	Host(Output &p_output, EngineValues &p_values, EngineObjects &p_objects) :
			output(p_output), values(p_values), objects(p_objects) {}
	// One object that is all three.
	template <typename T, typename = std::enable_if_t<std::is_base_of<Output, T>::value && std::is_base_of<EngineValues, T>::value &&
															 std::is_base_of<EngineObjects, T>::value>>
	Host(T &p_all) :
			output(p_all), values(p_all), objects(p_all) {}
};

// Writes traces to standard output and errors to standard error.
struct StandaloneOutput final : Output {
	void print(std::string_view p_text) override;
	void error(std::string_view p_text) override;
};

} // namespace vm
} // namespace kas3
