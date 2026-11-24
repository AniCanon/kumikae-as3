// Resolved foreign declarations are independent of the receiver carrying the script.
#pragma once

#include "../vm/cache_eviction.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kas3 {

class ScriptDeclarations {
public:
	enum class Member { Missing, Field, Method };
	using Names = std::map<std::string, Member, std::less<>>;

	template <class Reflect>
	Member lookup(uint64_t p_script, std::string_view p_name, Reflect p_reflect) {
		auto at = scripts_.find(p_script);
		if (at == scripts_.end()) {
			eviction_.make_room(scripts_);
			Names names;
			p_reflect(names);
			at = scripts_.emplace(p_script, std::move(names)).first;
		}
		const auto member = at->second.find(p_name);
		return member == at->second.end() ? Member::Missing : member->second;
	}

	void clear() {
		if (!scripts_.empty()) {
			scripts_.clear();
		}
	}

private:
	vm::CacheEviction eviction_;
	std::unordered_map<uint64_t, Names> scripts_;
};

} // namespace kas3
