// A Godot Script resource for one .as file, and the loader that makes one when Godot loads the file.
#pragma once

#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>

#include <cstdint>
#include <string>

class AS3Script : public godot::ScriptExtension {
	GDCLASS(AS3Script, godot::ScriptExtension)

	std::string file_; // The file's path on disk.
	godot::String source_;
	uint32_t class_ = 0xFFFFFFFFu; // The class the file declares, in the program's IR.

protected:
	static void _bind_methods() {}

public:
	AS3Script();
	~AS3Script() override;
	void set_file(const godot::String &p_resource_path);
	// After the scripts were loaded afresh: the file's class is looked up again in the new program.
	static void refresh_all();
	uint32_t get_class_index() const { return class_; }

	bool _editor_can_reload_from_file() override { return false; }
	bool _can_instantiate() const override { return class_ != 0xFFFFFFFFu; }
	godot::Ref<godot::Script> _get_base_script() const override { return {}; }
	godot::StringName _get_global_name() const override;
	bool _inherits_script(const godot::Ref<godot::Script> &) const override { return false; }
	godot::StringName _get_instance_base_type() const override;
	void *_instance_create(godot::Object *p_for_object) const override;
	void *_placeholder_instance_create(godot::Object *p_for_object) const override { return _instance_create(p_for_object); }
	bool _has_source_code() const override { return true; }
	godot::String _get_source_code() const override { return source_; }
	void _set_source_code(const godot::String &p_code) override { source_ = p_code; }
	godot::Error _reload(bool) override { return godot::OK; }
	godot::StringName _get_doc_class_name() const override { return godot::StringName(); }
	godot::TypedArray<godot::Dictionary> _get_documentation() const override { return {}; }
	godot::String _get_class_icon_path() const override { return ""; }
	bool _has_method(const godot::StringName &p_method) const override;
	bool _has_static_method(const godot::StringName &) const override { return false; }
	godot::Variant _get_script_method_argument_count(const godot::StringName &) const override { return {}; }
	godot::Dictionary _get_method_info(const godot::StringName &) const override { return {}; }
	bool _is_tool() const override { return false; }
	bool _is_valid() const override { return class_ != 0xFFFFFFFFu; }
	bool _is_abstract() const override { return false; }
	godot::ScriptLanguage *_get_language() const override;
	bool _has_script_signal(const godot::StringName &) const override { return false; }
	godot::TypedArray<godot::Dictionary> _get_script_signal_list() const override { return {}; }
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

class AS3ScriptLoader : public godot::ResourceFormatLoader {
	GDCLASS(AS3ScriptLoader, godot::ResourceFormatLoader)

protected:
	static void _bind_methods() {}

public:
	godot::PackedStringArray _get_recognized_extensions() const override;
	bool _handles_type(const godot::StringName &p_type) const override;
	godot::String _get_resource_type(const godot::String &p_path) const override;
	godot::Variant _load(const godot::String &p_path, const godot::String &p_original_path, bool p_use_sub_threads,
			int32_t p_cache_mode) const override;
};

// The engine's handle for a new instance of the script on that object, or null.
void *create_script_instance(const AS3Script *p_script, godot::Object *p_owner);
// How the engine's name for a method maps to the class's function: Godot's name for a virtual the class
// answers, or the AS3 name of any of its methods. NONE if there is none.
uint32_t script_method(uint32_t p_class, const godot::StringName &p_name);
