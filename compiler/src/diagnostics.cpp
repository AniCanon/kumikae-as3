#include "diagnostics.h"

#include <algorithm>

namespace kas3 {

void Diagnostics::store(Diagnostic p_item) {
	// Past the limit the first in the file are kept, so that what is shown does not depend on the order the
	// passes run in.
	if ((int)items_.size() >= LIMIT) {
		if (p_item.offset >= items_.back().offset) {
			return;
		}
		stored_errors_ -= items_.back().warning ? 0 : 1;
		items_.pop_back();
	}
	stored_errors_ += p_item.warning ? 0 : 1;
	// Keep the list ordered by offset; equal offsets stay in arrival order.
	auto place = std::upper_bound(items_.begin(), items_.end(), p_item.offset,
			[](uint32_t p_value, const Diagnostic &p_other) { return p_value < p_other.offset; });
	items_.insert(place, std::move(p_item));
}

void Diagnostics::error(uint32_t p_offset, std::string p_message) {
	add(p_offset, std::move(p_message), false);
}

void Diagnostics::unresolved(uint32_t p_offset, std::string p_message) {
	add(p_offset, std::move(p_message), true);
}

void Diagnostics::add(uint32_t p_offset, std::string p_message, bool p_unresolved) {
	if (muted_) {
		return;
	}
	count_++;
	unresolved_ += p_unresolved ? 1 : 0;
	store(Diagnostic{ p_offset, std::move(p_message), false, p_unresolved });
}

void Diagnostics::warning(uint32_t p_offset, std::string p_message) {
	if (!muted_) {
		store(Diagnostic{ p_offset, std::move(p_message), true, false });
	}
}

void Diagnostics::drop_unresolved() {
	items_.erase(std::remove_if(items_.begin(), items_.end(), [](const Diagnostic &p_item) { return p_item.unresolved; }),
			items_.end());
	count_ -= unresolved_;
	unresolved_ = 0;
	stored_errors_ = (int)std::count_if(items_.begin(), items_.end(), [](const Diagnostic &p_item) { return !p_item.warning; });
}

void Diagnostics::clear() {
	items_.clear();
	count_ = 0;
	unresolved_ = 0;
	stored_errors_ = 0;
}

std::string Diagnostics::format(const Source &p_source, bool p_warnings) const {
	std::string out;
	for (const Diagnostic &item : items_) {
		if (item.warning && !p_warnings) {
			continue;
		}
		Position at = p_source.position(item.offset);
		out += p_source.path();
		out += ':';
		out += std::to_string(at.line);
		out += ':';
		out += std::to_string(at.column);
		out += item.warning ? ": warning: " : ": error: ";
		out += item.message;
		out += '\n';
	}
	int dropped = this->dropped();
	if (dropped > 0) {
		out += p_source.path();
		out += ": error: too many errors (";
		out += std::to_string(dropped);
		out += " more)\n";
	}
	return out;
}

} // namespace kas3
