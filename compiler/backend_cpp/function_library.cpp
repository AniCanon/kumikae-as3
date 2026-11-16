// An instruction that one of the library's functions of plain values carries out whole (ir/library_entries.h):
// the runtime's function of that name called on the instruction's values, with no thunk and no interpreter.
#include "function_writer.h"

#include "library_entries.h"
#include "names.h"

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::NONE;

bool FunctionWriter::by_library(const Instruction &p_in) {
	const std::string_view entry = ir::library_entry(p_in.op, p_in.b);
	if (entry.empty()) {
		return false;
	}
	// What it reads, in the instruction's order; its number, if it has one, is handed over as it is.
	const char *shape = ir::op_shape(p_in.op);
	const uint32_t operands[3] = { p_in.a, p_in.b, p_in.c };
	std::string values;
	size_t count = 0;
	uint32_t which = 0;
	const auto value = [&](uint32_t p_register) {
		values += (count++ ? ", " : "") + (p_register == NONE ? std::string("empty()") : "slot(" + reg(p_register) + ")");
	};
	for (int n = 1; n < 3; n++) {
		if (shape[n] == 'R' || shape[n] == 'r') {
			value(operands[n]);
		} else if (shape[n] == 'A') {
			for (const uint32_t each : function_.argument_lists[operands[n]]) {
				value(each);
			}
		} else if (shape[n] != '-') {
			which = operands[n];
		}
	}
	mark();
	line("{");
	line("const Slot t[] = { " + values + (count ? ", " : "") + "empty() };"); // One more than given: no array is empty.
	// With no register for what it gives, the function lets go of it.
	if (p_in.a != NONE) {
		line("Slot o = empty();");
	}
	line(std::string(entry) + "(c, " + std::to_string(which) + ", t, " + std::to_string(count) + (p_in.a != NONE ? ", &o);" : ", nullptr);"));
	check();
	if (p_in.a != NONE) {
		put(p_in.a, from_slot(kind(p_in.a), "o"));
	}
	line("}");
	return true;
}

} // namespace backend_cpp
} // namespace kas3
