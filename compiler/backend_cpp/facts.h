// What is known of every function of the module before any is written, so that a function can be written
// by what the functions it calls do.
#pragma once

#include "ir.h"

#include <set>
#include <vector>

namespace kas3 {
namespace backend_cpp {

struct FunctionFacts {
	// Nothing in it can raise an error, and so it calls nothing of the program's (a call can always be
	// one too deep). It is written with no count of the depth, and whoever calls it looks at the depth
	// before the call in its place, and for no error after it.
	bool quiet = false;
	// What it does to the fields of objects, itself and through the functions it calls: either anything
	// at all (it runs code that is not looked into: the library, the engine, a call found in a table, or
	// it lets go of something whose end may run a script), or only these slots, read and written, of
	// whatever objects it has.
	bool opaque = true;
	std::set<uint32_t> reads;
	std::set<uint32_t> writes;
	// It, or a function it calls, stores an element of a vector: which may make the vector longer.
	bool stores_elements = false;
};

struct Facts {
	std::vector<FunctionFacts> functions;
};

// Whether an instruction may run code that nothing here looks into. One that cannot is arithmetic, flow,
// an element of a vector, a static field: it leaves every field of every object as it was, but for a
// GetField or a SetField (and a Call, which is as its callee is), which say themselves what they touch.
// Letting go of an object, a Godot value or an untyped value counts as running code: its end may be the
// end of a Godot object, which a script is told of.
// For a Call it answers for the call alone (a result that is let go of), not for what the callee does.
bool may_run_code(const ir::Module &p_module, const ir::Function &p_function, const ir::Instruction &p_in);
// The same, where writing a register lets go of nothing because the register counts nothing (p_counts_nothing
// says which): what is left is what the instruction does besides.
bool may_run_code(const ir::Module &p_module, const ir::Function &p_function, const ir::Instruction &p_in,
		const std::vector<bool> &p_counts_nothing);

// Found by writing every function once, into nothing: what can fail is what the writer writes a way out
// for, so the two cannot come apart.
Facts find_facts(const ir::Module &p_module, bool p_keeps_stack);

} // namespace backend_cpp
} // namespace kas3
