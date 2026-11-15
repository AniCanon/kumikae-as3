#include "diagnostics.h"

#include <algorithm>

namespace kas3 {

void Diagnostics::store(Diagnostic p_item) {
	if ((int)items_.size() >= LIMIT) {
		return;
	}
	stored_errors_ += p_item.warning ? 0 : 1;
	// Keep the list ordered by offset; equal offsets stay in arrival order.
	auto place = std::upper_bound(items_.begin(), items_.end(), p_item.offset,
			[](uint32_t p_value, const Diagnostic &p_other) { return p_value < p_other.offset; });
	items_.insert(place, std::move(p_item));
}

void Diagnostics::error(uint32_t p_offset, std::string p_message) {
	if (muted_) {
		return;
	}
	count_++;
	store(Diagnostic{ p_offset, std::move(p_message), false });
}

void Diagnostics::warning(uint32_t p_offset, std::string p_message) {
	if (!muted_) {
		store(Diagnostic{ p_offset, std::move(p_message), true });
	}
}

std::string Diagnostics::format(const Source &p_source) const {
	std::string out;
	for (const Diagnostic &item : items_) {
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
