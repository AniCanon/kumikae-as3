// Editor-only services for .as files opened in Godot.
#pragma once

#if !defined(IOS_ENABLED) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_syntax_highlighter.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>

#include <utility>
#include <vector>

namespace godot {
class Script;
}

class AS3SyntaxHighlighter : public godot::EditorSyntaxHighlighter {
	GDCLASS(AS3SyntaxHighlighter, godot::EditorSyntaxHighlighter)

	mutable bool dirty_ = true;
	mutable std::vector<std::vector<std::pair<int, godot::Color>>> lines_;
	void rebuild() const;
	void text_edited(int, int) { dirty_ = true; }

protected:
	static void _bind_methods() {}

public:
	godot::String _get_name() const override;
	godot::PackedStringArray _get_supported_languages() const override;
	godot::Ref<godot::EditorSyntaxHighlighter> _create() const override;
	godot::Dictionary _get_line_syntax_highlighting(int32_t p_line) const override;
	void _update_cache() override;
};

class AS3ScriptSaver : public godot::ResourceFormatSaver {
	GDCLASS(AS3ScriptSaver, godot::ResourceFormatSaver)

protected:
	static void _bind_methods() {}

public:
	bool _recognize(const godot::Ref<godot::Resource> &p_resource) const override;
	godot::PackedStringArray _get_recognized_extensions(const godot::Ref<godot::Resource> &p_resource) const override;
	godot::Error _save(const godot::Ref<godot::Resource> &p_resource, const godot::String &p_path, uint32_t p_flags) override;
};

class AS3ScriptEditor : public godot::EditorPlugin {
	GDCLASS(AS3ScriptEditor, godot::EditorPlugin)

	godot::Ref<AS3SyntaxHighlighter> highlighter_;
	godot::Ref<AS3ScriptSaver> saver_;
	void files_changed();
	void files_reloaded(const godot::PackedStringArray &p_paths);
	void script_changed(const godot::Ref<godot::Script> &p_script);
	void script_closed(const godot::Ref<godot::Script> &p_script);

protected:
	static void _bind_methods() {}

public:
	void _enter_tree() override;
	void _exit_tree() override;
};

namespace kas3 {
void forget_editor_sources();
void track_editor_buffer(uint64_t p_script, uint64_t p_editor);
void forget_editor_buffer(uint64_t p_script);
void forget_editor_buffers();
}
#endif
