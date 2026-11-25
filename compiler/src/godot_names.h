// Godot's name table (compiler/godot/godot_names.txt, written by the generator): one entry for each class
// and member godot.as declares, and for each operator, global function and default AS3 can't write. An
// entry's id is its place in the table: fixed for one build of the table, so that what the IR names by it
// is found again without text.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace kas3 {

struct GodotEntry {
	std::string_view key; // "Node2D.position", "Vector2", "@operator.OP_ADD", "@global.lerpf"
	std::string_view value; // Godot's name first, then what the generator says of it: see godot/generate.py.
};

// The words of a name-table value, split at its spaces: "position property Vector2 get_position 3341600327 ...".
inline std::vector<std::string_view> godot_words(std::string_view p_value) {
	std::vector<std::string_view> words;
	for (size_t at = 0; at <= p_value.size();) {
		const size_t space = std::min(p_value.find(' ', at), p_value.size());
		words.push_back(p_value.substr(at, space - at));
		at = space + 1;
	}
	return words;
}

// The entry of this key, by its id.
std::optional<uint32_t> godot_entry(std::string_view p_key);
// The entry of an id that godot_entry() gave.
GodotEntry godot_entry_at(uint32_t p_id);
// The entry of the operator Godot's description names so ("+", "unary-", "=="): an @operator entry, whose
// value starts with that name.
std::optional<uint32_t> godot_operator(std::string_view p_name);
// The whole value of the entry of this key: a global function's signature, a default's text.
std::optional<std::string_view> godot_value(std::string_view p_key);

} // namespace kas3
