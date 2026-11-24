#include "liveness.h"

#include <numeric>

namespace kas3::ir {

void Liveness::solve(const Function &p_function, const std::vector<bool> &p_tracked) {
	function_ = &p_function;
	find_blocks();
	find_regions();
	summarize(p_tracked);
	number();
	live_in_.reset(size_t(blocks_) + 1 + regions());
	live_.resize(register_of_.size());
	merged_.resize(register_of_.size());
	leaving_.resize(register_of_.size());
	local_.assign(bit_.size(), 0);
	// With nothing live at any block boundary, every set stays empty.
	if (!register_of_.empty()) {
		propagate();
	}
}

void Liveness::find_blocks() {
	const auto &code = function_->code;
	const uint32_t size = static_cast<uint32_t>(code.size());
	block_at_.assign(size + 1, NONE);
	block_at_[0] = block_at_[size] = 0;
	for (uint32_t pc = 0; pc < size; ++pc) {
		const uint32_t target = jump_target(code[pc]);
		if (target != NONE) {
			block_at_[target] = 0;
		}
		if (target != NONE || !falls_through(code[pc].op)) {
			block_at_[pc + 1] = 0;
		}
	}
	for (const auto &handler : function_->handlers) {
		block_at_[handler.start] = block_at_[handler.end] = block_at_[handler.target] = 0;
	}
	block_start_.clear();
	for (uint32_t pc = 0; pc <= size; ++pc) {
		if (block_at_[pc] != NONE) {
			block_at_[pc] = static_cast<uint32_t>(block_start_.size());
			block_start_.push_back(pc);
		}
	}
	blocks_ = static_cast<uint32_t>(block_start_.size()) - 1;

	next_.assign(blocks_, NONE);
	target_.assign(blocks_, NONE);
	entered_.assign(blocks_ + 1, 0);
	const auto enter = [&](uint32_t p_block) { entered_[p_block] = static_cast<uint8_t>(std::min(entered_[p_block] + 1, 2)); };
	for (uint32_t block = 0; block < blocks_; ++block) {
		const auto &last = code[block_start_[block + 1] - 1];
		if (const uint32_t target = jump_target(last); target != NONE) {
			enter(target_[block] = block_at_[target]);
		}
		if (falls_through(last.op)) {
			enter(next_[block] = block + 1);
		}
	}
}

// Between two neighbouring ends of try ranges the same handlers cover every block, so each such stretch
// with a handler is one region.
void Liveness::find_regions() {
	const auto &handlers = function_->handlers;
	handled_by_.assign(blocks_, NONE);
	regions_.clear();
	region_handlers_.clear();
	std::vector<uint32_t> cuts;
	std::vector<uint32_t> opening;
	for (uint32_t index = 0; index < handlers.size(); ++index) {
		if (handlers[index].start < handlers[index].end) {
			cuts.push_back(handlers[index].start);
			cuts.push_back(handlers[index].end);
			opening.push_back(index);
			entered_[block_at_[handlers[index].target]] = 2;
		}
	}
	std::sort(cuts.begin(), cuts.end());
	cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
	std::stable_sort(opening.begin(), opening.end(), [&](uint32_t p_a, uint32_t p_b) { return handlers[p_a].start < handlers[p_b].start; });
	std::vector<uint32_t> open;
	size_t opened = 0;
	for (size_t cut = 0; cut + 1 < cuts.size(); ++cut) {
		open.erase(std::remove_if(open.begin(), open.end(), [&](uint32_t p_handler) { return handlers[p_handler].end <= cuts[cut]; }), open.end());
		for (; opened < opening.size() && handlers[opening[opened]].start == cuts[cut]; ++opened) {
			open.push_back(opening[opened]);
		}
		if (open.empty()) {
			continue;
		}
		const uint32_t region = static_cast<uint32_t>(regions_.size());
		regions_.push_back({ block_at_[cuts[cut]], block_at_[cuts[cut + 1]], static_cast<uint32_t>(region_handlers_.size()), 0 });
		region_handlers_.insert(region_handlers_.end(), open.begin(), open.end());
		for (uint32_t block = regions_[region].first_block; block < regions_[region].end_block; ++block) {
			handled_by_[block] = region_node(region);
		}
	}
	regions_.push_back({ blocks_, blocks_, static_cast<uint32_t>(region_handlers_.size()), 0 });
}

// One pass over the code finds which tracked registers cross a block boundary, what each block does to
// them, and what each region mentions. Registers stand for themselves until `number` gives the crossing
// ones their bits.
void Liveness::summarize(const std::vector<bool> &p_tracked) {
	const size_t registers = function_->registers.size();
	bit_.assign(registers, NONE);
	for (size_t reg = 0; reg < registers; ++reg) {
		if (p_tracked[reg]) {
			bit_[reg] = LOCAL;
		}
	}
	written_in_.assign(registers, 0);
	exposed_in_.assign(registers, 0);
	mentioned_in_.assign(registers, 0);
	exposed_.clear();
	exposed_start_.clear();
	written_.clear();
	written_start_.clear();
	region_mentions_.clear();
	uint32_t region = 0;
	for (uint32_t block = 0; block < blocks_; ++block) {
		exposed_start_.push_back(static_cast<uint32_t>(exposed_.size()));
		written_start_.push_back(static_cast<uint32_t>(written_.size()));
		while (block >= regions_[region].end_block && region < regions()) {
			++region;
		}
		if (block == regions_[region].first_block) {
			regions_[region].mentions = static_cast<uint32_t>(region_mentions_.size());
		}
		const bool handled = handled_by_[block] != NONE;
		const auto mention = [&](uint32_t p_register) {
			if (handled && mentioned_in_[p_register] != region + 1) {
				mentioned_in_[p_register] = region + 1;
				region_mentions_.push_back(p_register);
			}
		};
		for (uint32_t pc = block_start_[block]; pc < block_start_[block + 1]; ++pc) {
			const auto &in = function_->code[pc];
			each_read(*function_, in, [&](uint32_t p_register) {
				if (bit_[p_register] == NONE) {
					return;
				}
				mention(p_register);
				if (written_in_[p_register] != block + 1 && exposed_in_[p_register] != block + 1) {
					exposed_in_[p_register] = block + 1;
					exposed_.push_back(p_register);
					bit_[p_register] = 0;
				}
			});
			const uint32_t written = written_by(in);
			if (written != NONE && bit_[written] != NONE) {
				mention(written);
				if (written_in_[written] != block + 1) {
					written_in_[written] = block + 1;
					written_.push_back(written);
				}
			}
		}
	}
	exposed_start_.push_back(static_cast<uint32_t>(exposed_.size()));
	written_start_.push_back(static_cast<uint32_t>(written_.size()));
	regions_.back().mentions = static_cast<uint32_t>(region_mentions_.size());
}

void Liveness::number() {
	register_of_.clear();
	for (uint32_t reg = 0; reg < bit_.size(); ++reg) {
		if (bit_[reg] < LOCAL) {
			bit_[reg] = static_cast<uint32_t>(register_of_.size());
			register_of_.push_back(reg);
		}
	}
	for (auto &reg : exposed_) {
		reg = bit_[reg];
	}
	// A write to a register that stays inside its block changes nothing at a boundary.
	uint32_t at = 0;
	uint32_t kept = 0;
	for (uint32_t block = 0; block < blocks_; ++block) {
		for (const uint32_t end = written_start_[block + 1]; at < end; ++at) {
			if (bit_[written_[at]] != LOCAL) {
				written_[kept++] = bit_[written_[at]];
			}
		}
		written_start_[block + 1] = kept;
	}
	written_.resize(kept);
}

// Liveness flows against the code, so blocks are taken from the last to the first, and again while a
// jump backwards or a handler's target has changed what a later block starts from.
void Liveness::propagate() {
	// By block: the nodes it is entered from, a region for a handler's target.
	std::vector<std::pair<uint32_t, uint32_t>> edges;
	for (uint32_t block = 0; block < blocks_; ++block) {
		for (uint32_t node : { next_[block], target_[block] }) {
			if (node != NONE) {
				edges.emplace_back(node, block);
			}
		}
	}
	for (uint32_t region = 0; region < regions(); ++region) {
		for (uint32_t at = regions_[region].handlers; at < regions_[region + 1].handlers; ++at) {
			edges.emplace_back(block_at_[function_->handlers[region_handlers_[at]].target], region_node(region));
		}
	}
	std::vector<uint32_t> entry_start(blocks_ + 2);
	for (const auto &[into, from] : edges) {
		++entry_start[into + 1];
	}
	std::partial_sum(entry_start.begin(), entry_start.end(), entry_start.begin());
	std::vector<uint32_t> entries(edges.size());
	std::vector<uint32_t> filled(entry_start.begin(), entry_start.end() - 1);
	for (const auto &[into, from] : edges) {
		entries[filled[into]++] = from;
	}

	std::vector<bool> unsettled(blocks_, true);
	size_t waiting = blocks_;
	const auto unsettle = [&](uint32_t p_block) {
		if (!unsettled[p_block]) {
			unsettled[p_block] = true;
			++waiting;
		}
	};
	while (waiting != 0) {
		for (uint32_t block = blocks_; block-- > 0;) {
			if (!unsettled[block]) {
				continue;
			}
			unsettled[block] = false;
			--waiting;
			live_.clear();
			for (uint32_t node : { next_[block], target_[block] }) {
				if (node != NONE) {
					live_.add(live_in_[node]);
				}
			}
			for (uint32_t at = written_start_[block]; at < written_start_[block + 1]; ++at) {
				live_.remove(written_[at]);
			}
			for (uint32_t at = exposed_start_[block]; at < exposed_start_[block + 1]; ++at) {
				live_.add(exposed_[at]);
			}
			// A throw may come before any of the block's writes.
			if (handled_by_[block] != NONE) {
				live_.add(live_in_[handled_by_[block]]);
			}
			if (!live_in_.grow(block, live_.settled())) {
				continue;
			}
			for (uint32_t at = entry_start[block]; at < entry_start[block + 1]; ++at) {
				const uint32_t from = entries[at];
				if (from < blocks_) {
					unsettle(from);
					continue;
				}
				merged_.clear();
				merged_.add(live_in_[from]);
				merged_.add(live_in_[block]);
				if (live_in_.grow(from, merged_.settled())) {
					const Region &region = regions_[from - region_node(0)];
					for (uint32_t covered = region.first_block; covered < region.end_block; ++covered) {
						unsettle(covered);
					}
				}
			}
		}
	}
}

void Liveness::abandoned_to_handlers(std::vector<std::vector<uint32_t>> &r_registers) {
	const auto &handlers = function_->handlers;
	r_registers.resize(handlers.size());
	for (auto &registers : r_registers) {
		registers.clear();
	}
	if (regions() == 0) {
		return;
	}
	// By region: the crossing registers it mentions, or that are live on entry to one of its blocks.
	SetTable held;
	held.reset(regions());
	for (uint32_t region = 0; region < regions(); ++region) {
		live_.clear();
		for (uint32_t block = regions_[region].first_block; block < regions_[region].end_block; ++block) {
			live_.add(live_in_[block]);
		}
		for (uint32_t at = regions_[region].mentions; at < regions_[region + 1].mentions; ++at) {
			if (bit_[region_mentions_[at]] != LOCAL) {
				live_.add(bit_[region_mentions_[at]]);
			}
		}
		held.grow(region, live_.settled());
	}
	std::vector<uint32_t> listed_for(bit_.size()); // By register: the last handler that listed it, plus one.
	for (uint32_t index = 0; index < handlers.size(); ++index) {
		const auto &handler = handlers[index];
		if (handler.start >= handler.end) {
			continue;
		}
		auto &registers = r_registers[index];
		const uint32_t first = handled_by_[block_at_[handler.start]] - region_node(0);
		const uint32_t last = handled_by_[block_at_[handler.end] - 1] - region_node(0);
		live_.clear();
		for (uint32_t region = first; region <= last; ++region) {
			live_.add(held[region]);
		}
		live_.remove(live_in_[block_at_[handler.target]]);
		for (const SetWord &word : live_.settled()) {
			each_member(word.index, word.bits, [&](uint32_t p_bit) { registers.push_back(register_of_[p_bit]); });
		}
		const auto locals = static_cast<std::ptrdiff_t>(registers.size());
		for (uint32_t at = regions_[first].mentions; at < regions_[last + 1].mentions; ++at) {
			const uint32_t reg = region_mentions_[at];
			if (bit_[reg] == LOCAL && listed_for[reg] != index + 1) {
				listed_for[reg] = index + 1;
				registers.push_back(reg);
			}
		}
		// Registers are numbered as the code needs them, so these are nearly always in order already.
		if (!std::is_sorted(registers.begin() + locals, registers.end())) {
			std::sort(registers.begin() + locals, registers.end());
		}
		std::inplace_merge(registers.begin(), registers.begin() + locals, registers.end());
	}
}

} // namespace kas3::ir
