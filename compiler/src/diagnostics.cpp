#include "diagnostics.h"

#include <algorithm>

namespace kas3 {

void Diagnostics::error(uint32_t p_offset, std::string p_message) {
	if (muted_) {
		return;
	}
	count_++;
	if ((int)items_.size() >= LIMIT) {
		return;
	}
	// Keep the list ordered by offset; equal offsets stay in arrival order.
	auto place = std::upper_bound(items_.begin(), items_.end(), p_offset,
			[](uint32_t p_value, const Diagnostic &p_item) { return p_value < p_item.offset; });
	items_.insert(place, Diagnostic{ p_offset, std::move(p_message) });
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
		out += ": error: ";
		out += item.message;
		out += '\n';
	}
	int dropped = count_ - (int)items_.size();
	if (dropped > 0) {
		out += p_source.path();
		out += ": error: too many errors (";
		out += std::to_string(dropped);
		out += " more)\n";
	}
	return out;
}

} // namespace kas3
