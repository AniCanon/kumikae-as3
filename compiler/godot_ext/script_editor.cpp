#include "script_editor.h"

#if !defined(IOS_ENABLED) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
#include "script.h"

#include <godot_cpp/classes/code_edit.hpp>
#include <godot_cpp/classes/editor_file_system.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/script_editor.hpp>
#include <godot_cpp/classes/script_editor_base.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

using namespace godot;

bool AS3ScriptSaver::_recognize(const Ref<Resource> &p_resource) const {
	return Object::cast_to<AS3Script>(p_resource.ptr()) != nullptr;
}

PackedStringArray AS3ScriptSaver::_get_recognized_extensions(const Ref<Resource> &p_resource) const {
	return _recognize(p_resource) ? PackedStringArray({ "as" }) : PackedStringArray();
}

Error AS3ScriptSaver::_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t) {
	const Ref<AS3Script> script = p_resource;
	if (script.is_null() || p_path.get_extension().to_lower() != "as") {
		return ERR_FILE_UNRECOGNIZED;
	}
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_null()) {
		return FileAccess::get_open_error();
	}
	if (!file->store_string(script->_get_source_code())) {
		return ERR_FILE_CANT_WRITE;
	}
	file->close();
	kas3::forget_editor_sources();
	return OK;
}

void AS3ScriptEditor::_enter_tree() {
	EditorInterface *editor = EditorInterface::get_singleton();
	ScriptEditor *scripts = editor->get_script_editor();
	highlighter_.instantiate();
	scripts->register_syntax_highlighter(highlighter_);
	scripts->connect("editor_script_changed", callable_mp(this, &AS3ScriptEditor::script_changed));
	scripts->connect("script_close", callable_mp(this, &AS3ScriptEditor::script_closed));
	script_changed(scripts->get_current_script());
	saver_.instantiate();
	ResourceSaver::get_singleton()->add_resource_format_saver(saver_);
	EditorFileSystem *files = editor->get_resource_filesystem();
	files->connect("filesystem_changed", callable_mp(this, &AS3ScriptEditor::files_changed));
	files->connect("resources_reload", callable_mp(this, &AS3ScriptEditor::files_reloaded));
}

void AS3ScriptEditor::_exit_tree() {
	EditorInterface *editor = EditorInterface::get_singleton();
	ScriptEditor *scripts = editor->get_script_editor();
	scripts->disconnect("editor_script_changed", callable_mp(this, &AS3ScriptEditor::script_changed));
	scripts->disconnect("script_close", callable_mp(this, &AS3ScriptEditor::script_closed));
	EditorFileSystem *files = editor->get_resource_filesystem();
	files->disconnect("filesystem_changed", callable_mp(this, &AS3ScriptEditor::files_changed));
	files->disconnect("resources_reload", callable_mp(this, &AS3ScriptEditor::files_reloaded));
	ResourceSaver::get_singleton()->remove_resource_format_saver(saver_);
	saver_.unref();
	scripts->unregister_syntax_highlighter(highlighter_);
	highlighter_.unref();
	kas3::forget_editor_buffers();
}

void AS3ScriptEditor::files_changed() {
	kas3::forget_editor_sources();
}

void AS3ScriptEditor::files_reloaded(const PackedStringArray &p_paths) {
	for (const String &path : p_paths) {
		if (path.ends_with(".as")) {
			kas3::forget_editor_sources();
			return;
		}
	}
}

void AS3ScriptEditor::script_changed(const Ref<Script> &p_script) {
	if (!Object::cast_to<AS3Script>(p_script.ptr())) {
		return;
	}
	ScriptEditor *scripts = EditorInterface::get_singleton()->get_script_editor();
	ScriptEditorBase *tab = scripts->get_current_editor();
	// Godot can also signal when opening a background tab during layout restoration.
	if (tab && scripts->get_current_script() == p_script) {
		if (const auto *code = Object::cast_to<CodeEdit>(tab->get_base_editor())) {
			kas3::track_editor_buffer(p_script->get_instance_id(), code->get_instance_id());
		}
	}
}

void AS3ScriptEditor::script_closed(const Ref<Script> &p_script) {
	if (p_script.is_valid()) {
		kas3::forget_editor_buffer(p_script->get_instance_id());
	}
}
#endif
