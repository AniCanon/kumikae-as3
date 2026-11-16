#include "thunks.h"

#include <algorithm>
#include <tuple>

namespace kas3 {
namespace backend_cpp {

using ir::NONE;
using ir::Op;

bool Thunk::operator<(const Thunk &p_other) const {
	const auto key = [](const Thunk &p_thunk) {
		return std::tie(p_thunk.op, p_thunk.a, p_thunk.b, p_thunk.c, p_thunk.kinds, p_thunk.given, p_thunk.list, p_thunk.result, p_thunk.updated,
				p_thunk.code_class);
	};
	return key(*this) < key(p_other);
}

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

ThunkUse ThunkTable::use(const ir::Function &p_function, const ir::Instruction &p_in) {
	Thunk thunk;
	ThunkUse use;
	thunk.op = p_in.op;
	// What the operation may reach by name is what the function's own code may.
	thunk.code_class = p_function.owner != NONE ? p_function.owner : p_function.written_in;
	// A register of the function as a register of the thunk: one handed in is handed in once.
	const auto handed = [&](uint32_t p_register) {
		const auto found = std::find(use.given.begin(), use.given.end(), p_register);
		if (found != use.given.end()) {
			return static_cast<uint32_t>(found - use.given.begin());
		}
		use.given.push_back(p_register);
		thunk.kinds.push_back(p_function.registers[p_register]);
		return static_cast<uint32_t>(use.given.size() - 1);
	};
	const char *shape = ir::op_shape(p_in.op);
	const bool writes_first = (shape[0] == 'R' || shape[0] == 'r') && !first_is_read(p_in.op);
	const uint32_t operands[3] = { p_in.a, p_in.b, p_in.c };
	uint32_t mapped[3] = { p_in.a, p_in.b, p_in.c };
	for (int n = 0; n < 3; n++) {
		if (n == 0 && writes_first) {
			continue;
		}
		if ((shape[n] == 'R' || shape[n] == 'r') && operands[n] != NONE) {
			mapped[n] = handed(operands[n]);
		} else if (shape[n] == 'A') {
			for (uint32_t each : p_function.argument_lists[operands[n]]) {
				thunk.list.push_back(handed(each));
			}
			mapped[n] = 0;
		}
	}
	thunk.given = static_cast<uint32_t>(use.given.size());
	if (writes_first && p_in.a != NONE) {
		// What it writes may be a register it also reads (`r1 = r1 + r2`): then the thunk's are one too.
		const auto found = std::find(use.given.begin(), use.given.end(), p_in.a);
		if (found != use.given.end()) {
			mapped[0] = static_cast<uint32_t>(found - use.given.begin());
		} else {
			mapped[0] = static_cast<uint32_t>(thunk.kinds.size());
			thunk.kinds.push_back(p_function.registers[p_in.a]);
		}
		thunk.result = mapped[0];
		use.result = p_in.a;
	}
	if (p_in.op == Op::DynNext) {
		thunk.updated = mapped[1]; // The cursor: read, and moved on.
		use.updated = p_in.b;
	}
	thunk.a = mapped[0];
	thunk.b = mapped[1];
	thunk.c = mapped[2];
	use.thunk = number_of(thunk);
	return use;
}

uint32_t ThunkTable::number_of(const Thunk &p_thunk) {
	const auto [found, added] = numbers_.emplace(p_thunk, static_cast<uint32_t>(thunks_.size()));
	if (added) {
		thunks_.push_back(p_thunk);
	}
	return found->second;
}

uint32_t ThunkTable::array_of(uint32_t p_array_class, size_t p_count) {
	Thunk thunk;
	thunk.op = Op::ArrayNew;
	const uint32_t count = static_cast<uint32_t>(p_count);
	thunk.kinds.assign(p_count, ir::Kind::Any);
	thunk.kinds.push_back(ir::Kind::Obj);
	thunk.given = count;
	for (uint32_t i = 0; i < count; i++) {
		thunk.list.push_back(i);
	}
	thunk.a = count;
	thunk.b = p_array_class;
	thunk.result = count;
	return number_of(thunk);
}

} // namespace backend_cpp
} // namespace kas3
