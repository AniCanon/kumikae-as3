#include "builtins.h"

#include <unordered_map>

namespace kas3 {

bool godot_name(std::string_view p_class, std::string_view p_member, std::string *r_name) {
	// Built on first use: "Class.member" or "Class" to Godot's name.
	static const std::unordered_map<std::string, std::string> names = []() {
		std::unordered_map<std::string, std::string> table;
		const std::string text = godot_names_text();
		size_t at = 0;
		while (at < text.size()) {
			size_t end = text.find('\n', at);
			if (end == std::string::npos) {
				end = text.size();
			}
			size_t equals = text.find('=', at);
			if (equals != std::string::npos && equals < end) {
				table.emplace(text.substr(at, equals - at), text.substr(equals + 1, end - equals - 1));
			}
			at = end + 1;
		}
		return table;
	}();
	std::string key(p_class);
	if (!p_member.empty()) {
		key += '.';
		key += p_member;
	}
	auto found = names.find(key);
	if (found == names.end()) {
		return false;
	}
	*r_name = found->second;
	return true;
}

} // namespace kas3
