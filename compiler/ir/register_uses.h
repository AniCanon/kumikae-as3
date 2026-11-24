// What one instruction reads, writes and may continue at.
#pragma once

#include "ir.h"

#include <algorithm>
#include <array>

namespace kas3::ir {

// Whether the operation's first operand, a register, is read and not written (VecSet's vector).
constexpr bool first_is_read(Op p_op) {
	switch (p_op) {
		case Op::VecSet:
		case Op::VecSetLength:
		case Op::VecSetFixed:
		case Op::DynSet:
		case Op::NullCheck:
		case Op::AnyNullCheck:
		case Op::Throw:
		case Op::Rethrow:
		case Op::CallbackThis:
		case Op::SetField:
		case Op::HostSet:
		case Op::HostIndexSet:
		case Op::Return:
		case Op::JumpIf:
		case Op::JumpIfNot:
			return true;
		default:
			return false;
	}
}

// How an operation uses its three operands. Each pass over a function asks this of every instruction,
// so it is worked out from the shapes once, when the compiler is built.
struct OperandUses {
	static constexpr uint8_t NO_OPERAND = 3;
	uint8_t reads = 0; // Bit n is set where operand n is a register the operation reads.
	uint8_t list = NO_OPERAND; // The operand that is an argument list.
	uint8_t label = NO_OPERAND; // The operand that is an instruction to continue at.
	bool writes = false; // The first operand is a register the operation writes.
};

constexpr std::array<OperandUses, OP_COUNT + 1> operand_uses_by_op() {
	std::array<OperandUses, OP_COUNT + 1> table{}; // The last is for a code that names no operation.
	for (uint8_t op = 0; op < OP_COUNT; ++op) {
		OperandUses &uses = table[op];
		for (uint8_t n = 0; n < 3; ++n) {
			const char letter = OP_SHAPES[op][n];
			if (letter == 'A') {
				uses.list = n;
			} else if (letter == 'L') {
				uses.label = n;
			} else if (letter != 'R' && letter != 'r') {
				continue;
			} else if (n == 0 && !first_is_read(static_cast<Op>(op))) {
				uses.writes = true;
			} else {
				uses.reads = static_cast<uint8_t>(uses.reads | 1 << n);
			}
		}
	}
	return table;
}
inline constexpr std::array<OperandUses, OP_COUNT + 1> OPERAND_USES = operand_uses_by_op();

inline const OperandUses &operand_uses(Op p_op) {
	return OPERAND_USES[std::min(static_cast<uint8_t>(p_op), OP_COUNT)];
}

// The register an instruction gives a new value, or NONE.
inline uint32_t written_by(const Instruction &p_in) {
	return operand_uses(p_in.op).writes ? p_in.a : NONE;
}

// Calls p_read with each register an instruction reads, an argument list's among them.
template <typename Read>
void each_read(const Function &p_function, const Instruction &p_in, Read &&p_read) {
	const OperandUses &uses = operand_uses(p_in.op);
	const uint32_t operands[] = { p_in.a, p_in.b, p_in.c };
	for (size_t n = 0; n < 3; ++n) {
		if ((uses.reads >> n & 1) && operands[n] != NONE) {
			p_read(operands[n]);
		}
	}
	if (uses.list != OperandUses::NO_OPERAND) {
		for (uint32_t reg : p_function.argument_lists[operands[uses.list]]) {
			p_read(reg);
		}
	}
}

// The register whose value a store puts somewhere else, or NONE.
inline uint32_t stored_by(const Instruction &p_in) {
	switch (p_in.op) {
		case Op::Move:
		case Op::SetGlobal:
			return p_in.b;
		case Op::SetField:
		case Op::VecSet:
			return p_in.c;
		default:
			return NONE;
	}
}

// The instruction a jump or a branch may continue at, or NONE.
inline uint32_t jump_target(const Instruction &p_in) {
	const uint32_t operands[] = { p_in.a, p_in.b, p_in.c, NONE };
	return operands[operand_uses(p_in.op).label];
}

// Whether the instruction after this one may run next.
inline bool falls_through(Op p_op) {
	return p_op != Op::Jump && p_op != Op::Return && p_op != Op::ReturnVoid && p_op != Op::Throw && p_op != Op::Rethrow;
}

} // namespace kas3::ir
