// A computed read may reuse its receiver's bare status across a loop with no intervening script effects.
#pragma once

#include "ir.h"

#include <algorithm>
#include <optional>

namespace kas3::ir {

inline bool preserves_method_read(const Function &p_fn, size_t p_pc, size_t p_read) {
	const auto &in = p_fn.code[p_pc];
	switch (in.op) {
		case Op::Move:
			return !is_held(p_fn.registers[in.a]) || p_fn.registers[in.a] == Kind::Str ||
					(p_pc == p_read + 1 && in.b == p_fn.code[p_read].a && in.a != p_fn.code[p_read].b);
		case Op::VecGet:
			return p_fn.registers[in.a] == Kind::Str && !is_held(p_fn.registers[in.c]);
		case Op::Stmt:
		case Op::Jump:
		case Op::JumpIf:
		case Op::JumpIfNot:
		case Op::ConstStr:
		case Op::ConstI32:
		case Op::ConstU32:
		case Op::ConstF64:
		case Op::ConstBool:
		case Op::I32ToF64:
		case Op::U32ToF64:
		case Op::F64ToI32:
		case Op::F64ToU32:
		case Op::AddF64:
		case Op::SubF64:
		case Op::MulF64:
		case Op::DivF64:
		case Op::ModF64:
		case Op::LtF64:
		case Op::LeF64:
		case Op::EqF64:
			return true;
		default:
			return false;
	}
}

// Include every back edge so an outer loop cannot reenter the read after calling user code.
// After two consecutive bare reads, both old result slots contain only the bound method.
inline std::optional<size_t> repeated_method_read(const Function &p_fn) {
	if (!p_fn.handlers.empty()) {
		return {};
	}
	size_t start = p_fn.code.size(), end = 0;
	for (size_t pc = 0; pc < p_fn.code.size(); ++pc) {
		const auto &in = p_fn.code[pc];
		const auto shape = op_shape(in.op);
		const uint32_t args[] = { in.a, in.b, in.c };
		for (size_t i = 0; i < 3; ++i) {
			if (shape[i] == 'L' && args[i] <= pc) {
				start = std::min(start, size_t(args[i]));
				end = pc;
			}
		}
	}
	std::optional<size_t> read;
	for (size_t pc = start; pc <= end && pc < p_fn.code.size(); ++pc) {
		if (p_fn.code[pc].op == Op::DynGet) {
			const auto &in = p_fn.code[pc];
			if (read || in.a == in.b || p_fn.registers[in.b] != Kind::Any || p_fn.registers[in.c] != Kind::Str) {
				return {};
			}
			read = pc;
		}
	}
	if (read) {
		for (size_t pc = start; pc <= end; ++pc) {
			if (pc != *read && !preserves_method_read(p_fn, pc, *read)) {
				return {};
			}
		}
	}
	return read;
}

} // namespace kas3::ir
