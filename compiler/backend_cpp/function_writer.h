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

#include "facts.h"
#include "ir.h"
#include "thunks.h"

#include <optional>
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
	// The program reads the stack of an error nothing threw: each function keeps a StackFrame, and says
	// which line it is on before anything that may run code.
	const bool keeps_stack_;
	const Facts *const facts_; // Null while the facts themselves are being found: nothing is known of any callee.
	std::string body_;
	std::vector<bool> targets_; // Per instruction: a jump lands on it.
	std::vector<bool> written_; // Per register: an instruction writes it.
	// The handlers an instruction's error is offered to, innermost first, as numbers of
	// function_.handlers. Instructions with the same list leave by the same label.
	std::vector<std::vector<uint32_t>> chains_;
	bool fails_ = false; // Something in the function can raise an error.
	// A Number register that holds a sum of whole numbers, as the same sum in 32-bit arithmetic
	// (function_whole.cpp): what it reads, so that it is forgotten when one of those is written.
	struct Whole {
		std::string expression; // Empty: nothing is known of the register.
		std::vector<uint32_t> sources;
		uint32_t terms = 0;
	};
	std::vector<Whole> whole_;
	// A String register that holds a constant of the program, by the string's number (NONE otherwise): what
	// Godot takes as a name is then one the host built once.
	std::vector<uint32_t> text_;
	// What stands for a register while the instruction being written is one the library runs: a value
	// held inline is boxed into a handle for it (function_engine.cpp). Empty: the register itself.
	std::vector<std::string> shadow_;
	// A register that holds one of Godot's objects and only ever the script's own, or one singleton: it
	// borrows a handle that outlives the function, and owns nothing.
	enum class Alias : uint8_t { None, Owner, Singleton };
	std::vector<Alias> alias_;
	std::vector<uint32_t> alias_name_; // For a singleton: its engine name.
	std::vector<uint32_t> singleton_use_; // And the use the host resolves it by, once written.
	bool has_owner_alias_ = false;
	// Per register: it holds a value with parts inline. Only one that every instruction writing it gives a
	// value to (what Godot makes, computes or gives for a property of that type): a register that may be
	// given null (a field read, a call's result, an untyped value) stays a handle, which can say so.
	std::vector<bool> inline_;
	void find_inline();
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
	void mark(); // Before what may run other code: the line this function is on, where frames are kept.
	void null_check(uint32_t p_register);
	uint32_t chain_of(uint32_t p_pc);

	void instruction(const ir::Instruction &p_in);
	bool simple(const ir::Instruction &p_in); // function_simple.cpp: arithmetic, conversions, comparisons.
	bool constant(const ir::Instruction &p_in);
	bool object(const ir::Instruction &p_in); // function_objects.cpp: fields, globals, classes.
	bool flow(const ir::Instruction &p_in); // Jumps, returns, throws.
	bool call(const ir::Instruction &p_in); // function_calls.cpp
	// p_direct: the callee is the very function p_callee, and not one found in a table as the call is made.
	void call_function(const ir::Instruction &p_in, const ir::Function &p_callee, const std::string &p_target, bool p_direct);
	void by_thunk(const ir::Instruction &p_in);
	std::string_view parts(uint32_t p_register) const; // The value with parts the register holds inline, if it does.
	void dispatch(const ir::Instruction &p_in); // The instruction written by whichever part owns it.
	void generic(const ir::Instruction &p_in); // dispatch(), with the values held inline boxed for it.
	const ir::Function *callee_of(const ir::Instruction &p_in) const;
	// function_engine.cpp
	void find_aliases();
	bool holds(uint32_t p_register, std::string_view p_type) const;
	std::string object_behind(uint32_t p_register);
	std::optional<std::string> bare_argument(uint32_t p_register, std::string_view p_type, size_t p_at, std::string *r_guard);
	bool engine(const ir::Instruction &p_in);
	bool engine_alias(const ir::Instruction &p_in);
	bool engine_inline(const ir::Instruction &p_in);
	bool engine_bound(const ir::Instruction &p_in);
	void forget_whole(uint32_t p_register);
	bool whole_conversion(const ir::Instruction &p_in);
	void remember_whole(const ir::Instruction &p_in);
	// function_fast.cpp: an inline path for the usual case, and the thunk for the rest.
	bool fast_object(const ir::Instruction &p_in); // A new object, a vector's elements and length.
	bool fast_text(const ir::Instruction &p_in);
	bool fast_untyped(const ir::Instruction &p_in);
	bool fast_call(const ir::Instruction &p_in);
	std::string arguments(const ir::Function &p_callee, const std::vector<uint32_t> &p_list);
	const ir::Function *virtual_callee(const ir::Instruction &p_in) const;

	void analyse();
	std::string prologue() const;
	std::string epilogue() const;
	std::string handlers() const;

public:
	FunctionWriter(const ir::Module &p_module, uint32_t p_function, ThunkTable &r_thunks, bool p_keeps_stack, const Facts *p_facts);
	// The function's definition, and after it its entry (the function called with slots).
	std::string write();
	// Once written: whether anything in it can raise an error.
	bool fails() const { return fails_; }
};

} // namespace backend_cpp
} // namespace kas3
