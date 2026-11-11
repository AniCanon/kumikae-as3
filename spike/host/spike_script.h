// A Godot Script resource that stands for one AS3-shaped class.
#pragma once

#include "host/class_registry.h"

#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>

#include <vector>

// The per-class method table the engine's calls are resolved through (rule 10).
struct ClassRuntime {
	struct Slot {
		const void *key; // The interned StringName's data pointer.
		void (*thunk)(as3::Object *, const GDExtensionConstVariantPtr *, int64_t, GDExtensionVariantPtr);
	};
	const as3::ScriptClass *cls = nullptr;
	std::vector<godot::StringName> names; // Keeps the interned names alive.
	std::vector<Slot> slots;
};

class SpikeScript : public godot::ScriptExtension {
	GDCLASS(SpikeScript, godot::ScriptExtension)

	ClassRuntime runtime;

protected:
	static void _bind_methods();

public:
	static godot::Ref<SpikeScript> for_class(const godot::StringName &p_name);
	static void clear_cache();

	const ClassRuntime *get_runtime() const { return &runtime; }

	bool _editor_can_reload_from_file() override { return false; }
	bool _can_instantiate() const override { return runtime.cls != nullptr; }
	godot::Ref<godot::Script> _get_base_script() const override { return {}; }
	godot::StringName _get_global_name() const override;
	bool _inherits_script(const godot::Ref<godot::Script> &) const override { return false; }
	godot::StringName _get_instance_base_type() const override;
	void *_instance_create(godot::Object *p_for_object) const override;
	void *_placeholder_instance_create(godot::Object *p_for_object) const override {
		return _instance_create(p_for_object);
	}
	bool _has_source_code() const override { return false; }
	godot::String _get_source_code() const override { return ""; }
	void _set_source_code(const godot::String &) override {}
	godot::Error _reload(bool) override { return godot::OK; }
	godot::StringName _get_doc_class_name() const override { return godot::StringName(); }
	godot::TypedArray<godot::Dictionary> _get_documentation() const override { return {}; }
	godot::String _get_class_icon_path() const override { return ""; }
	bool _has_method(const godot::StringName &p_method) const override;
	bool _has_static_method(const godot::StringName &) const override { return false; }
	godot::Variant _get_script_method_argument_count(const godot::StringName &) const override { return {}; }
	godot::Dictionary _get_method_info(const godot::StringName &) const override { return {}; }
	bool _is_tool() const override { return false; }
	bool _is_valid() const override { return runtime.cls != nullptr; }
	bool _is_abstract() const override { return false; }
	godot::ScriptLanguage *_get_language() const override;
	bool _has_script_signal(const godot::StringName &p_signal) const override;
	godot::TypedArray<godot::Dictionary> _get_script_signal_list() const override;
	bool _has_property_default_value(const godot::StringName &) const override { return false; }
	godot::Variant _get_property_default_value(const godot::StringName &) const override { return {}; }
	void _update_exports() override {}
	godot::TypedArray<godot::Dictionary> _get_script_method_list() const override { return {}; }
	godot::TypedArray<godot::Dictionary> _get_script_property_list() const override { return {}; }
	int32_t _get_member_line(const godot::StringName &) const override { return -1; }
	godot::Dictionary _get_constants() const override { return {}; }
	godot::TypedArray<godot::StringName> _get_members() const override { return {}; }
	bool _is_placeholder_fallback_enabled() const override { return false; }
	godot::Variant _get_rpc_config() const override { return {}; }
};
