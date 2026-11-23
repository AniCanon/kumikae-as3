// Adjacent scalar operations in a counted loop can advance the same frame together. Every intermediate
// register is written as usual. A branch into the middle still starts at that instruction.
#pragma once

#include "ir.h"
#include "runtime.h"

namespace kas3::vm {

inline void numeric_tail(const ir::Module &p_module, const ir::Function &p_function, uint32_t &r_pc, Slot *r_values) {
	using ir::Op;
	const size_t count = p_function.code.size() - r_pc;
	const ir::Instruction *in = p_function.code.data() + r_pc;
	if (count >= 3 && in[0].op == Op::I32ToF64 && in[1].op == Op::LtF64 && in[2].op == Op::JumpIfNot) {
		r_values[in[0].a].d = r_values[in[0].b].i;
		const bool before = r_values[in[1].b].d < r_values[in[1].c].d;
		r_values[in[1].a].bits = 0;
		r_values[in[1].a].b = before;
		r_pc = r_values[in[2].a].b ? r_pc + 3 : in[2].b;
	} else if (count >= 5 && in[0].op == Op::ConstF64 && in[1].op == Op::AddF64 && in[2].op == Op::F64ToI32 &&
			in[3].op == Op::Move && in[4].op == Op::Jump && p_function.registers[in[3].a] == ir::Kind::I32) {
		r_values[in[0].a].d = p_module.numbers[in[0].b];
		r_values[in[1].a].d = r_values[in[1].b].d + r_values[in[1].c].d;
		r_values[in[2].a].i = to_int32(r_values[in[2].b].d);
		r_values[in[3].a] = r_values[in[3].b];
		r_pc = in[4].a;
	}
}

} // namespace kas3::vm
