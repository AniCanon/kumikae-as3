// The records the interpreter keeps (vm/interpreter.h): its frames, where an error was thrown and what is
// kept of one on its way, a match, a property's key, a call of a vector's method and a field sortOn() sorts
// by. Each is known in the class by the same name. Nothing but interpreter.h includes this.
#pragma once

#include "ir.h"
#include "runtime.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kas3 {
namespace vm {

// A call in progress.
struct Frame {
	uint32_t function;
	uint32_t pc;
	size_t base; // Where its registers start in stack_.
	uint32_t result; // The caller's register for the value returned, or NONE.
	uint32_t arguments; // How many the caller passed.
	bool traced = false; // Its line is already in the trace of the error on its way out.
};

// Where an error was thrown, as the trace's first line says it.
struct Site {
	std::string function;
	std::string file;
	int32_t line = 0;
};

// An error carried back from a call the engine made, kept until the instruction that made the call ends
// (Interpreter::carried_).
struct Carried {
	bool held = false;
	bool stopped = false; // It stops the run: Flash's #1502, which no catch stops.
	size_t frames = 0; // How many frames there were: the last is the one whose instruction it ends.
	Slot error{};
	std::string trace;
	size_t lines = 0;
	Site at;
};

// The trace of an error a `finally` caught, kept for when it is thrown again: by the depth of the frame
// and the register the finally keeps its error in, so that there is at most one for each finally.
struct Kept {
	size_t depth;
	uint32_t reg;
	std::string trace;
	size_t lines;
	Site site;
};

// A match: where it and each group start and end, by unit (-1 for a group that took no part).
struct Found {
	std::vector<int32_t> captures;
};

// A key as its holder keeps it: an element's index, a Dictionary's object, or text (always filled in but for
// an object).
struct PropertyKey {
	std::optional<uint32_t> index;
	Object *object = nullptr; // Held by the key it came from.
	std::string text;
};

// A call of one of a vector's methods: its arguments (the vector first) by register, where the
// frame's registers start, and the register for what it gives back, or NONE.
struct VectorCall {
	const std::vector<uint32_t> &list;
	size_t base;
	uint32_t out;
	ir::VectorMethod method;
};

// An operator an engine instruction names, worked out once from its entry: AS3's spelling ("-" for the
// description's "unary-"), and whether it compares (== and !=, which take a missing value).
struct OperatorName {
	std::string_view spelling;
	bool compares = false;
	int number = -1; // Godot's number for it (Variant::Operator), the entry's second word.
};

struct Prepared; // vm/engine_handles.h

// What the host worked out for one engine instruction when the program was loaded, and whether any of its
// operands is untyped (what refused_by_godot() has to look at as it runs).
struct EngineStep {
	const Prepared *prepared = nullptr;
	bool untyped = false;
};

// A field sortOn() sorts by, and its options.
struct SortField {
	std::string name;
	int32_t options;
};

} // namespace vm
} // namespace kas3
