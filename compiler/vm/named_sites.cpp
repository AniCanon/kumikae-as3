#include "named_sites.h"

#include "adjacent_uses.h"
#include "any.h"
#include "fields.h"

#include <atomic>

namespace kas3::vm {

using ir::Kind;
using ir::NONE;
using ir::Op;

static std::atomic<uint64_t> next_generation{ 1 };

NamedSites::NamedSites(const ir::Module &p_module) : places_(p_module.functions.size()), vector_targets_(p_module.functions.size()), borrowed_sets_(p_module.functions.size()), generation_(next_generation.fetch_add(1, std::memory_order_relaxed)) {
	for (size_t f = 0; f < p_module.functions.size(); f++) {
		const auto &fn = p_module.functions[f];
		std::vector<uint32_t> names;
		std::optional<ir::AdjacentUses> uses;
		for (size_t pc = 0; pc < fn.code.size(); pc++) {
			const auto &in = fn.code[pc];
			if (pc + 1 < fn.code.size() && (in.op == Op::VecGet || in.op == Op::ToAny)) {
				const auto &next = fn.code[pc + 1];
				const bool moves = next.op == Op::Move && in.op == Op::VecGet && in.a == next.b;
				const bool sets = next.op == Op::DynSet && in.op == Op::ToAny && in.a == next.c && fn.registers[in.b] == Kind::Val &&
						 in.b < fn.engine_types.size() && ir::bare_named(fn.engine_types[in.b]) != ir::Bare::None;
				if (moves || sets) {
					if (!uses) {
						uses.emplace(fn);
					}
					if (uses->only_consumer(in.a, pc + 1)) {
						if (moves) {
							vector_targets_[f].resize(pc + 1, NONE);
							vector_targets_[f][pc] = next.a;
						} else {
							borrowed_sets_[f].resize(pc + 1);
							borrowed_sets_[f][pc] = true;
						}
					}
				}
			}
			if (in.op == Op::ConstStr && in.a < fn.registers.size() && in.b < p_module.strings.size()) {
				if (names.empty()) {
					names.resize(fn.registers.size(), NONE);
				}
				names[in.a] = in.b;
			} else if (!names.empty() && (in.op == Op::DynGet || in.op == Op::DynSet || in.op == Op::DynCall)) {
				if (in.op == Op::DynCall && (in.c >= fn.argument_lists.size() || fn.argument_lists[in.c].empty())) {
					continue;
				}
				const uint32_t key = in.op == Op::DynGet ? in.c : in.op == Op::DynSet ? in.b : fn.argument_lists[in.c][0];
				if (key < names.size() && fn.registers[key] == Kind::Str && names[key] != NONE) {
					places_[f].resize(pc + 1);
					places_[f][pc] = { names[key], static_cast<uint32_t>(sites_.size()) };
					auto &site = sites_.emplace_back();
					if (in.op == Op::DynGet) {
						if (!uses) {
							uses.emplace(fn);
						}
						site.math = ir::math_consumer(fn, pc, *uses);
					}
				}
			}
		}
	}
}

InterpreterSite *NamedSites::at(uint32_t p_function, uint32_t p_pc, const Str *p_key, const std::vector<Str *> &p_strings) {
	const auto &places = places_[p_function];
	if (p_pc >= places.size()) {
		return nullptr;
	}
	const Place &place = places[p_pc];
	// Even a jump or a later write to the key register must present the exact module string.
	return place.site != NONE && p_key == p_strings[place.name] ? &sites_[place.site] : nullptr;
}

static NamedSite::Entry resolve(EngineHandles &p_engine, const ir::Module &p_module, const NamedShape &p_shape, void *p_handle, std::string_view p_name) {
	NamedSite::Entry entry{};
	entry.type = p_shape.type;
	entry.script = p_shape.script;
	if (p_shape.instance) {
		entry.klass = p_shape.instance->klass;
		const auto &klass = p_module.classes[*entry.klass];
		if (auto index = klass.field(p_name, true)) {
			entry.field = *index;
			return entry;
		}
		for (const auto &[name, slot] : klass.named_methods) {
			const std::string_view full = name;
			if (full == p_name) {
				entry.method = klass.methods[slot];
				return entry;
			}
			if (full.size() == p_name.size() + 4 && full.substr(4) == p_name &&
					(full.substr(0, 4) == "get " || full.substr(0, 4) == "set ")) {
				return entry; // A method or accessor needs the full AS3 call convention.
			}
		}
	}
	entry.engine = &p_engine.named(p_handle, p_name, NameSource::Literal);
	if (entry.engine->what == NamedMember::What::Missing && p_shape.script && !p_shape.instance) {
		entry.engine = &p_engine.script_member(p_handle, p_name);
	}
	return entry;
}

static bool scalar_field(const ir::Class &p_class, Object *p_object, uint32_t p_index, Op p_op, const Slot *p_given, Slot *r_result) {
	const Kind kind = p_class.fields[p_index].kind;
	Slot &value = field(p_class, p_object, p_index);
	if (p_op == Op::DynGet && (kind == Kind::I32 || kind == Kind::Bool)) {
		*r_result = kind == Kind::I32 ? any::integer(value.i) : any::boolean(value.b);
		return true;
	}
	if (p_op == Op::DynSet && kind == Kind::I32 && any::tag(*p_given) == AnyTag::Int) {
		value.i = any::to_int_payload(*p_given);
		return true;
	}
	if (p_op == Op::DynSet && kind == Kind::Bool && any::tag(*p_given) == AnyTag::Boolean) {
		value.b = any::to_bool_payload(*p_given);
		return true;
	}
	return false;
}

const NamedSite::Entry &NamedSites::binding(NamedSite &p_site, EngineHandles &p_engine, const ir::Module &p_module, const NamedShape &p_shape, void *p_handle,
		std::string_view p_name, const NamedSite::Entry *p_delegated) {
	const std::optional<uint32_t> klass = p_shape.instance ? std::optional<uint32_t>(p_shape.instance->klass) : std::nullopt;
	const NamedSite::Entry *entry = p_shape.delegate ? p_delegated : nullptr;
	for (size_t i = 0; !entry && i < p_site.used; i++) {
		const auto &candidate = p_site.entries[i];
		if (p_shape.type == candidate.type && p_shape.script == candidate.script && candidate.klass == klass) {
			entry = &candidate;
			break;
		}
	}
	if (!entry && p_site.used < p_site.entries.size()) {
		auto &next = p_site.entries[p_site.used];
		next = resolve(p_engine, p_module, p_shape, p_handle, p_name);
		if (p_shape.script && !p_shape.instance && next.engine && !next.engine->godot.empty() &&
				(!next.engine->script_only || p_engine.delegates_script(p_handle, p_name))) {
			p_site.delegated = p_site.used;
		}
		entry = &next;
		p_site.used++;
	} else if (!entry) {
		if (!p_site.overflow) {
			auto found = shared_.find(p_name);
			if (found == shared_.end()) {
				found = shared_.try_emplace(std::string(p_name)).first;
			}
			p_site.overflow = &found->second;
		}
		auto &entries = p_site.overflow->entries;
		const NamedOverflow::Shape key{ p_shape.type, klass, p_shape.script };
		auto at = entries.find(key);
		if (at == entries.end()) {
			at = entries.emplace(key, resolve(p_engine, p_module, p_shape, p_handle, p_name)).first;
		}
		entry = &at->second;
	}
	return *entry;
}

std::optional<bool> NamedSites::access(NamedSite &p_site, const ir::Module &p_module, EngineHandles &p_engine, Op p_op, Slot p_holder, std::string_view p_name,
		const Slot *p_arguments, size_t p_count, Slot *r_result, std::optional<NamedCall> *r_call, std::string *r_error, const Wanted &wanted) {
	if (!any::is_engine(p_holder) || p_count > 4) {
		return std::nullopt;
	}
	void *handle = any::pointer(p_holder);
	if (p_site.generation != generation_) {
		p_site.used = 0;
		p_site.overflow = nullptr;
		p_site.delegated.reset();
		p_site.generation = generation_;
	}
	const auto *delegated = p_site.delegated ? &p_site.entries[*p_site.delegated] : nullptr;
	const auto checked = p_engine.named_shape(handle, delegated ? delegated->type : 0);
	if (!checked) {
		return std::nullopt;
	}
	const NamedShape &shape = *checked;
	const auto &entry = binding(p_site, p_engine, p_module, shape, handle, p_name, delegated);
	if (entry.field) {
		return scalar_field(p_module.classes[shape.instance->klass], shape.instance, *entry.field, p_op, p_arguments, r_result) ?
				std::optional<bool>(true) : std::nullopt;
	}
	if (entry.method && p_op == Op::DynCall) {
		*r_call = NamedCall{ shape.instance, *entry.method };
		return std::nullopt;
	}
	const NamedMember *member = entry.engine;
	const bool call = p_op == Op::DynCall, write = p_op == Op::DynSet;
	const std::string_view name = member ? member->godot : std::string_view{};
	if (!member || member->what != (call ? NamedMember::What::Method : NamedMember::What::Property) || name.empty() ||
			name.front() == '@' || (!call && !(write ? member->written : member->read)) || (write && any::tag(p_holder) == AnyTag::Value)) {
		return std::nullopt;
	}
	Argument operands[5];
	operands[0] = { any::tag(p_holder) == AnyTag::Value ? Kind::Val : Kind::Ref, Slot{} };
	operands[0].slot.p = handle;
	for (size_t i = 0; i < p_count; i++) {
		if (any::tag(p_arguments[i]) == AnyTag::Object) {
			return std::nullopt; // Script objects and collections need the VM's checks before conversion.
		}
		operands[i + 1] = { Kind::Any, p_arguments[i] };
	}
	const Prepared *bind = write ? member->setter : member->invoke;
	if (member->script_only) {
		if (!bind || write) {
			return std::nullopt;
		}
		return p_engine.run_script(shape, *bind, operands, p_count + 1, wanted, r_result, r_error);
	}
	if (bind) {
		if (auto done = p_engine.run_named(shape, *bind, operands, p_count + 1, wanted, r_result, r_error)) {
			return done;
		}
	}
	if (write) {
		return p_engine.set(handle, name, operands[1], r_error);
	}
	return call ? p_engine.call(handle, name, operands + 1, p_count, wanted, r_result, r_error)
				: p_engine.get(handle, name, wanted, r_result, r_error);
}

} // namespace kas3::vm
