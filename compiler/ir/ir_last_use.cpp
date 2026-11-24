#include "last_use.h"

#include <algorithm>
#include <unordered_set>

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

namespace {

// Find where a register must still own its value, tracing reads back to the instructions
// that supply them. Only held temporary registers enter the worklist.
struct Uses {
	std::vector<uint32_t> read;
	uint32_t written = NONE;
	std::vector<uint32_t> previous;
	std::vector<uint32_t> exceptional;
};

std::vector<Uses> uses_of(const Function &p_function) {
	std::vector<Uses> uses(p_function.code.size() + 1);
	for (uint32_t pc = 0; pc < p_function.code.size(); ++pc) {
		const auto &in = p_function.code[pc];
		const char *shape = op_shape(in.op);
		const uint32_t operands[] = { in.a, in.b, in.c };
		for (size_t n = 0; n < 3; ++n) {
			const auto reg = operands[n];
			if (shape[n] == 'L') {
				uses[reg].previous.push_back(pc);
			} else if (shape[n] == 'A') {
				const auto &args = p_function.argument_lists[reg];
				uses[pc].read.insert(uses[pc].read.end(), args.begin(), args.end());
			} else if ((shape[n] == 'R' || shape[n] == 'r') && reg != NONE) {
				if (n == 0 && !first_is_read(in.op)) {
					uses[pc].written = reg;
				} else {
					uses[pc].read.push_back(reg);
				}
			}
		}
		if (in.op != Op::Jump && in.op != Op::Return && in.op != Op::ReturnVoid && in.op != Op::Throw && in.op != Op::Rethrow) {
			uses[pc + 1].previous.push_back(pc);
		}
	}
	for (const auto &handler : p_function.handlers) {
		for (uint32_t pc = handler.start; pc < handler.end; ++pc) {
			uses[handler.target].exceptional.push_back(pc);
		}
	}
	return uses;
}

uint32_t stored(const Instruction &p_in) {
	switch (p_in.op) {
		case Op::Move: case Op::SetGlobal: return p_in.b;
		case Op::SetField: case Op::VecSet: return p_in.c;
		default: return NONE;
	}
}

using Registers = std::vector<std::unordered_set<uint32_t>>;

struct Live {
	Registers before;
	Registers after;
};

Live liveness(const std::vector<Uses> &p_uses, const std::vector<bool> &p_temporary) {
	Registers after(p_uses.size()), before(p_uses.size());
	std::vector<std::pair<uint32_t, uint32_t>> pending;
	for (uint32_t pc = 0; pc < p_uses.size(); ++pc) {
		for (auto reg : p_uses[pc].read) {
			if (p_temporary[reg]) {
				pending.emplace_back(pc, reg);
			}
		}
	}
	while (!pending.empty()) {
		const auto [pc, reg] = pending.back();
		pending.pop_back();
		if (!before[pc].insert(reg).second) {
			continue;
		}
		for (uint32_t prev : p_uses[pc].previous) {
			if (after[prev].insert(reg).second && p_uses[prev].written != reg) {
				pending.emplace_back(prev, reg);
			}
		}
		for (uint32_t prev : p_uses[pc].exceptional) {
			after[prev].insert(reg);
			pending.emplace_back(prev, reg); // A failed instruction need not have written its result.
		}
	}
	return { std::move(before), std::move(after) };
}

struct Lifetime {
	std::vector<bool> transfers;
	std::vector<std::vector<uint32_t>> release;
	std::vector<std::vector<uint32_t>> taken;
	std::vector<std::vector<uint32_t>> caught;
};

std::vector<bool> temporaries(const Function &p_function, const std::vector<bool> &p_owned, const std::vector<Uses> &p_uses, bool p_stripped) {
	auto temporary = p_owned;
	for (uint32_t reg = 0; reg < p_function.parameters; ++reg) {
		temporary[reg] = false;
	}
	for (const auto &local : p_function.locals) {
		temporary[local.reg] = false;
	}
	for (const auto &handler : p_function.handlers) {
		temporary[handler.reg] = false;
	}
	// Older modules can omit local names. A Move destination may then be a source local.
	if (p_stripped && p_function.locals.empty()) {
		for (const auto &in : p_function.code) {
			if (in.op == Op::Move) {
				temporary[in.a] = false;
			}
		}
	}

	// A literal string belongs to the module, and a bare math handle contains only scalar parts.
	// Keeping either available lets a warmed loop reuse its storage.
	const auto bare_math = [&](uint32_t reg) {
		return reg < p_function.engine_types.size() && p_function.registers[reg] == Kind::Val &&
				bare_named(p_function.engine_types[reg]) != Bare::None;
	};
	std::vector<bool> can_hold(temporary.size());
	for (size_t pc = 0; pc < p_function.code.size(); ++pc) {
		const auto &in = p_function.code[pc];
		const uint32_t reg = p_uses[pc].written;
		if (reg != NONE && in.op != Op::ConstStr && in.op != Op::AnyConst && in.op != Op::ConstNull &&
				!(in.op == Op::ToAny && (!is_held(p_function.registers[in.b]) || bare_math(in.b)))) {
			can_hold[reg] = true;
		}
	}
	for (size_t reg = 0; reg < temporary.size(); ++reg) {
		const bool math = bare_math(static_cast<uint32_t>(reg));
		temporary[reg] = temporary[reg] && can_hold[reg] && !math;
	}
	return temporary;
}

void edge_deaths(const std::unordered_set<uint32_t> &p_live, const std::unordered_set<uint32_t> &p_needed, std::vector<uint32_t> &r_deaths) {
	for (auto reg : p_live) {
		if (!p_needed.count(reg)) {
			r_deaths.push_back(reg);
		}
	}
	std::sort(r_deaths.begin(), r_deaths.end());
}

Lifetime lifetimes(const Function &p_function, const std::vector<bool> &p_owned, bool p_stripped) {
	const auto uses = uses_of(p_function);
	const auto temporary = temporaries(p_function, p_owned, uses, p_stripped);
	const auto live = liveness(uses, temporary);
	Lifetime result;
	result.transfers.resize(p_function.code.size());
	result.release.resize(p_function.code.size());
	result.taken.resize(p_function.code.size());
	for (uint32_t pc = 0; pc < p_function.code.size(); ++pc) {
		const auto &in = p_function.code[pc];
		auto &release = result.release[pc];
		const auto consider = [&](uint32_t p_reg) {
			if (p_reg != NONE && temporary[p_reg] && !live.after[pc].count(p_reg) && std::find(release.begin(), release.end(), p_reg) == release.end()) {
				release.push_back(p_reg);
			}
		};
		for (auto reg : uses[pc].read) {
			consider(reg);
		}
		consider(uses[pc].written);
		if (in.op == Op::JumpIf || in.op == Op::JumpIfNot) {
			edge_deaths(live.after[pc], live.before[in.b], result.taken[pc]);
		}
		if (in.op != Op::Jump) {
			edge_deaths(live.after[pc], live.before[pc + 1], release);
		}
		const uint32_t from = stored(in);
		const auto found = std::find(release.begin(), release.end(), from);
		if (found != release.end() && (in.op != Op::Move || (p_owned[in.a] && in.a != in.b))) {
			result.transfers[pc] = true;
			release.erase(found);
		}
	}
	// A string has no destructor that can run user code. Keep a producer and its consuming
	// Move adjacent so both back ends can write the result straight into the destination.
	for (size_t pc = 1; pc < p_function.code.size(); ++pc) {
		const auto &move = p_function.code[pc];
		auto &previous = result.release[pc - 1];
		if (move.op != Op::Move || previous.empty() || uses[pc].previous.size() != 1 || !uses[pc].exceptional.empty()) {
			continue;
		}
		auto &following = result.release[pc];
		previous.erase(std::remove_if(previous.begin(), previous.end(), [&](uint32_t p_reg) {
			if (p_function.registers[p_reg] != Kind::Str || p_reg == move.a || p_reg == move.b) {
				return false;
			}
			following.push_back(p_reg);
			return true;
		}), previous.end());
	}
	for (const auto &handler : p_function.handlers) {
		std::unordered_set<uint32_t> abandoned;
		for (uint32_t pc = handler.start; pc < handler.end; ++pc) {
			abandoned.insert(live.before[pc].begin(), live.before[pc].end());
			const uint32_t reg = uses[pc].written;
			if (reg != NONE && temporary[reg]) {
				abandoned.insert(reg);
			}
		}
		edge_deaths(abandoned, live.before[handler.target], result.caught.emplace_back());
	}
	return result;
}

// Only the edge that leaves the value behind enters its cleanup instructions.
uint32_t cleanup(std::vector<Instruction> &r_code, uint32_t p_target, const std::vector<uint32_t> &p_registers) {
	if (p_registers.empty()) {
		return p_target;
	}
	const uint32_t start = static_cast<uint32_t>(r_code.size());
	const uint32_t position = r_code[p_target].position;
	for (auto reg : p_registers) {
		r_code.push_back({ Op::Release, reg, 0, 0, position });
	}
	r_code.push_back({ Op::Jump, p_target, 0, 0, position });
	return start;
}

} // namespace

std::vector<bool> last_stores(const Function &p_function, const std::vector<bool> &p_candidate) {
	auto last = p_candidate;
	if (std::none_of(last.begin(), last.end(), [](bool p_value) { return p_value; })) {
		return last;
	}
	std::vector<bool> sources(p_function.registers.size());
	for (size_t pc = 0; pc < last.size(); ++pc) {
		if (last[pc]) {
			sources[stored(p_function.code[pc])] = true;
		}
	}
	const auto live = liveness(uses_of(p_function), sources);
	for (size_t pc = 0; pc < last.size(); ++pc) {
		if (last[pc]) {
			last[pc] = !live.after[pc].count(stored(p_function.code[pc]));
		}
	}
	return last;
}

void release_temporaries(Function &r_function, bool p_stripped) {
	std::vector<bool> held;
	for (Kind kind : r_function.registers) {
		held.push_back(is_held(kind));
	}
	const auto plan = lifetimes(r_function, held, p_stripped);
	std::vector<uint32_t> positions;
	std::vector<Instruction> code;
	for (uint32_t pc = 0; pc < r_function.code.size(); ++pc) {
		const Instruction &in = r_function.code[pc];
		positions.push_back(static_cast<uint32_t>(code.size()));
		code.push_back(in);
		if (plan.transfers[pc]) {
			switch (in.op) {
				case Op::Move: code.back().op = Op::TakeMove; break;
				case Op::SetField: code.back().op = Op::TakeField; break;
				case Op::SetGlobal: code.back().op = Op::TakeGlobal; break;
				case Op::VecSet: code.back().op = Op::TakeElement; break;
				default: break;
			}
		}
		if (in.op == Op::Return || in.op == Op::Throw || in.op == Op::Rethrow) {
			continue;
		}
		const auto written = positions.back();
		for (auto reg : plan.release[pc]) {
			if (reg == in.a && op_shape(in.op)[0] == 'r') {
				code[written].a = NONE;
				continue;
			}
			code.push_back({ Op::Release, reg, 0, 0, in.position });
		}
	}
	positions.push_back(static_cast<uint32_t>(code.size()));
	for (auto &in : code) {
		if (in.op == Op::Jump) {
			in.a = positions[in.a];
		} else if (in.op == Op::JumpIf || in.op == Op::JumpIfNot) {
			in.b = positions[in.b];
		}
	}
	for (size_t pc = 0; pc < plan.taken.size(); ++pc) {
		if (!plan.taken[pc].empty()) {
			const auto branch = positions[pc];
			const auto target = cleanup(code, code[branch].b, plan.taken[pc]);
			code[branch].b = target;
		}
	}
	for (size_t i = 0; i < r_function.handlers.size(); ++i) {
		auto &handler = r_function.handlers[i];
		handler.start = positions[handler.start];
		handler.end = positions[handler.end];
		handler.target = cleanup(code, positions[handler.target], plan.caught[i]);
	}
	for (auto &[pc, klass] : r_function.virtual_receivers) {
		pc = positions[pc];
	}
	r_function.code = std::move(code);
}

} // namespace kas3::ir
