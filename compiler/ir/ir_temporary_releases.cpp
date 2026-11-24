#include "temporary_releases.h"

#include <algorithm>

namespace kas3::ir {

namespace {

// Only the edge that leaves the value behind enters its cleanup instructions.
template <typename Registers>
uint32_t cleanup(std::vector<Instruction> &r_code, uint32_t p_target, const Registers &p_registers) {
	if (p_registers.begin() == p_registers.end()) {
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

void TemporaryReleases::find_temporaries(const Function &p_function, bool p_stripped) {
	const size_t registers = p_function.registers.size();
	owned_.resize(registers);
	for (size_t reg = 0; reg < registers; ++reg) {
		owned_[reg] = is_held(p_function.registers[reg]);
	}
	temporary_ = owned_;
	for (uint32_t reg = 0; reg < p_function.parameters; ++reg) {
		temporary_[reg] = false;
	}
	for (const auto &local : p_function.locals) {
		temporary_[local.reg] = false;
	}
	for (const auto &handler : p_function.handlers) {
		temporary_[handler.reg] = false;
	}
	// Older modules can omit local names. A Move destination may then be a source local.
	if (p_stripped && p_function.locals.empty()) {
		for (const auto &in : p_function.code) {
			if (in.op == Op::Move) {
				temporary_[in.a] = false;
			}
		}
	}

	// A literal string belongs to the module, and a bare math handle contains only scalar parts.
	// Keeping either available lets a warmed loop reuse its storage.
	const auto bare_math = [&](uint32_t p_reg) {
		return p_reg < p_function.engine_types.size() && p_function.registers[p_reg] == Kind::Val &&
				bare_named(p_function.engine_types[p_reg]) != Bare::None;
	};
	can_hold_.assign(registers, false);
	for (const auto &in : p_function.code) {
		const uint32_t reg = written_by(in);
		if (reg != NONE && in.op != Op::ConstStr && in.op != Op::AnyConst && in.op != Op::ConstNull &&
				!(in.op == Op::ToAny && (!is_held(p_function.registers[in.b]) || bare_math(in.b)))) {
			can_hold_[reg] = true;
		}
	}
	for (uint32_t reg = 0; reg < registers; ++reg) {
		temporary_[reg] = temporary_[reg] && can_hold_[reg] && !bare_math(reg);
	}
}

void TemporaryReleases::plan(const Function &p_function) {
	const auto &code = p_function.code;
	live_.solve(p_function, temporary_);
	transfers_.assign(code.size(), false);
	release_.resize(code.size());
	registers_.clear();
	taken_.clear();
	live_.each_instruction([&](uint32_t p_pc, const std::vector<uint32_t> &p_ending) {
		const auto &in = code[p_pc];
		// What the instruction uses for the last time, and what is live after it and not where p_next starts.
		const auto ending_before = [&](uint32_t p_next) {
			const auto first = static_cast<std::ptrdiff_t>(registers_.size());
			registers_.insert(registers_.end(), p_ending.begin(), p_ending.end());
			live_.dying_into(p_next, registers_);
			const auto ending = registers_.begin() + first;
			if (registers_.end() - ending > 1) {
				std::sort(ending, registers_.end());
				registers_.erase(std::unique(ending, registers_.end()), registers_.end());
			}
			return Span{ static_cast<uint32_t>(first), static_cast<uint32_t>(registers_.size() - first) };
		};
		if (in.op == Op::JumpIf || in.op == Op::JumpIfNot) {
			taken_.push_back({ p_pc, ending_before(in.b) });
		}
		// A return or a throw hands its operand on, and nothing runs after an instruction that leaves.
		if (!falls_through(in.op)) {
			release_[p_pc] = {};
			return;
		}
		Span ending = ending_before(p_pc + 1);
		if (ending.count != 0 && stored_by(in) != NONE) {
			const auto found = std::find(registers_.end() - ending.count, registers_.end(), stored_by(in));
			if (found != registers_.end() && (in.op != Op::Move || (owned_[in.a] && in.a != in.b))) {
				transfers_[p_pc] = true;
				registers_.erase(found);
				--ending.count;
			}
		}
		release_[p_pc] = ending;
	});
	live_.abandoned_to_handlers(caught_);
}

// Copies the code with its releases in place, and notes where each instruction went.
void TemporaryReleases::write(const Function &p_function, std::vector<Instruction> &r_code) {
	const auto &old = p_function.code;
	positions_.clear();
	// A string has no destructor that can run user code. Its release waits until after a Move that
	// follows its producer, so both back ends can write the result straight into the destination.
	waiting_.clear();
	for (uint32_t pc = 0; pc < old.size(); ++pc) {
		const Instruction &in = old[pc];
		const uint32_t written = static_cast<uint32_t>(r_code.size());
		positions_.push_back(written);
		r_code.push_back(in);
		if (transfers_[pc]) {
			switch (in.op) {
				case Op::Move: r_code.back().op = Op::TakeMove; break;
				case Op::SetField: r_code.back().op = Op::TakeField; break;
				case Op::SetGlobal: r_code.back().op = Op::TakeGlobal; break;
				case Op::VecSet: r_code.back().op = Op::TakeElement; break;
				default: break;
			}
		}
		if (release_[pc].count == 0 && waiting_.empty()) {
			continue;
		}
		const bool before_move = pc + 1 < old.size() && old[pc + 1].op == Op::Move && live_.entered_once(pc + 1);
		const auto release = [&](uint32_t p_reg) {
			if (before_move && p_function.registers[p_reg] == Kind::Str && p_reg != old[pc + 1].a && p_reg != old[pc + 1].b) {
				later_.push_back(p_reg);
			} else if (p_reg == in.a && op_shape(in.op)[0] == 'r') {
				r_code[written].a = NONE;
			} else {
				r_code.push_back({ Op::Release, p_reg, 0, 0, in.position });
			}
		};
		for (uint32_t reg : listed(release_[pc])) {
			release(reg);
		}
		for (uint32_t reg : waiting_) {
			release(reg);
		}
		waiting_.clear();
		waiting_.swap(later_);
	}
	positions_.push_back(static_cast<uint32_t>(r_code.size()));
}

void TemporaryReleases::insert(Function &r_function, bool p_stripped) {
	find_temporaries(r_function, p_stripped);
	plan(r_function);
	std::vector<Instruction> code;
	write(r_function, code);
	for (auto &in : code) {
		if (in.op == Op::Jump) {
			in.a = positions_[in.a];
		} else if (in.op == Op::JumpIf || in.op == Op::JumpIfNot) {
			in.b = positions_[in.b];
		}
	}
	for (const auto &[pc, registers] : taken_) {
		const uint32_t target = cleanup(code, code[positions_[pc]].b, listed(registers));
		code[positions_[pc]].b = target;
	}
	for (size_t i = 0; i < r_function.handlers.size(); ++i) {
		auto &handler = r_function.handlers[i];
		handler.start = positions_[handler.start];
		handler.end = positions_[handler.end];
		handler.target = cleanup(code, positions_[handler.target], caught_[i]);
	}
	for (auto &[pc, klass] : r_function.virtual_receivers) {
		pc = positions_[pc];
	}
	r_function.code = std::move(code);
}

} // namespace kas3::ir
