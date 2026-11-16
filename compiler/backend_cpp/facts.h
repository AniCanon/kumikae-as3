// What is known of every function of the module before any is written, so that a function can be written
// by what the functions it calls do.
#pragma once

#include "ir.h"

#include <vector>

namespace kas3 {
namespace backend_cpp {

struct FunctionFacts {
	// Nothing in it can raise an error, and so it calls nothing of the program's (a call can always be
	// one too deep). It is written with no count of the depth, and whoever calls it looks at the depth
	// before the call in its place, and for no error after it.
	bool quiet = false;
};

struct Facts {
	std::vector<FunctionFacts> functions;
};

// Found by writing every function once, into nothing: what can fail is what the writer writes a way out
// for, so the two cannot come apart.
Facts find_facts(const ir::Module &p_module, bool p_keeps_stack);

} // namespace backend_cpp
} // namespace kas3
