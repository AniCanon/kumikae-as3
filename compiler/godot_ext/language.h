// ActionScript 3 as a Godot scripting language. For now only what Godot needs to attach scripts and call
// their methods: the editor features (templates, completion, the debugger) answer "nothing".
#pragma once

#include "callable.h"
#include "program.h"

#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/classes/script_language_extension_profiling_info.hpp>

class AS3Language : public godot::ScriptLanguageExtension {
	GDCLASS(AS3Language, godot::ScriptLanguageExtension)

protected:
	static void _bind_methods() {}

public:
	static AS3Language *singleton;

	godot::String _get_name() const override { return "ActionScript"; }
	void _init() override {}
	godot::String _get_type() const override { return "AS3Script"; }
	godot::String _get_extension() const override { return "as"; }
	void _finish() override {}
	godot::PackedStringArray _get_reserved_words() const override { return {}; }
	bool _is_control_flow_keyword(const godot::String &) const override { return false; }
	godot::PackedStringArray _get_comment_delimiters() const override { return {}; }
	godot::PackedStringArray _get_doc_comment_delimiters() const override { return {}; }
	godot::PackedStringArray _get_string_delimiters() const override { return {}; }
	godot::Ref<godot::Script> _make_template(const godot::String &, const godot::String &,
			const godot::String &) const override {
		return {};
	}
	godot::TypedArray<godot::Dictionary> _get_built_in_templates(const godot::StringName &) const override {
		return {};
	}
	bool _is_using_templates() override { return false; }
	godot::Dictionary _validate(const godot::String &, const godot::String &, bool, bool, bool, bool) const override {
		return {};
	}
	godot::String _validate_path(const godot::String &) const override { return ""; }
	godot::Object *_create_script() const override { return nullptr; }
	bool _has_named_classes() const override { return false; }
	bool _supports_builtin_mode() const override { return false; }
	bool _supports_documentation() const override { return false; }
	bool _can_inherit_from_file() const override { return false; }
	int32_t _find_function(const godot::String &, const godot::String &) const override { return -1; }
	godot::String _make_function(const godot::String &, const godot::String &,
			const godot::PackedStringArray &) const override {
		return "";
	}
	bool _can_make_function() const override { return false; }
	godot::Error _open_in_external_editor(const godot::Ref<godot::Script> &, int32_t, int32_t) override {
		return godot::OK;
	}
	bool _overrides_external_editor() override { return false; }
	godot::Dictionary _complete_code(const godot::String &, const godot::String &, godot::Object *) const override {
		return {};
	}
	godot::Dictionary _lookup_code(const godot::String &, const godot::String &, const godot::String &,
			godot::Object *) const override {
		return {};
	}
	godot::String _auto_indent_code(const godot::String &p_code, int32_t, int32_t) const override { return p_code; }
	void _add_global_constant(const godot::StringName &, const godot::Variant &) override {}
	void _add_named_global_constant(const godot::StringName &, const godot::Variant &) override {}
	void _remove_named_global_constant(const godot::StringName &) override {}
	void _thread_enter() override {}
	void _thread_exit() override {}
	godot::String _debug_get_error() const override { return ""; }
	int32_t _debug_get_stack_level_count() const override { return 0; }
	int32_t _debug_get_stack_level_line(int32_t) const override { return 0; }
	godot::String _debug_get_stack_level_function(int32_t) const override { return ""; }
	godot::String _debug_get_stack_level_source(int32_t) const override { return ""; }
	godot::Dictionary _debug_get_stack_level_locals(int32_t, int32_t, int32_t) override { return {}; }
	godot::Dictionary _debug_get_stack_level_members(int32_t, int32_t, int32_t) override { return {}; }
	void *_debug_get_stack_level_instance(int32_t) override { return nullptr; }
	godot::Dictionary _debug_get_globals(int32_t, int32_t) override { return {}; }
	godot::String _debug_parse_stack_level_expression(int32_t, const godot::String &, int32_t, int32_t) override {
		return "";
	}
	godot::TypedArray<godot::Dictionary> _debug_get_current_stack_info() override { return {}; }
	void _reload_all_scripts() override {}
	void _reload_scripts(const godot::Array &, bool) override {}
	void _reload_tool_script(const godot::Ref<godot::Script> &, bool) override {}
	godot::PackedStringArray _get_recognized_extensions() const override {
		godot::PackedStringArray extensions;
		extensions.push_back("as");
		return extensions;
	}
	godot::TypedArray<godot::Dictionary> _get_public_functions() const override { return {}; }
	godot::Dictionary _get_public_constants() const override { return {}; }
	godot::TypedArray<godot::Dictionary> _get_public_annotations() const override { return {}; }
	void _profiling_start() override {}
	void _profiling_stop() override {}
	void _profiling_set_save_native_calls(bool) override {}
	int32_t _profiling_get_accumulated_data(godot::ScriptLanguageExtensionProfilingInfo *, int32_t) override {
		return 0;
	}
	int32_t _profiling_get_frame_data(godot::ScriptLanguageExtensionProfilingInfo *, int32_t) override { return 0; }
	void _frame() override { kas3::Project::collect_step(); }
	bool _handles_global_class_type(const godot::String &) const override { return false; }
	godot::Dictionary _get_global_class_name(const godot::String &) const override { return {}; }
};
