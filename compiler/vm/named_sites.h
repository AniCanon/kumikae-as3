// The interpreter indexes sites by function and instruction. Native code supplies a site of its own.
#pragma once

#include "adjacent_uses.h"
#include "engine_handles.h"
#include "named_site.h"

#include <map>
#include <unordered_map>

namespace kas3::vm {

struct InterpreterSite : NamedSite {
	std::optional<ir::MathConsumer> math;
};

// A literal name is interned once on overflow. Subsequent dispatch uses just the class identities.
struct NamedOverflow {
	struct Shape {
		uintptr_t type;
		std::optional<uint32_t> klass;
		uint64_t script;
		bool operator==(const Shape &p_other) const { return type == p_other.type && klass == p_other.klass && script == p_other.script; }
	};
	struct Hash {
		size_t operator()(const Shape &p_shape) const { return std::hash<uintptr_t>{}(p_shape.type) ^ (std::hash<std::optional<uint32_t>>{}(p_shape.klass) << 1) ^ std::hash<uint64_t>{}(p_shape.script); }
	};
	std::unordered_map<Shape, NamedSite::Entry, Hash> entries;
};

class NamedSites {
	struct Place {
		uint32_t name = ir::NONE;
		uint32_t site = ir::NONE;
	};
	std::vector<std::vector<Place>> places_;
	std::vector<InterpreterSite> sites_;
	std::vector<std::vector<uint32_t>> vector_targets_;
	std::vector<std::vector<bool>> borrowed_sets_;
	// Overflow shapes share resolution across sites. Foreign scripts dispatch through Godot and
	// share its class metadata. Only AS3 field and method slots depend on a script class.
	std::map<std::string, NamedOverflow, std::less<>> shared_;
	uint64_t generation_;
	const NamedSite::Entry &binding(NamedSite &p_site, EngineHandles &p_engine, const ir::Module &p_module, const NamedShape &shape, void *handle,
		std::string_view p_name, const NamedSite::Entry *delegated);
public:
	explicit NamedSites(const ir::Module &p_module);
	uint32_t vector_target(uint32_t p_function, uint32_t p_pc) const {
		const auto &targets = vector_targets_[p_function];
		return p_pc < targets.size() ? targets[p_pc] : ir::NONE;
	}
	bool borrows_set(uint32_t p_function, uint32_t p_pc) const {
		const auto &sets = borrowed_sets_[p_function];
		return p_pc < sets.size() && sets[p_pc];
	}
	InterpreterSite *at(uint32_t p_function, uint32_t p_pc, const Str *p_key, const std::vector<Str *> &p_strings);
	std::optional<bool> access(NamedSite &p_site, const ir::Module &p_module, EngineHandles &p_engine, ir::Op p_op, Slot p_holder,
			std::string_view p_name, const Slot *p_arguments, size_t p_count, Slot *r_result, std::optional<NamedCall> *r_call, std::string *r_error, const Wanted &p_wanted);
};

} // namespace kas3::vm
