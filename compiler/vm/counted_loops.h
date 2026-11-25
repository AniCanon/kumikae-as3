// The increment and test of a counted int loop. Branches still enter the original IR,
// and a debugger or a pending clock check still visits the statement at the loop head.
#pragma once

#include "adjacent_uses.h"
#include "runtime.h"

namespace kas3::vm {

class CountedLoops {
	struct Step {
		uint32_t counter = ir::NONE;
		uint32_t bound = 0;
		uint32_t body = 0;
		uint32_t exit = 0;

		uint32_t advance(Slot *r_values) const {
			Slot &index = r_values[counter];
			index.i = static_cast<int32_t>(static_cast<uint32_t>(index.i) + 1u);
			return index.i < r_values[bound].i ? body : exit;
		}
	};
	std::vector<std::vector<Step>> functions_;

	static std::optional<Step> match(const ir::Module &p_module, const ir::Function &p_fn, const ir::AdjacentUses &p_uses, size_t p_pc) {
		using ir::Op;
		const auto *inc = &p_fn.code[p_pc];
		if (inc[0].op != Op::I32ToF64 || inc[1].op != Op::ConstF64 || inc[2].op != Op::AddF64 ||
				inc[3].op != Op::F64ToI32 || inc[4].op != Op::Move || inc[5].op != Op::Jump ||
				inc[1].b >= p_module.numbers.size() || p_module.numbers[inc[1].b] != 1.0 || inc[2].b != inc[0].a || inc[2].c != inc[1].a ||
				inc[3].b != inc[2].a || inc[4].b != inc[3].a || inc[4].a != inc[0].b) {
			return {};
		}
		const size_t head = inc[5].a;
		if (head >= p_pc || p_pc - head <= 4) {
			return {};
		}
		const auto *test = &p_fn.code[head];
		if (test[0].op != Op::Stmt || test[1].op != Op::I32ToF64 || test[2].op != Op::I32ToF64 ||
				test[3].op != Op::LtF64 || test[4].op != Op::JumpIfNot || test[1].b != inc[4].a ||
				test[3].b != test[1].a || test[3].c != test[2].a || test[4].a != test[3].a) {
			return {};
		}
		// No argument, local, handler, branch target or later consumer can observe the temporaries.
		for (size_t i = 0; i < 4; ++i) {
			if (!p_uses.only_consumer(inc[i].a, p_pc + i + 1)) {
				return {};
			}
		}
		for (size_t i = 1; i <= 3; ++i) {
			if (!p_uses.only_consumer(test[i].a, head + i + 1)) {
				return {};
			}
		}
		return Step{ inc[4].a, test[2].b, static_cast<uint32_t>(head + 5), test[4].b };
	}

public:
	explicit CountedLoops(const ir::Module &p_module) : functions_(p_module.functions.size()) {
		for (size_t f = 0; f < functions_.size(); ++f) {
			const auto &fn = p_module.functions[f];
			std::optional<ir::AdjacentUses> uses;
			for (size_t pc = 5; pc < fn.code.size(); ++pc) {
				if (fn.code[pc].op != ir::Op::Jump || fn.code[pc - 5].op != ir::Op::I32ToF64) {
					continue;
				}
				if (!uses) {
					uses.emplace(fn);
				}
				if (auto step = match(p_module, fn, *uses, pc - 5)) {
					functions_[f].resize(pc - 4);
					functions_[f].back() = *step;
				}
			}
		}
	}
	const Step *at(uint32_t p_function, uint32_t p_pc) const {
		const auto &steps = functions_[p_function];
		return p_pc < steps.size() && steps[p_pc].counter != ir::NONE ? &steps[p_pc] : nullptr;
	}
};

} // namespace kas3::vm
