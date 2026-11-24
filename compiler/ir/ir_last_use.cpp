#include "last_use.h"

#include "liveness.h"

#include <algorithm>

namespace kas3::ir {

uint32_t changed_in_place(const ir::Function &p_function, const ir::Instruction &p_in) {
	const auto value = [&](uint32_t p_register) { return p_function.registers[p_register] == ir::Kind::Val ? p_register : NONE; };
	switch (p_in.op) {
		case Op::DynNext:
			return p_in.b;
		case Op::HostSet:
		case Op::HostIndexSet:
			return value(p_in.a);
		case Op::HostCall:
			return p_function.argument_lists[p_in.c].empty() ? NONE : value(p_function.argument_lists[p_in.c][0]);
		default:
			return NONE;
	}
}

std::vector<bool> last_stores(const Function &p_function, const std::vector<bool> &p_candidate) {
	auto last = p_candidate;
	if (std::none_of(last.begin(), last.end(), [](bool p_value) { return p_value; })) {
		return last;
	}
	std::vector<bool> sources(p_function.registers.size());
	for (size_t pc = 0; pc < last.size(); ++pc) {
		if (last[pc]) {
			sources[stored_by(p_function.code[pc])] = true;
		}
	}
	Liveness live;
	live.solve(p_function, sources);
	live.each_instruction([&](uint32_t p_pc, const std::vector<uint32_t> &p_ending) {
		if (last[p_pc]) {
			last[p_pc] = std::find(p_ending.begin(), p_ending.end(), stored_by(p_function.code[p_pc])) != p_ending.end();
		}
	});
	return last;
}

} // namespace kas3::ir
