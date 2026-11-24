// The operations generated code leaves to the library the interpreter has: each is an instruction the
// host carries out with the values handed to it (runtime/program.h, ThunkInfo). Two instructions alike in
// everything but their registers' numbers are one thunk.
#pragma once

#include "last_use.h"

#include <map>
#include <string>
#include <vector>

namespace kas3 {
namespace backend_cpp {

struct Thunk {
	ir::Op op = ir::Op::Stmt;
	uint32_t a = 0, b = 0, c = 0; // A register operand is a register of the thunk's.
	std::vector<ir::Kind> kinds; // Its registers: those handed in first.
	std::vector<std::string> types; // Godot's name for the type of each, where the function says one.
	uint32_t given = 0;
	std::vector<uint32_t> list; // The instruction's argument list, in the thunk's registers.
	uint32_t result = ir::NONE;
	uint32_t updated = ir::NONE;
	uint32_t code_class = ir::NONE;
	uint32_t self = ir::NONE; // The thunk's register that is the method's object, for an engine instruction.

	bool operator<(const Thunk &p_other) const;
};

// A use of a thunk in a function: which thunk, and the function's registers it is handed and hands back.
struct ThunkUse {
	uint32_t thunk = 0;
	std::vector<uint32_t> given; // The function's registers, in the thunk's order.
	uint32_t result = ir::NONE; // The function's register the result goes to.
	uint32_t updated = ir::NONE; // The function's register that is moved on.
};

class ThunkTable {
	std::map<Thunk, uint32_t> numbers_;
	std::vector<Thunk> thunks_;
	std::vector<uint32_t> uses_;
	std::vector<uint32_t> names_;
	uint32_t number_of(const Thunk &p_thunk);

public:
	// The thunk for an instruction of a function, made if there is none like it yet.
	ThunkUse use(const ir::Function &p_function, const ir::Instruction &p_in);
	// The thunk that makes a function's own value: bound to an object handed in, or to nothing.
	uint32_t function_value(uint32_t p_function, bool p_bound);
	// The thunk that makes an untyped value a value of the kind, as a store does: a catch's variable.
	uint32_t from_any(ir::Kind p_kind);
	// A place generated code asks the engine directly, as the thunk of the same instruction: its number
	// among the uses the host resolves. And a String given as one of Godot's names, likewise.
	uint32_t use_index(uint32_t p_thunk);
	uint32_t name_index(uint32_t p_string);
	const std::vector<uint32_t> &uses() const { return uses_; }
	const std::vector<uint32_t> &names() const { return names_; }
	const std::vector<Thunk> &all() const { return thunks_; }
};

} // namespace backend_cpp
} // namespace kas3
