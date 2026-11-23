// Only a program constant can introduce a negative entry. A computed key can reuse it.
#pragma once

#include "engine_handles.h"

#include <set>

namespace kas3 {

class LiteralMisses {
	std::set<std::string, std::less<>> names_;

public:
	template <class Resolve>
	const vm::NamedMember *lookup(std::string_view p_name, vm::NameSource p_source, Resolve p_resolve) {
		if (names_.find(p_name) != names_.end()) {
			return &vm::MISSING_MEMBER;
		}
		const vm::NamedMember *found = p_resolve();
		if (found->what == vm::NamedMember::What::Missing && p_source == vm::NameSource::Literal) {
			names_.emplace(p_name);
		}
		return found;
	}
	void clear() { names_.clear(); }
};

} // namespace kas3
