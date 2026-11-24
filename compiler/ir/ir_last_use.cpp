#include "last_use.h"

namespace kas3::ir {

bool first_is_read(Op p_op) {
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

// A copy that is the last use of what it copies. Only the plain case is looked for, which is a value made
// and then put where it is kept: a register written once, in straight code from there to this copy, with
// every read of it between the two. Nothing can then read the register before it is written again, so
// the copy takes its reference and leaves it empty.
std::vector<bool> last_use_stores(const Function &p_function, const std::vector<bool> &p_owned) {
	std::vector<bool> transfers(p_function.code.size());
	const size_t count = p_function.registers.size();
	std::vector<uint32_t> writes(count, 0);
	std::vector<uint32_t> written_at(count, NONE);
	std::vector<uint32_t> last_read(count, NONE);
	std::vector<uint32_t> first_read(count, NONE);
	std::vector<bool> lands(p_function.code.size() + 1, false);
	// A named local owns its reference until reassignment or frame exit, even after its last read.
	for (const auto &local : p_function.locals) {
		writes[local.reg] += 2;
	}
	for (const ir::Handler &handler : p_function.handlers) {
		writes[handler.reg] += 2; // Written from outside the code: never the plain case.
		lands[handler.target] = true;
	}
	const auto read = [&](uint32_t p_register, uint32_t p_pc) {
		first_read[p_register] = first_read[p_register] == NONE ? p_pc : first_read[p_register];
		last_read[p_register] = p_pc;
	};
	for (uint32_t pc = 0; pc < p_function.code.size(); pc++) {
		const Instruction &in = p_function.code[pc];
		const char *shape = ir::op_shape(in.op);
		const uint32_t operands[3] = { in.a, in.b, in.c };
		const bool writes_first = (shape[0] == 'R' || shape[0] == 'r') && in.a != NONE && !first_is_read(in.op);
		for (int n = 0; n < 3; n++) {
			if (shape[n] == 'L') {
				lands[operands[n]] = true;
			} else if (shape[n] == 'A') {
				for (uint32_t each : p_function.argument_lists[operands[n]]) {
					read(each, pc);
				}
			} else if ((shape[n] == 'R' || shape[n] == 'r') && operands[n] != NONE && !(n == 0 && writes_first)) {
				read(operands[n], pc);
			}
		}
		if (writes_first) {
			writes[in.a]++;
			written_at[in.a] = pc;
		}
		if (const uint32_t changed = changed_in_place(p_function, in); changed != NONE) {
			writes[changed] += 2;
		}
	}
	for (uint32_t pc = 0; pc < p_function.code.size(); pc++) {
		const Instruction &in = p_function.code[pc];
		const uint32_t from = in.op == Op::Move ? in.b : in.op == Op::SetField || in.op == Op::VecSet ? in.c : in.op == Op::SetGlobal ? in.b : NONE;
		if (from == NONE || from < p_function.parameters || !p_owned[from] || writes[from] != 1 || last_read[from] != pc) {
			continue;
		}
		const uint32_t made = written_at[from];
		// In a stripped module a Move destination may be a local whose name was omitted.
		if (p_function.locals.empty() && p_function.code[made].op == Op::Move) {
			continue;
		}
		if (in.op == Op::Move && (in.a == in.b || !p_owned[in.a])) {
			continue;
		}
		bool straight = made < pc && first_read[from] > made;
		for (uint32_t between = made; straight && between < pc; between++) {
			const Op op = p_function.code[between].op;
			straight = !lands[between + 1] && op != Op::Jump && op != Op::JumpIf && op != Op::JumpIfNot;
		}
		transfers[pc] = straight;
	}
	return transfers;
}

} // namespace kas3::ir
