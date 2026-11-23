// The interpreter indexes sites by function and instruction. Native code supplies a site of its own.
#pragma once

#include "engine_handles.h"
#include "named_site.h"

namespace kas3::vm {

class NamedSites {
	struct Place {
		uint32_t name = ir::NONE;
		uint32_t site = ir::NONE;
	};
	std::vector<std::vector<Place>> places_;
	std::vector<NamedSite> sites_;
	uint64_t generation_;
public:
	explicit NamedSites(const ir::Module &p_module);
	NamedSite *at(uint32_t p_function, uint32_t p_pc, const Str *p_key, const std::vector<Str *> &p_strings);
	std::optional<bool> access(NamedSite &p_site, const ir::Module &p_module, EngineHandles &p_engine, ir::Op p_op, Slot p_holder,
			std::string_view p_name, const Slot *p_arguments, size_t p_count, Slot *r_result, std::string *r_error);
};

} // namespace kas3::vm
