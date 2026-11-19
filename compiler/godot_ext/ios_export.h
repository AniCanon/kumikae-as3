// Godot and the IDE both use this plugin when exporting an iOS project on a Mac.
#pragma once

#if defined(MACOS_ENABLED)
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

protected:
	static void _bind_methods() {}

public:
	godot::String _get_name() const override { return "ActionScript"; }
	bool _supports_platform(const godot::Ref<godot::EditorExportPlatform> &p_platform) const override;
	godot::TypedArray<godot::Dictionary> _get_export_options(const godot::Ref<godot::EditorExportPlatform> &p_platform) const override;
	void _export_begin(const godot::PackedStringArray &p_features, bool p_debug, const godot::String &p_path, uint32_t p_flags) override;
	void _export_file(const godot::String &p_path, const godot::String &, const godot::PackedStringArray &) override;
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
