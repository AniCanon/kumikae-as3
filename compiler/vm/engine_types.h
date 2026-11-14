// What crosses between the interpreter and the engine.
//
// An engine value (a Vector2, a node) is a handle only the engine's side understands. The interpreter
// copies and drops handles and never looks inside one. A null handle is null. Names are Godot's own.
#pragma once

#include "ir.h"
#include "runtime.h"

#include <string_view>

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
};

} // namespace vm
} // namespace kas3
