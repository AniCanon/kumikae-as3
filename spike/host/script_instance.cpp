#include "host/script_instance.h"

#include "host/spike_language.h"
#include "runtime/godot_calls.h"

#include <unordered_map>

using namespace godot;

// Owner to AS3 object, for AS3 code that reaches a sibling object's AS3 side (checksums over children).
static std::unordered_map<GDExtensionObjectPtr, as3::Object *> objects_by_owner;

namespace as3 {
Object *script_object_of(GDExtensionObjectPtr p_owner) {
	auto found = objects_by_owner.find(p_owner);
	return found == objects_by_owner.end() ? nullptr : found->second;
}
} // namespace as3

static inline const void *name_key(GDExtensionConstStringNamePtr p_name) {
	return *reinterpret_cast<const void *const *>(p_name);
}

static GDExtensionBool instance_set(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr,
		GDExtensionConstVariantPtr) {
	return false;
}

static GDExtensionBool instance_get(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr,
		GDExtensionVariantPtr) {
	return false;
}

static const GDExtensionPropertyInfo *instance_get_property_list(GDExtensionScriptInstanceDataPtr, uint32_t *r_count) {
	*r_count = 0;
	return nullptr;
}

static void instance_free_property_list(GDExtensionScriptInstanceDataPtr, const GDExtensionPropertyInfo *, uint32_t) {
}

static const GDExtensionMethodInfo *instance_get_method_list(GDExtensionScriptInstanceDataPtr, uint32_t *r_count) {
	*r_count = 0;
	return nullptr;
}

static void instance_free_method_list(GDExtensionScriptInstanceDataPtr, const GDExtensionMethodInfo *, uint32_t) {
}

static GDExtensionObjectPtr instance_get_owner(GDExtensionScriptInstanceDataPtr p_data) {
	return static_cast<ScriptInstance *>(p_data)->owner;
}

static GDExtensionBool instance_has_method(GDExtensionScriptInstanceDataPtr p_data,
		GDExtensionConstStringNamePtr p_name) {
	const ClassRuntime *runtime = static_cast<ScriptInstance *>(p_data)->runtime;
	const void *key = name_key(p_name);
	for (const ClassRuntime::Slot &slot : runtime->slots) {
		if (slot.key == key) {
			return true;
		}
	}
	return false;
}

// The engine calling into AS3: resolve the interned name, unbox, call the typed method (rule 10).
static void instance_call(GDExtensionScriptInstanceDataPtr p_data, GDExtensionConstStringNamePtr p_method,
		const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argc, GDExtensionVariantPtr r_return,
		GDExtensionCallError *r_error) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	const void *key = name_key(p_method);
	for (const ClassRuntime::Slot &slot : instance->runtime->slots) {
		if (slot.key == key) {
			slot.thunk(instance->object, p_args, p_argc, r_return);
			if (AS3_PENDING()) {
				as3::report_uncaught();
			}
			r_error->error = GDEXTENSION_CALL_OK;
			return;
		}
	}
	r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
}

static void instance_notification(GDExtensionScriptInstanceDataPtr, int32_t, GDExtensionBool) {
}

static void instance_to_string(GDExtensionScriptInstanceDataPtr, GDExtensionBool *r_is_valid, GDExtensionStringPtr) {
	*r_is_valid = false;
}

static GDExtensionObjectPtr instance_get_script(GDExtensionScriptInstanceDataPtr p_data) {
	return static_cast<ScriptInstance *>(p_data)->script;
}

static GDExtensionBool instance_is_placeholder(GDExtensionScriptInstanceDataPtr) {
	return false;
}

static GDExtensionScriptLanguagePtr instance_get_language(GDExtensionScriptInstanceDataPtr) {
	return SpikeLanguage::singleton->_owner;
}

static void instance_free(GDExtensionScriptInstanceDataPtr p_data) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	objects_by_owner.erase(instance->owner);
	as3::invalidate_closures(instance->owner);
	as3::release(instance->object);
	delete instance;
}

GDExtensionScriptInstancePtr create_script_instance(const SpikeScript *p_script, GDExtensionObjectPtr p_owner) {
	static GDExtensionScriptInstanceInfo3 info = {};
	static bool ready = false;
	if (!ready) {
		info.set_func = instance_set;
		info.get_func = instance_get;
		info.get_property_list_func = instance_get_property_list;
		info.free_property_list_func = instance_free_property_list;
		info.get_owner_func = instance_get_owner;
		info.get_method_list_func = instance_get_method_list;
		info.free_method_list_func = instance_free_method_list;
		info.has_method_func = instance_has_method;
		info.call_func = instance_call;
		info.notification_func = instance_notification;
		info.to_string_func = instance_to_string;
		info.get_script_func = instance_get_script;
		info.is_placeholder_func = instance_is_placeholder;
		info.get_language_func = instance_get_language;
		info.free_func = instance_free;
		ready = true;
	}
	const ClassRuntime *runtime = p_script->get_runtime();
	ScriptInstance *instance = new ScriptInstance();
	instance->runtime = runtime;
	instance->owner = p_owner;
	instance->script = p_script->_owner;
	instance->object = runtime->cls->create(p_owner);
	as3::retain(instance->object);
	objects_by_owner[p_owner] = instance->object;
	return gdextension_interface::script_instance_create3(&info, instance);
}
