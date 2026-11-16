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

// A class's constructor chain: its own constructor (which also gives its fields their first values), the
// `super(...)` constructors above it, and everything they call. For a reload that updates objects in place:
// it runs the new chain again on a throwaway object with the arguments an object was made with, which is
// right only if the chain depends on nothing but its arguments and constants.
struct ConstructorChain {
	// Why an object of the class can't be made again like that, in words that follow the class's name
	// ("it extends Godot's Node2D", "Coin() reads the static variable Game.count"): the first reason found.
	// Nothing when it can. Decided conservatively: the class has no Godot base and is not the library's, and
	// the chain only works out numbers, Booleans and text from its arguments, constants that hold those
	// and the object's own fields, writes only the object's own fields, calls only the object's own methods
	// and static functions that do the same, and never hands the object itself to anything else. So it
	// reads and writes no static variable, asks nothing of Godot, of the clock or of chance, prints nothing
	// and makes no object. An operation not known to be repeatable refuses.
	std::optional<std::string> refusal;
	// The constants (globals marked is_const) the chain reads, by name, each once, in the order they are
	// first met; with what it calls on other objects followed too (ir::run_once_changes says how), so
	// that a class that is refused still says what it read.
	std::vector<std::string> constants;
	// The chain's code as text that two builds compare by, as run_once_changes compares code.
	std::string text;
};
ConstructorChain constructor_chain(const Module &p_module, uint32_t p_class);

// A number two modules share if they print alike, their sources' lines are where each other's are, and
// Godot's name table said the same of each name they use: a program compiled twice from the same sources
// with the same table, by whatever reads them, gives the same one.
uint64_t fingerprint(const Module &p_module);

} // namespace ir
} // namespace kas3
