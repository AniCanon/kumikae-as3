// One function of the module written as a C++ function, by the fixed shape of generated code:
//
//   - `Context *c` first, then the parameters in their kinds, then `n` (how many the caller gave) for a
//     function that has defaults;
//   - a register is a local; one that holds a String, an object or an untyped value owns a reference,
//     but for a parameter that is never written, which borrows its caller's;
//   - a result is the caller's to release;
//   - an error is `c->failing`, tested after each operation that can raise one: the function then leaves
//     through its handlers, or by `failed`, which puts its line into the trace.
#pragma once

#include "ir.h"
#include "thunks.h"

#include <string>
#include <vector>

namespace kas3 {
namespace backend_cpp {

// A function's C++ type and parameters, for its definition, its declaration and a call through a pointer.
struct Signature {
	std::string result; // "double"
	std::vector<std::string> parameters; // "Context *", "Object *", "double", "uint32_t"
	bool counted = false; // The last parameter is how many arguments the caller gave.
};
Signature signature_of(const ir::Function &p_function);
// Whether callers say how many arguments they give: the function has parameters with defaults.
bool takes_count(const ir::Function &p_function);

class FunctionWriter {
	const ir::Module &module_;
	const uint32_t index_;
	const ir::Function &function_;
	ThunkTable &thunks_;
	std::string body_;
	std::vector<bool> targets_; // Per instruction: a jump lands on it.
	std::vector<bool> written_; // Per register: an instruction writes it.
	// The handlers an instruction's error is offered to, innermost first, as numbers of
	// function_.handlers. Instructions with the same list leave by the same label.
	std::vector<std::vector<uint32_t>> chains_;
	bool fails_ = false; // Something in the function can raise an error.
	uint32_t pc_ = 0;

	std::string reg(uint32_t p_register) const;
	ir::Kind kind(uint32_t p_register) const { return function_.registers[p_register]; }
	bool owns(uint32_t p_register) const; // Whether the register holds a reference of its own.
	bool is_this(uint32_t p_register) const; // The object of the method being written: never null.
	void line(const std::string &p_text);
	// Stores an owned value in a register, giving up what was there.
	void put(uint32_t p_register, const std::string &p_owned);
	// Stores a value someone else holds: a reference is taken first.
	void share(uint32_t p_register, const std::string &p_borrowed);
	std::string fail(); // "line = 12; goto failed;": where an error at this instruction goes.
	void check(); // If an error is on its way out, go where fail() says.
	void null_check(uint32_t p_register);
	uint32_t chain_of(uint32_t p_pc);

	void instruction(const ir::Instruction &p_in);
	bool simple(const ir::Instruction &p_in); // function_simple.cpp: arithmetic, conversions, comparisons.
	bool constant(const ir::Instruction &p_in);
	bool object(const ir::Instruction &p_in); // function_objects.cpp: fields, globals, classes.
	bool flow(const ir::Instruction &p_in); // Jumps, returns, throws.
	bool call(const ir::Instruction &p_in); // function_calls.cpp
	void call_function(const ir::Instruction &p_in, const ir::Function &p_callee, const std::string &p_target);
	void by_thunk(const ir::Instruction &p_in);
	std::string arguments(const ir::Function &p_callee, const std::vector<uint32_t> &p_list);
	const ir::Function *virtual_callee(const ir::Instruction &p_in) const;

	void analyse();
	std::string prologue() const;
	std::string epilogue() const;
	std::string handlers() const;

public:
	FunctionWriter(const ir::Module &p_module, uint32_t p_function, ThunkTable &r_thunks);
	// The function's definition, and after it its entry (the function called with slots).
	std::string write();
};

} // namespace backend_cpp
} // namespace kas3
