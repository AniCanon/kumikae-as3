// What crosses between the interpreter and the engine.
//
// An engine value (a Vector2, a node) is a handle only the engine's side understands. The interpreter
// copies and drops handles and never looks inside one. A null handle is null. Names are Godot's own.
#pragma once

#include "ir.h"
#include "runtime.h"

#include <string_view>
#include <vector>

namespace kas3 {
namespace vm {

// A value going to the engine: a number, a Boolean, a string (a Str *, or null), or a handle.
struct Argument {
	ir::Kind kind;
	Slot slot;
};

// What the interpreter wants back: the kind, and for an engine value Godot's name for its type
// ("Vector2", "Node"; empty when it is not known). The engine's side refuses what does not fit.
struct Wanted {
	ir::Kind kind = ir::Kind::Void;
	std::string_view type;
	// For EngineHandles::run(): the handle the register the result goes to holds now, if any. The engine may
	// write one of its values into it and give it back as the result, in place of a new one: a value the
	// loop makes each time round then costs no handle.
	void *reuse = nullptr;
	uint8_t *bare_reads = nullptr; // Local to an effect-free read loop. Reset before any getter can run.
};

// A thing of the engine's that the cycle collector (vm/collector.h) looks through: one that holds counted
// references to script objects, or to other such things (a reference-counted object that a script listens
// to, or that has a script; a listener). Known to the interpreter by a number the engine chooses, never 0.
struct EngineNode {
	// How many references it has. Exact, or (when `exact` is false) only those the engine can see: a
	// listener is held by its signals, and perhaps by copies the engine can't count.
	uint64_t references = 0;
	bool exact = true;
	std::vector<Object *> objects; // The script objects it holds, once for each reference it holds.
	std::vector<uint64_t> nodes; // The nodes it holds, once for each reference.
	// Script objects that live as long as it does, though it holds no reference to them (the script object
	// of a reference-counted Godot object, ir::Class::counted): in use if it is, and never taken off.
	std::vector<Object *> joined;
};

// Containers inside containers may go this deep to Godot: deeper is taken for one that holds itself. With
// the words for what Godot can't hold, said by the interpreter before a request and by the extension where
// Godot reads a value itself (a field, a result).
constexpr int MAX_ENGINE_NESTING = 128;
constexpr std::string_view HOLDS_ITSELF = "what is given to Godot holds itself, or nests too deep: Godot can't hold it.";
constexpr std::string_view ARRAY_PROPERTIES = "an Array with properties other than its indices can't be given to Godot: a Godot Array can't hold them.";

} // namespace vm
} // namespace kas3
