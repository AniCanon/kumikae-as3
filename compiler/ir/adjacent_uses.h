// When two adjacent instructions may hand a temporary straight to its consumer.
#pragma once

#include "ir.h"

namespace kas3::ir {

class AdjacentUses {
	std::vector<uint32_t> mentions_;
	std::vector<bool> entries_;
public:
	explicit AdjacentUses(const ir::Function &p_function) : mentions_(p_function.registers.size()), entries_(p_function.code.size()) {
		for (const auto &in : p_function.code) {
			const char *shape = ir::op_shape(in.op);
			const uint32_t operands[] = { in.a, in.b, in.c };
			for (size_t i = 0; i < 3; i++) {
				const uint32_t operand = operands[i];
				if ((shape[i] == 'R' || shape[i] == 'r') && operand < mentions_.size()) {
					mentions_[operand]++;
				} else if (shape[i] == 'L' && operand < entries_.size()) {
					entries_[operand] = true;
				} else if (shape[i] == 'A' && operand < p_function.argument_lists.size()) {
					for (const uint32_t reg : p_function.argument_lists[operand]) {
						if (reg < mentions_.size()) {
							mentions_[reg]++;
						}
					}
				}
			}
		}
		for (const auto &local : p_function.locals) {
			if (local.reg < mentions_.size()) {
				mentions_[local.reg]++;
			}
		}
		for (uint32_t reg = 0; reg < p_function.parameters && reg < mentions_.size(); reg++) {
			mentions_[reg]++;
		}
		for (const auto &handler : p_function.handlers) {
			if (handler.reg < mentions_.size()) {
				mentions_[handler.reg]++;
			}
			if (handler.target < entries_.size()) {
				entries_[handler.target] = true;
			}
		}
	}

	bool only_consumer(uint32_t p_register, size_t p_pc) const {
		return p_register < mentions_.size() && mentions_[p_register] == 2 && p_pc < entries_.size() && !entries_[p_pc];
	}
};

inline std::optional<uint32_t> move_consumer(const Function &p_function, size_t p_pc, const AdjacentUses &p_uses) {
	const auto &source = p_function.code[p_pc];
	if (p_uses.only_consumer(source.a, p_pc + 1)) {
		const auto &move = p_function.code[p_pc + 1];
		if (move.op == Op::Move && move.b == source.a) {
			return move.a;
		}
	}
	return std::nullopt;
}

struct MathConsumer {
	uint32_t target;
	uint32_t skip = 0;
};

inline std::optional<MathConsumer> math_consumer(const Function &p_function, size_t p_pc, const AdjacentUses &p_uses) {
	const auto &code = p_function.code;
	const uint32_t source = code[p_pc].a;
	if (!p_function.handlers.empty() || code[p_pc].op != Op::DynGet || !p_uses.only_consumer(source, p_pc + 1)) {
		return {};
	}
	const auto &coerce = code[p_pc + 1];
	if (coerce.op != Op::FromAny || coerce.b != source || p_function.registers[coerce.a] != Kind::Val) {
		return {};
	}
	MathConsumer result{ coerce.a, 1 };
	// On a reused result the getter never wrote its temporary.
	if (p_pc + 2 < code.size() && code[p_pc + 2].op == Op::Release && code[p_pc + 2].a == source) {
		result.skip++;
	}
	const auto next = p_pc + result.skip + 1;
	if (p_uses.only_consumer(coerce.a, next)) {
		const auto &move = code[next];
		if (move.op == Op::Move && move.b == coerce.a && p_function.registers[move.a] == Kind::Val) {
			result.target = move.a;
			result.skip++;
		}
	}
	if (result.target < p_function.parameters) {
		return std::nullopt;
	}
	return result;
}

} // namespace kas3::ir
