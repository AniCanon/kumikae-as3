// The interpreter indexes sites by function and instruction. Native code supplies a site of its own.
#pragma once

#include "engine_handles.h"
#include "named_site.h"

#include <map>

namespace kas3::vm {

class NamedSites {
	struct Place {
		uint32_t name = ir::NONE;
		uint32_t site = ir::NONE;
	};
	std::vector<std::vector<Place>> places_;
	std::vector<NamedSite> sites_;
	// Overflow shapes share resolution across sites. Foreign scripts dispatch through Godot and
	// share its class metadata. Only AS3 field and method slots depend on a script class.
	using Members = std::map<std::string, NamedSite::Entry, std::less<>>;
	std::map<std::pair<uintptr_t, uint32_t>, Members> shared_;
	uint64_t generation_;
public:
	explicit NamedSites(const ir::Module &p_module);
	NamedSite *at(uint32_t p_function, uint32_t p_pc, const Str *p_key, const std::vector<Str *> &p_strings);
	std::optional<bool> access(NamedSite &p_site, const ir::Module &p_module, EngineHandles &p_engine, ir::Op p_op, Slot p_holder,
			std::string_view p_name, const Slot *p_arguments, size_t p_count, Slot *r_result, NamedCall *r_call, std::string *r_error, void *p_reuse);
};

} // namespace kas3::vm
