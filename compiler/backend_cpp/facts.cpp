#include "facts.h"

#include "function_writer.h"
#include "thunks.h"

namespace kas3 {
namespace backend_cpp {

Facts find_facts(const ir::Module &p_module, bool p_keeps_stack) {
	Facts facts;
	facts.functions.resize(p_module.functions.size());
	ThunkTable scratch; // The thunks a function uses are numbered as the functions are really written.
	for (uint32_t i = 0; i < p_module.functions.size(); i++) {
		FunctionWriter writer(p_module, i, scratch, p_keeps_stack, nullptr);
		writer.write();
		facts.functions[i].quiet = !writer.fails();
	}
	return facts;
}

} // namespace backend_cpp
} // namespace kas3
