// The typed IR: what a checked program means, as plain data. Both backends read it and nothing else.
// Everything refers to everything else by index, never by pointer and never to the syntax tree, so that a
// module can be written to a file and run without the front end.
#pragma once

#include "ir_codes.h"
#include "ir_module.h"
#include "ir_ops.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kas3 {
namespace ir {

// The module as text, for tests and for reading.
std::string print(const Module &p_module);
// One function as text. Names stand for what operands point at, and no position is shown: the same code
// moved down a file prints the same.
std::string print(const Module &p_module, const Function &p_function);
// One operand of one of the function's instructions, by its shape letter (see KAS3_IR_OPS).
std::string print_operand(const Module &p_module, const Function &p_function, char p_shape, uint32_t p_value);

// The classes, by name, whose code that runs once for each object changed from one build to the next: a
// constructor (which also gives fields their first values), `_ready`, `_enter_tree` or `_init` (its own or
// inherited), or what those call: by name, on the object itself (an override included), on another object
// through the class it is declared as (any subclass's override included) or through an interface (any
// class's that implements it). A call on an untyped value, or through a function value, is not followed.
// A field or method of the object a function runs on is compared by name, so
// one added before it changes nothing and one renamed does. So a `_ready` that reaches much of the game
// is changed by most edits to it. Objects that are alive ran the old version, so a
// reload of the code alone does not show such a change. A class that is new, or gone, is not listed.
std::vector<std::string> run_once_changes(const Module &p_old, const Module &p_new);
// The classes, by name, whose static fields' first values changed from one build to the next (what the
// class's static initializer runs, and what it calls). Static fields given their values keep them through a
// reload, and through the scene started again.
std::vector<std::string> static_changes(const Module &p_old, const Module &p_new);

// A number two modules that print alike share: a program compiled twice from the same sources, by whatever
// reads them, gives the same one.
uint64_t fingerprint(const Module &p_module);

} // namespace ir
} // namespace kas3
