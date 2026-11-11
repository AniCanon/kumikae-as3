#include "host/spike_script.h"

#include "host/script_instance.h"
#include "host/spike_language.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <unordered_map>

using namespace godot;

static std::unordered_map<const as3::ScriptClass *, Ref<SpikeScript>> script_cache;

void SpikeScript::_bind_methods() {
	ClassDB::bind_static_method("SpikeScript", D_METHOD("for_class", "name"), &SpikeScript::for_class);
}

Ref<SpikeScript> SpikeScript::for_class(const StringName &p_name) {
	const as3::ScriptClass *cls = as3::find_class(String(p_name).utf8().get_data());
	if (!cls) {
		UtilityFunctions::printerr("SpikeScript: unknown class: ", p_name);
		return {};
	}
	auto found = script_cache.find(cls);
	if (found != script_cache.end()) {
		return found->second;
	}
	Ref<SpikeScript> script;
	script.instantiate();
	script->runtime.cls = cls;
	for (int i = 0; i < cls->method_count; i++) {
		StringName name(cls->methods[i].name);
		ClassRuntime::Slot slot;
		slot.key = *reinterpret_cast<const void *const *>(name._native_ptr());
		slot.thunk = cls->methods[i].thunk;
		script->runtime.names.push_back(name);
		script->runtime.slots.push_back(slot);
	}
	script_cache[cls] = script;
	return script;
}

void SpikeScript::clear_cache() {
	script_cache.clear();
}

StringName SpikeScript::_get_global_name() const {
	return runtime.cls ? StringName(runtime.cls->name) : StringName();
}

StringName SpikeScript::_get_instance_base_type() const {
	return runtime.cls ? StringName(runtime.cls->godot_base) : StringName("Object");
}

void *SpikeScript::_instance_create(Object *p_for_object) const {
	if (!runtime.cls) {
		return nullptr;
	}
	return create_script_instance(this, p_for_object->_owner);
}

bool SpikeScript::_has_method(const StringName &p_method) const {
	const void *key = *reinterpret_cast<const void *const *>(p_method._native_ptr());
	for (const ClassRuntime::Slot &slot : runtime.slots) {
		if (slot.key == key) {
			return true;
		}
	}
	return false;
}

ScriptLanguage *SpikeScript::_get_language() const {
	return SpikeLanguage::singleton;
}

bool SpikeScript::_has_script_signal(const StringName &p_signal) const {
	if (!runtime.cls) {
		return false;
	}
	for (int i = 0; i < runtime.cls->signal_count; i++) {
		if (p_signal == StringName(runtime.cls->signals[i])) {
			return true;
		}
	}
	return false;
}

TypedArray<Dictionary> SpikeScript::_get_script_signal_list() const {
	TypedArray<Dictionary> list;
	if (runtime.cls) {
		for (int i = 0; i < runtime.cls->signal_count; i++) {
			Dictionary signal;
			signal["name"] = String(runtime.cls->signals[i]);
			signal["args"] = Array();
			list.push_back(signal);
		}
	}
	return list;
}
