// Adjacent scalar operations in a counted loop can advance the same frame together. Every intermediate
// register is written as usual. A branch into the middle still starts at that instruction.
#pragma once

#include "ir.h"
#include "runtime.h"

#include <limits>

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
	} else if (count >= 2 && in[0].op == Op::AddF64 && in[1].op == Op::Move && p_function.registers[in[1].a] == ir::Kind::F64) {
		r_values[in[0].a].d = r_values[in[0].b].d + r_values[in[0].c].d;
		r_values[in[1].a] = r_values[in[1].b];
		r_pc += 2;
	} else if (count >= 2 && in[0].op == Op::I32ToF64 && in[1].op == Op::ModF64 && r_pc &&
			in[1].b == in[-1].a && in[1].c == in[0].a && in[0].a != in[1].b) {
		// The dispatch that brought us here just converted the dividend from int.
		const int32_t left = r_values[in[-1].b].i;
		const int32_t right = r_values[in[0].b].i;
		r_values[in[0].a].d = right;
		if (right) {
			const int32_t remainder = right == -1 ? 0 : left % right;
			r_values[in[1].a].d = remainder == 0 && left < 0 ? -0.0 : static_cast<double>(remainder);
		} else {
			r_values[in[1].a].d = std::numeric_limits<double>::quiet_NaN();
		}
		r_pc += 2;
	} else if (count >= 4 && in[1].op == Op::AddF64 && in[2].op == Op::F64ToI32 &&
			in[3].op == Op::Move && p_function.registers[in[3].a] == ir::Kind::I32) {
		const bool increment = in[0].op == Op::ConstF64 && count >= 5 && in[4].op == Op::Jump;
		if (!increment && in[0].op != Op::I32ToF64) {
			return;
		}
		r_values[in[0].a].d = increment ? p_module.numbers[in[0].b] : static_cast<double>(r_values[in[0].b].i);
		r_values[in[1].a].d = r_values[in[1].b].d + r_values[in[1].c].d;
		r_values[in[2].a].i = to_int32(r_values[in[2].b].d);
		r_values[in[3].a] = r_values[in[3].b];
		r_pc = increment ? in[4].a : r_pc + 4;
	}
}

} // namespace kas3::vm
