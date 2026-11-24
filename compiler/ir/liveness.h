// Which registers still hold a value that a later instruction reads.
#pragma once

#include "register_uses.h"
#include "sparse_set.h"

namespace kas3::ir {

// Liveness of the tracked registers of one function, solved over basic blocks.
//
// A register is live where some path reads it before writing it. An instruction inside a try range may
// throw before it writes its result, so whatever is live at the handler's target is live at every
// instruction of the range, and a write there does not end it.
//
// Most temporaries are written and read inside one block. Only a register some block reads before
// writing it can be live at a block boundary, so only those get a bit in the set kept for each block.
// The others need no more than a flag while their own block is walked. Of those that do cross, few are
// live at any one place, so a block's set is kept as its words that are not empty: a function of many
// thousands of statements would otherwise need a full row of bits for each of its blocks.
//
// One object serves any number of functions and keeps its storage between them: a program has many
// small functions, and allocating for each costs more than solving it.
class Liveness {
public:
	// Solves for a function, which must stay as it is while the answers are read.
	void solve(const Function &p_function, const std::vector<bool> &p_tracked);

	// Calls p_visit(pc, ending) for every instruction. `ending` has the tracked registers the instruction
	// reads or writes that hold nothing read after it, in no order, and some perhaps twice. Blocks come
	// in order, and the instructions of a block from its last to its first.
	template <typename Visit>
	void each_instruction(Visit &&p_visit);

	// For use in a visit: appends, in no order, the registers live after the last instruction of the
	// block being visited that are not live on entry to p_pc. Nothing is, unless a block starts there: for
	// the instruction after the last of a block, or a jump's target.
	void dying_into(uint32_t p_pc, std::vector<uint32_t> &r_registers) {
		if (const uint32_t block = block_at_[p_pc]; block != NONE) {
			const SparseSet entry = live_in_[block];
			leaving_.each_word([&](uint32_t p_index, uint64_t p_bits) {
				each_member(p_index, p_bits & ~entry.bits_at(p_index), [&](uint32_t p_bit) { r_registers.push_back(register_of_[p_bit]); });
			});
		}
	}

	// For each handler, in register order: the tracked registers that may hold a value when control
	// leaves the handler's range for its target, where nothing reads them.
	void abandoned_to_handlers(std::vector<std::vector<uint32_t>> &r_registers);

	// Whether one jump or one instruction before it leads to the instruction, and no handler does.
	bool entered_once(uint32_t p_pc) const {
		const uint32_t block = block_at_[p_pc];
		return block == NONE || entered_[block] == 1;
	}

private:
	static constexpr uint32_t LOCAL = NONE - 1; // Tracked, and never live at a block boundary.

	// A run of blocks covered by the same handlers. It stands in the flow graph as a node every one of
	// its blocks may leave for, whose successors are its handlers' targets.
	struct Region {
		uint32_t first_block = 0;
		uint32_t end_block = 0;
		uint32_t handlers = 0; // Where its part of region_handlers_ starts: the next region's ends it.
		uint32_t mentions = 0; // The same, of region_mentions_.
	};

	uint32_t region_node(uint32_t p_region) const { return blocks_ + 1 + p_region; }
	uint32_t regions() const { return static_cast<uint32_t>(regions_.size()) - 1; }

	void find_blocks();
	void find_regions();
	void summarize(const std::vector<bool> &p_tracked);
	void number();
	void propagate();
	void leave(uint32_t p_block);
	void step_back(const Instruction &p_in);

	const Function *function_ = nullptr;

	// A block is a run of instructions entered only at its first and left only at its last, all inside
	// the same try ranges. The end of the code is one more, empty block, numbered after the others.
	std::vector<uint32_t> block_start_; // By block, the end among them.
	std::vector<uint32_t> block_at_; // By instruction: the block that starts there, or NONE.
	uint32_t blocks_ = 0;
	// By block: the nodes it may continue at. Each is NONE where there is none: the block after it, its
	// jump's target, and its region.
	std::vector<uint32_t> next_;
	std::vector<uint32_t> target_;
	std::vector<uint32_t> handled_by_;
	std::vector<uint8_t> entered_; // By block: 0, 1 for one jump or fall into it, or 2 for anything else.

	std::vector<Region> regions_; // In the order of the code, and one more that ends the last.
	std::vector<uint32_t> region_handlers_;
	std::vector<uint32_t> region_mentions_; // The tracked registers each region reads or writes, once each.

	std::vector<uint32_t> bit_; // By register: its bit, LOCAL, or NONE if it is not tracked.
	std::vector<uint32_t> register_of_; // By bit, in register order.
	// What each block does to the registers that cross block boundaries, as bits: those it reads before
	// any write, and those it writes. One block's follow another's.
	std::vector<uint32_t> exposed_;
	std::vector<uint32_t> exposed_start_;
	std::vector<uint32_t> written_;
	std::vector<uint32_t> written_start_;
	// By register, for `summarize`: the last block or region that noted it, plus one.
	std::vector<uint32_t> written_in_;
	std::vector<uint32_t> exposed_in_;
	std::vector<uint32_t> mentioned_in_;

	SetTable live_in_; // By node: what is live on entry to a block, or at a region's targets.

	WorkingSet live_; // The crossing registers live where the solver or a walk has got to.
	WorkingSet merged_;
	// For a walk: what is live after the last instruction of its block, what the handlers around the
	// block read, the registers that stay inside a block and are live, and what ends at an instruction.
	WorkingSet leaving_;
	SparseSet handled_;
	std::vector<uint8_t> local_; // By register: a block ends with none set, as it starts.
	std::vector<uint32_t> ending_;
};

// After the block's last instruction, what is live is what is on entry to any successor, or at a
// handler's target.
inline void Liveness::leave(uint32_t p_block) {
	const uint32_t region = handled_by_[p_block];
	leaving_.clear();
	for (uint32_t node : { next_[p_block], target_[p_block], region }) {
		if (node != NONE) {
			leaving_.add(live_in_[node]);
		}
	}
	live_.clear();
	live_.add(leaving_.settled());
	handled_ = region == NONE ? SparseSet() : live_in_[region];
}

// Moves from the point after an instruction to the point before it, and notes what ends at it.
inline void Liveness::step_back(const Instruction &p_in) {
	ending_.clear();
	const uint32_t written = written_by(p_in);
	bool written_ends = false;
	if (written != NONE) {
		const uint32_t bit = bit_[written];
		if (bit == LOCAL) {
			written_ends = local_[written] == 0;
			local_[written] = 0;
		} else if (bit != NONE) {
			written_ends = !live_.has(bit);
			// A handler that reads the register may run without this write having happened.
			if (!handled_.has(bit)) {
				live_.remove(bit);
			}
		}
		if (written_ends) {
			ending_.push_back(written);
		}
	}
	each_read(*function_, p_in, [&](uint32_t p_register) {
		const uint32_t bit = bit_[p_register];
		if (bit == NONE) {
			return;
		}
		// The write above has already been undone, so a register read and written is asked about apart.
		const bool live = bit == LOCAL ? local_[p_register] != 0 : live_.has(bit);
		if (p_register == written ? written_ends : !live) {
			ending_.push_back(p_register);
		}
		if (bit == LOCAL) {
			local_[p_register] = 1;
		} else {
			live_.add(bit);
		}
	});
}

template <typename Visit>
void Liveness::each_instruction(Visit &&p_visit) {
	for (uint32_t block = 0; block < blocks_; ++block) {
		leave(block);
		for (uint32_t pc = block_start_[block + 1]; pc-- > block_start_[block];) {
			step_back(function_->code[pc]);
			p_visit(pc, ending_);
		}
	}
}

} // namespace kas3::ir
