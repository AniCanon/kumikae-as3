#include "thunks.h"

#include <algorithm>
#include <tuple>

namespace kas3 {
namespace backend_cpp {

using ir::NONE;
using ir::Op;

bool Thunk::operator<(const Thunk &p_other) const {
	const auto key = [](const Thunk &p_thunk) {
		return std::tie(p_thunk.op, p_thunk.a, p_thunk.b, p_thunk.c, p_thunk.kinds, p_thunk.types, p_thunk.given, p_thunk.list, p_thunk.result, p_thunk.updated,
				p_thunk.code_class, p_thunk.self);
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

ThunkUse ThunkTable::use(const ir::Function &p_function, const ir::Instruction &p_in) {
	Thunk thunk;
	ThunkUse use;
	thunk.op = p_in.op;
	// What the operation may reach by name is what the function's own code may.
	thunk.code_class = p_function.owner != NONE ? p_function.owner : p_function.written_in;
	const auto type_of = [&](uint32_t p_register) {
		return p_register < p_function.engine_types.size() ? p_function.engine_types[p_register] : std::string();
	};
	// A register of the function as a register of the thunk: one handed in is handed in once.
	const auto handed = [&](uint32_t p_register) {
		const auto found = std::find(use.given.begin(), use.given.end(), p_register);
		if (found != use.given.end()) {
			return static_cast<uint32_t>(found - use.given.begin());
		}
		use.given.push_back(p_register);
		thunk.kinds.push_back(p_function.registers[p_register]);
		thunk.types.push_back(type_of(p_register));
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
	// What asks the engine from a method hands the method's object in too: the engine's side may ask whose
	// code is asking (the listeners a script added are its own).
	const bool engine = shape[0] == 'E' || shape[1] == 'E' || shape[2] == 'E';
	if (engine && p_function.owner != NONE && !p_function.is_static && p_function.parameters > 0) {
		thunk.self = handed(0);
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
			thunk.types.push_back(type_of(p_in.a));
		}
		thunk.result = mapped[0];
		use.result = p_in.a;
	}
	if (const uint32_t changed = changed_in_place(p_function, p_in); changed != NONE) {
		thunk.updated = handed(changed);
		use.updated = changed;
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

namespace {

uint32_t index_in(std::vector<uint32_t> &r_list, uint32_t p_value) {
	const auto found = std::find(r_list.begin(), r_list.end(), p_value);
	if (found != r_list.end()) {
		return static_cast<uint32_t>(found - r_list.begin());
	}
	r_list.push_back(p_value);
	return static_cast<uint32_t>(r_list.size() - 1);
}

} // namespace

uint32_t ThunkTable::use_index(uint32_t p_thunk) {
	return index_in(uses_, p_thunk);
}

uint32_t ThunkTable::name_index(uint32_t p_string) {
	return index_in(names_, p_string);
}

uint32_t ThunkTable::function_value(uint32_t p_function, bool p_bound) {
	Thunk thunk;
	thunk.op = Op::MakeFunction;
	thunk.kinds.assign(p_bound ? 2 : 1, ir::Kind::Obj);
	thunk.given = p_bound ? 1 : 0;
	thunk.a = thunk.given;
	thunk.b = p_function;
	thunk.c = p_bound ? 0 : NONE;
	thunk.result = thunk.given;
	return number_of(thunk);
}

uint32_t ThunkTable::from_any(ir::Kind p_kind) {
	Thunk thunk;
	thunk.op = Op::FromAny;
	thunk.kinds = { ir::Kind::Any, p_kind };
	thunk.given = 1;
	thunk.a = 1;
	thunk.b = 0;
	thunk.c = NONE;
	thunk.result = 1;
	return number_of(thunk);
}

} // namespace backend_cpp
} // namespace kas3
