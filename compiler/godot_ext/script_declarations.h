// Resolved foreign declarations are independent of the receiver carrying the script.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kas3 {

class ScriptDeclarations {
public:
	enum class Member { Missing, Field, Scalar, Method };
	using Names = std::map<std::string, Member, std::less<>>;

	template <class Reflect>
	Member lookup(uint64_t p_script, std::string_view p_name, Reflect p_reflect) {
		auto at = scripts_.find(p_script);
		if (at == scripts_.end()) {
			if (scripts_.size() == 64) {
				scripts_.clear();
			}
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
	std::unordered_map<uint64_t, Names> scripts_;
};

} // namespace kas3
