#pragma once

#include "check.h"
#include "builtins.h"
#include "query.h"

#include <algorithm>
#include <string>
#include <vector>

namespace completion_test {

using namespace kas3;

// What completion offers at the `|` in the text, by name.
struct Offered {
	std::vector<Completion> items;
	bool has(std::string_view p_name) const { return find(p_name) != nullptr; }
	const Completion *find(std::string_view p_name) const {
		for (const Completion &item : items) {
			if (item.name == p_name) {
				return &item;
			}
		}
		return nullptr;
	}
	size_t count(std::string_view p_name) const {
		size_t found = 0;
		for (const Completion &item : items) {
			found += item.name == p_name;
		}
		return found;
	}
};

inline Offered complete(std::string p_text, const char *p_other = nullptr) {
	size_t at = p_text.find('|');
	CHECK(at != std::string::npos);
	p_text.erase(at, 1);
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	Unit *unit = compilation.add("Player.as", p_text);
	if (p_other) {
		compilation.add("Other.as", p_other);
	}
	compilation.check(true);
	Queries queries(compilation);
	return Offered{ queries.complete(*unit, (uint32_t)at).items };
}

inline std::string apply_completion(std::string p_text, std::string_view p_name, const char *p_other = nullptr) {
	const size_t caret = p_text.find('|');
	p_text.erase(caret, 1);
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	Unit *unit = compilation.add("Player.as", p_text);
	if (p_other) {
		compilation.add("Other.as", p_other);
	}
	compilation.check(true);
	Queries queries(compilation);
	const auto span = queries.completion_range(*unit, static_cast<uint32_t>(caret));
	const auto offered = queries.complete(*unit, static_cast<uint32_t>(caret)).items;
	for (const auto &item : offered) {
		if (item.name != p_name) {
			continue;
		}
		std::vector<CompletionEdit> edits = { item.edit.value_or(CompletionEdit{ span.begin, span.end, item.name }) };
		if (item.additional_edit) {
			edits.push_back(*item.additional_edit);
		}
		std::sort(edits.begin(), edits.end(), [](const CompletionEdit &a, const CompletionEdit &b) { return a.begin > b.begin; });
		for (const auto &edit : edits) {
			p_text.replace(edit.begin, edit.end - edit.begin, edit.text);
		}
		return p_text;
	}
	CHECK(false);
	return p_text;
}

} // namespace completion_test
