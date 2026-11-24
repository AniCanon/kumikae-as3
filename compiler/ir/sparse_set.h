// Sets of small numbers kept as bits, for sets that are nearly empty: the registers live at one point of
// a function hold a few of the thousands it may have. A set costs what it holds, not what it could hold.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace kas3::ir {

// The members of a set that share one 64-bit word.
struct SetWord {
	uint32_t index = 0;
	uint64_t bits = 0;
};

// A set as its words that are not empty, in order. It refers to words kept elsewhere.
struct SparseSet {
	const SetWord *first = nullptr;
	const SetWord *last = nullptr;

	const SetWord *begin() const { return first; }
	const SetWord *end() const { return last; }
	uint64_t bits_at(uint32_t p_index) const {
		const SetWord *found = std::lower_bound(first, last, p_index, [](const SetWord &p_word, uint32_t p_wanted) { return p_word.index < p_wanted; });
		return found != last && found->index == p_index ? found->bits : 0;
	}
	bool has(uint32_t p_member) const { return bits_at(p_member / 64) >> (p_member % 64) & 1; }
};

// Calls p_visit with each member of a word, in order.
template <typename Visit>
void each_member(uint32_t p_index, uint64_t p_bits, Visit &&p_visit) {
	for (; p_bits != 0; p_bits &= p_bits - 1) {
		p_visit(p_index * 64 + static_cast<uint32_t>(__builtin_ctzll(p_bits)));
	}
}

// A set that is being changed. It has every word, and a list of those that may not be empty, so that
// reading and clearing it take no longer than changing it did.
class WorkingSet {
public:
	// Empties the set and makes room for members below p_members.
	void resize(size_t p_members) {
		words_.assign((p_members + 63) / 64, 0);
		touched_.clear();
	}
	void clear() {
		for (uint32_t index : touched_) {
			words_[index] = 0;
		}
		touched_.clear();
	}
	bool has(uint32_t p_member) const { return words_[p_member / 64] >> (p_member % 64) & 1; }
	void add(uint32_t p_member) { word(p_member / 64) |= uint64_t(1) << (p_member % 64); }
	void remove(uint32_t p_member) { words_[p_member / 64] &= ~(uint64_t(1) << (p_member % 64)); }
	void add(SparseSet p_set) {
		for (const SetWord &from : p_set) {
			word(from.index) |= from.bits;
		}
	}
	void remove(SparseSet p_set) {
		for (const SetWord &from : p_set) {
			words_[from.index] &= ~from.bits;
		}
	}
	// Calls p_visit(index, bits) for each word that is not empty, in no order.
	template <typename Visit>
	void each_word(Visit &&p_visit) {
		forget_emptied();
		for (uint32_t index : touched_) {
			p_visit(index, words_[index]);
		}
	}
	// The set as it is now, in order. It lasts until the next call of this.
	SparseSet settled() {
		forget_emptied();
		settled_.clear();
		for (uint32_t index : touched_) {
			settled_.push_back({ index, words_[index] });
		}
		return { settled_.data(), settled_.data() + settled_.size() };
	}

private:
	uint64_t &word(uint32_t p_index) {
		if (words_[p_index] == 0) {
			touched_.push_back(p_index);
		}
		return words_[p_index];
	}
	// A word emptied by `remove` stays listed, and is listed again if it is then added to.
	void forget_emptied() {
		if (touched_.empty()) {
			return;
		}
		std::sort(touched_.begin(), touched_.end());
		touched_.erase(std::unique(touched_.begin(), touched_.end()), touched_.end());
		touched_.erase(std::remove_if(touched_.begin(), touched_.end(), [&](uint32_t p_index) { return words_[p_index] == 0; }), touched_.end());
	}

	std::vector<uint64_t> words_;
	std::vector<uint32_t> touched_;
	std::vector<SetWord> settled_;
};

// Many sets that only grow, kept one after another in one piece of storage.
class SetTable {
public:
	// Starts again with p_sets empty sets.
	void reset(size_t p_sets) {
		places_.assign(p_sets, {});
		words_.clear();
	}
	SparseSet operator[](size_t p_set) const {
		const Place &place = places_[p_set];
		return { words_.data() + place.start, words_.data() + place.start + place.count };
	}
	// Replaces a set by one that has all its members, which must be kept elsewhere. Says whether any are new.
	bool grow(size_t p_set, SparseSet p_to) {
		Place &place = places_[p_set];
		const auto count = static_cast<uint32_t>(p_to.end() - p_to.begin());
		const SetWord *now = words_.data() + place.start;
		if (count == place.count && std::equal(p_to.begin(), p_to.end(), now, [](const SetWord &p_a, const SetWord &p_b) { return p_a.bits == p_b.bits; })) {
			return false;
		}
		if (count > place.capacity) {
			// Twice the room each time, so that a set that grows a word at a time is not copied each time.
			place.capacity = std::max(count, place.capacity * 2);
			place.start = static_cast<uint32_t>(words_.size());
			words_.resize(words_.size() + place.capacity);
		}
		std::copy(p_to.begin(), p_to.end(), words_.begin() + place.start);
		place.count = count;
		return true;
	}

private:
	struct Place {
		uint32_t start = 0;
		uint32_t count = 0;
		uint32_t capacity = 0;
	};
	std::vector<Place> places_;
	std::vector<SetWord> words_;
};

} // namespace kas3::ir
