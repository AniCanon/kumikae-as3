// Where a function lets go of the temporaries it holds.
#pragma once

#include "liveness.h"

namespace kas3::ir {

// Rewrites functions so that each owned temporary is released when the instruction that last uses it
// completes, or handed to the store that takes it. One object serves any number of functions and keeps
// its storage between them.
class TemporaryReleases {
public:
	// p_stripped: the function came from a module that may have no table of locals.
	void insert(Function &r_function, bool p_stripped = false);

private:
	struct Span {
		uint32_t start = 0;
		uint32_t count = 0;
	};
	// One of the lists in registers_.
	struct Listed {
		const uint32_t *first;
		const uint32_t *last;
		const uint32_t *begin() const { return first; }
		const uint32_t *end() const { return last; }
	};
	Listed listed(Span p_span) const { return { registers_.data() + p_span.start, registers_.data() + p_span.start + p_span.count }; }

	void find_temporaries(const Function &p_function, bool p_stripped);
	void plan(const Function &p_function);
	void write(const Function &p_function, std::vector<Instruction> &r_code);

	std::vector<bool> owned_; // By register: it holds something that must be let go.
	std::vector<bool> temporary_; // By register: this pass decides where it is let go.
	std::vector<bool> can_hold_;
	Liveness live_;

	// The plan for one function. Every list of registers is in register order.
	std::vector<bool> transfers_; // By instruction: its store takes the value over.
	std::vector<uint32_t> registers_; // The lists below, one after another.
	std::vector<Span> release_; // By instruction: what to release once it completes.
	std::vector<std::pair<uint32_t, Span>> taken_; // A branch, and what to release where it jumps to.
	std::vector<std::vector<uint32_t>> caught_; // By handler: what to release before its target runs.

	std::vector<uint32_t> positions_; // By instruction, and for the end: where the rewritten code has it.
	std::vector<uint32_t> waiting_;
	std::vector<uint32_t> later_;
};

} // namespace kas3::ir
