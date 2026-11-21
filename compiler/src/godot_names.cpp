#include "builtins.h"
#include "godot_names.h"

#include <algorithm>
#include <string>
#include <vector>

namespace kas3 {

namespace {

// The table, read on first use: the entries point into the text, which is kept for the whole run. The
// generator writes the lines sorted by key; they are sorted again here, so that a lookup is a binary search
// whatever order the file is in.
struct Table {
	std::string text;
	std::vector<GodotEntry> entries;

	Table() :
			text(godot_names_text()) {
		const std::string_view all(text);
		for (size_t at = 0; at < all.size();) {
			const size_t end = std::min(all.find('\n', at), all.size());
			const std::string_view line = all.substr(at, end - at);
			// The first '=' ends the key: a key never holds one, a value may.
			if (const size_t equals = line.find('='); equals != std::string_view::npos) {
				entries.push_back(GodotEntry{ line.substr(0, equals), line.substr(equals + 1) });
			}
			at = end + 1;
		}
		std::stable_sort(entries.begin(), entries.end(), [](const GodotEntry &p_a, const GodotEntry &p_b) { return p_a.key < p_b.key; });
	}
};

const Table &table() {
	static const Table made;
	return made;
}

} // namespace

std::optional<uint32_t> godot_entry(std::string_view p_key) {
	const std::vector<GodotEntry> &entries = table().entries;
	auto found = std::lower_bound(entries.begin(), entries.end(), p_key, [](const GodotEntry &p_entry, std::string_view p_wanted) {
		return p_entry.key < p_wanted;
	});
	if (found == entries.end() || found->key != p_key) {
		return std::nullopt;
	}
	return static_cast<uint32_t>(found - entries.begin());
}

GodotEntry godot_entry_at(uint32_t p_id) {
	return table().entries[p_id];
}

std::optional<uint32_t> godot_operator(std::string_view p_name) {
	// The operators' entries sit together, sorted by their key's prefix: a few dozen to look through.
	constexpr std::string_view prefix = "@operator.";
	const std::vector<GodotEntry> &entries = table().entries;
	auto at = std::lower_bound(entries.begin(), entries.end(), prefix, [](const GodotEntry &p_entry, std::string_view p_wanted) {
		return p_entry.key < p_wanted;
	});
	for (; at != entries.end() && at->key.substr(0, prefix.size()) == prefix; ++at) {
		if (at->value.substr(0, at->value.find(' ')) == p_name) {
			return static_cast<uint32_t>(at - entries.begin());
		}
	}
	return std::nullopt;
}

std::optional<std::string_view> godot_value(std::string_view p_key) {
	const std::optional<uint32_t> id = godot_entry(p_key);
	return id ? std::optional<std::string_view>(godot_entry_at(*id).value) : std::nullopt;
}

std::optional<std::string> godot_name(std::string_view p_class, std::string_view p_member) {
	std::string key(p_class);
	if (!p_member.empty()) {
		key += '.';
		key += p_member;
	}
	const std::optional<uint32_t> id = godot_entry(key);
	if (!id) {
		return std::nullopt;
	}
	// Godot's own name is the value's first word; what follows says what the member is.
	const std::string_view value = godot_entry_at(*id).value;
	return std::string(value.substr(0, value.find(' ')));
}

} // namespace kas3
