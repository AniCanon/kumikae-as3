// A GDScript reload can replace declarations in place without emitting Resource.changed.
// Retain reflected declarations until a handoff to Godot could run user code or
// release an object. An ordinary scalar field read can reuse both the schema and the binding.
#include "godot_host.h"
#include "program.h"

#include <godot_cpp/classes/script.hpp>

using namespace godot;

namespace kas3 {

// Only names declared by the attached script supplement the engine's AS3 declaration table.
const vm::NamedMember &GodotHost::script_member(void *p_receiver, std::string_view p_name) {
	const Variant &value = *static_cast<const Variant *>(p_receiver);
	Object *object = value.get_type() == Variant::OBJECT ? value.get_validated_object() : nullptr;
	if (!object || script_object_by_id(object->get_instance_id())) {
		return vm::MISSING_MEMBER;
	}
	Ref<Script> script = object->get_script();
	if (script.is_null()) {
		return vm::MISSING_MEMBER;
	}
	if (!declarations_current.load(std::memory_order_relaxed)) {
		declarations_current.store(true, std::memory_order_relaxed);
		declarations_.clear();
		read_owner_ = nullptr;
		read_binding_ = nullptr;
		read_name_ = {};
	}
	const auto kind = declarations_.lookup(script->get_instance_id(), p_name, [&](ScriptDeclarations::Names &r_names) {
		static const StringName name("name"), arguments("args"), type("type");
		bool ordinary = script->get_class() == StringName("GDScript");
		for (auto klass = script; klass.is_valid(); klass = klass->get_base_script()) {
			const auto add = [&](const TypedArray<Dictionary> &p_list, ScriptDeclarations::Member p_kind) {
				for (int i = 0; i < p_list.size(); ++i) {
					const Dictionary item = p_list[i];
					if (p_kind == ScriptDeclarations::Member::Field &&
							(int(item["usage"]) & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP))) {
						continue;
					}
					const CharString text = String(item[name]).utf8();
					if (p_kind == ScriptDeclarations::Member::Method && Array(item[arguments]).is_empty()) {
						ordinary = false; // Both named and inline GDScript getters take no arguments.
					}
					const int field_type = p_kind == ScriptDeclarations::Member::Field ? int(item[type]) : Variant::NIL;
					const bool scalar = field_type == Variant::BOOL || field_type == Variant::INT || field_type == Variant::FLOAT;
					r_names.try_emplace(std::string(text.get_data(), text.length()), scalar ? ScriptDeclarations::Member::Scalar : p_kind);
				}
			};
			add(klass->get_script_method_list(), ScriptDeclarations::Member::Method);
			add(klass->get_script_property_list(), ScriptDeclarations::Member::Field);
		}
		if (!ordinary) {
			for (auto &entry : r_names) {
				if (entry.second == ScriptDeclarations::Member::Scalar) {
					entry.second = ScriptDeclarations::Member::Field;
				}
			}
		}
	});
	const auto &member = kind == ScriptDeclarations::Member::Missing ? vm::MISSING_MEMBER : script_binding(p_name, kind == ScriptDeclarations::Member::Method);
	if (kind == ScriptDeclarations::Member::Scalar) {
		read_owner_ = object->_owner;
		read_binding_ = member.invoke;
		read_name_ = member.godot;
	}
	return member;
}

} // namespace kas3
