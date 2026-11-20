// Godot and the IDE both use this plugin when exporting iOS or Android projects.
#pragma once

#if !defined(IOS_ENABLED) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/editor_export_platform.hpp>
#include <godot_cpp/classes/editor_export_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>

#include <string>
#include <vector>

class AS3IOSExport : public godot::EditorExportPlugin {
	GDCLASS(AS3IOSExport, godot::EditorExportPlugin)

	std::vector<std::string> scripts_;
	bool enabled_ = false;
	bool debug_ = false;
	bool ios_ = false;
	bool web_ = false;
	bool android_ = false;
	godot::Ref<godot::DirAccess> android_temp_;

protected:
	static void _bind_methods() {}

public:
	godot::String _get_name() const override { return "ActionScript"; }
	bool _supports_platform(const godot::Ref<godot::EditorExportPlatform> &p_platform) const override;
	godot::TypedArray<godot::Dictionary> _get_export_options(const godot::Ref<godot::EditorExportPlatform> &p_platform) const override;
	void _export_begin(const godot::PackedStringArray &p_features, bool p_debug, const godot::String &p_path, uint32_t p_flags) override;
	void _export_file(const godot::String &p_path, const godot::String &, const godot::PackedStringArray &) override;
	bool _begin_customize_scenes(const godot::Ref<godot::EditorExportPlatform> &, const godot::PackedStringArray &) const override;
	godot::Node *_customize_scene(godot::Node *, const godot::String &) override { return nullptr; }
	uint64_t _get_customization_configuration_hash() const override { return 0; } // Scenes are used as-is.
	void _end_customize_scenes() override;
	void _end_generate_apple_embedded_project(const godot::String &p_path, bool) override;
	void _export_end() override;
};

class AS3ExportEditor : public godot::EditorPlugin {
	GDCLASS(AS3ExportEditor, godot::EditorPlugin)

	godot::Ref<AS3IOSExport> exporter_;

protected:
	static void _bind_methods() {}

public:
	void _enter_tree() override;
	void _exit_tree() override;
};
#endif
