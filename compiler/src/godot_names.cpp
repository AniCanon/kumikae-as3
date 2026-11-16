#include "builtins.h"
#include "godot_names.h"

#include <algorithm>
#include <string>

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

std::optional<std::string_view> godot_value(std::string_view p_key) {
	const std::optional<uint32_t> id = godot_entry(p_key);
	return id ? std::optional<std::string_view>(godot_entry_at(*id).value) : std::nullopt;
}

std::vector<std::string_view> godot_words(std::string_view p_value) {
	std::vector<std::string_view> words;
	for (size_t at = 0; at <= p_value.size();) {
		const size_t space = std::min(p_value.find(' ', at), p_value.size());
		words.push_back(p_value.substr(at, space - at));
		at = space + 1;
	}
	return words;
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
