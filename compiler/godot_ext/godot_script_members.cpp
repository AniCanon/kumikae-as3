// Reflection answers declaration queries and classifies foreign reads that Godot declined.
#include "godot_host.h"
#include "godot_calls.h"
#include "program.h"

#include <godot_cpp/classes/script.hpp>

using namespace godot;

namespace kas3 {

// Membership asks for a declaration even if a getter could supply a value. A foreign
// callback can reload its own script in place during the surrounding AS3 invocation.
bool GodotHost::script_has(void *p_receiver, std::string_view p_name) {
	const Variant &value = *static_cast<const Variant *>(p_receiver);
	Object *object = value.get_type() == Variant::OBJECT ? value.get_validated_object() : nullptr;
	if (!object || script_object_by_id(object->get_instance_id())) {
		return false;
	}
	const StringName key(to_godot(p_name));
	for (Ref<Script> script = object->get_script(); script.is_valid(); script = script->get_base_script()) {
		if (script->has_script_method(key)) {
			return true;
		}
		const auto fields = script->get_script_property_list();
		for (int i = 0; i < fields.size(); ++i) {
			const Dictionary field = fields[i];
			if (StringName(field["name"]) == key && !(int(field["usage"]) & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP))) {
				return true;
			}
		}
	}
	return false;
}

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
	if (reflected_epoch_ != declaration_epoch_) {
		reflected_epoch_ = declaration_epoch_;
		declarations_.clear();
	}
	const auto reflect = [&](ScriptDeclarations::Names &r_names) {
		static const StringName name("name");
		for (auto klass = script; klass.is_valid(); klass = klass->get_base_script()) {
			const auto add = [&](const TypedArray<Dictionary> &p_list, ScriptDeclarations::Member p_kind) {
				for (int i = 0; i < p_list.size(); ++i) {
					const Dictionary item = p_list[i];
					if (p_kind == ScriptDeclarations::Member::Field &&
							(int(item["usage"]) & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP))) {
						continue;
					}
					const CharString text = String(item[name]).utf8();
					r_names.try_emplace(std::string(text.get_data(), text.length()), p_kind);
				}
			};
			add(klass->get_script_method_list(), ScriptDeclarations::Member::Method);
			add(klass->get_script_property_list(), ScriptDeclarations::Member::Field);
		}
	};
	const auto kind = declarations_.lookup(script->get_instance_id(), p_name, reflect);
	return kind == ScriptDeclarations::Member::Missing ? vm::MISSING_MEMBER : script_binding(p_name, kind == ScriptDeclarations::Member::Method);
}

} // namespace kas3
