#include "language.h"

#if !defined(IOS_ENABLED) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
#include "builtins.h"
#include "query.h"
#include "script_editor.h"
#include "sources.h"

#include <godot_cpp/classes/code_edit.hpp>
#include <godot_cpp/classes/file_access.hpp>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>

using namespace godot;
using namespace kas3;

namespace {

std::string utf8(const String &p_text) {
	const CharString bytes = p_text.utf8();
	return std::string(bytes.get_data(), static_cast<size_t>(bytes.length()));
}

using Buffers = std::map<std::string, std::string>;

// ResourceSaver can update a file on a worker. Only the editor thread touches the compilation.
std::atomic<bool> sources_changed{ false };

std::unordered_map<uint64_t, uint64_t> &editor_tabs() {
	static std::unordered_map<uint64_t, uint64_t> tabs;
	return tabs;
}

void strip_byte_order_mark(std::string &r_text) {
	if (r_text.compare(0, BYTE_ORDER_MARK.size(), BYTE_ORDER_MARK) == 0) {
		r_text.erase(0, BYTE_ORDER_MARK.size());
	}
}

Buffers open_buffers(const String &p_active, std::string p_text) {
	Buffers buffers;
	buffers.emplace(utf8(p_active), std::move(p_text));
	for (const auto &[script_id, editor_id] : editor_tabs()) {
		const auto *script = Object::cast_to<Script>(ObjectDB::get_instance(ObjectID(script_id)));
		const auto *code = Object::cast_to<CodeEdit>(ObjectDB::get_instance(ObjectID(editor_id)));
		if (!script || !code || code->get_version() == code->get_saved_version()) {
			continue;
		}
		const String path = script->get_path();
		if (path != p_active && path.begins_with("res://")) {
			std::string text = utf8(code->get_text());
			strip_byte_order_mark(text);
			buffers.emplace(utf8(path), std::move(text));
		}
	}
	return buffers;
}

class EditorCompletion {
	struct File {
		Unit *unit;
		std::shared_ptr<Parsed> saved;
	};

	std::vector<std::shared_ptr<Parsed>> builtins_;
	std::shared_ptr<Parsed> godot_;
	std::unique_ptr<Compilation> compilation_;
	std::unordered_map<std::string, File> files_;
	std::set<std::string> overlaid_;

	void build(const Buffers &p_buffers) {
		if (!godot_) {
			builtins_ = parse_builtins();
			godot_ = parse_file(GODOT_DECLARATIONS_PATH, godot_declarations());
		}
		forget_sources();
		compilation_ = std::make_unique<Compilation>(builtins_);
		compilation_->add(godot_, true);
		std::vector<std::string> paths;
		collect_scripts("res://", paths, true);
		for (const auto &[path, text] : p_buffers) {
			paths.push_back(path);
		}
		std::sort(paths.begin(), paths.end());
		paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
		for (const std::string &path : paths) {
			auto saved = parse_file(path, utf8(FileAccess::get_file_as_string(String::utf8(path.c_str()))));
			auto parsed = saved;
			const auto buffer = p_buffers.find(path);
			if (buffer != p_buffers.end() && saved->source.text() != buffer->second) {
				parsed = parse_file(path, buffer->second);
				overlaid_.insert(path);
			}
			files_.emplace(path, File{ compilation_->add(std::move(parsed)), std::move(saved) });
		}
		compilation_->prepare();
	}

	void sync(const Buffers &p_buffers) {
		if (sources_changed.exchange(false) || std::any_of(p_buffers.begin(), p_buffers.end(),
				[this](const auto &p_buffer) { return files_.find(p_buffer.first) == files_.end(); })) {
			build(p_buffers);
			return;
		}
		std::vector<std::pair<Unit *, std::shared_ptr<Parsed>>> changes;
		std::set<std::string> overlaid;
		for (const auto &[path, text] : p_buffers) {
			const File &file = files_.at(path);
			if (text != file.saved->source.text()) {
				overlaid.insert(path);
				if (text != file.unit->source.text()) {
					changes.emplace_back(file.unit, parse_file(path, text));
				}
			}
		}
		for (const std::string &path : overlaid_) {
			if (overlaid.find(path) == overlaid.end()) {
				const File &file = files_.at(path);
				changes.emplace_back(file.unit, file.saved);
			}
		}
		if (!changes.empty() && !compilation_->update(changes)) {
			build(p_buffers);
			return;
		}
		overlaid_ = std::move(overlaid);
	}

public:
	void forget_sources() {
		overlaid_.clear();
		files_.clear();
		compilation_.reset();
	}

	CompletionResult complete(const String &p_path, std::string p_text, uint32_t p_offset) {
		sync(open_buffers(p_path, std::move(p_text)));
		Unit &unit = *files_.at(utf8(p_path)).unit;
		compilation_->check_bodies(unit);
		compilation_->settle_doubt();
		Queries queries(*compilation_);
		return queries.complete(unit, p_offset);
	}
};

EditorCompletion &editor_completion() {
	static EditorCompletion editor;
	return editor;
}

CodeEdit::CodeCompletionKind option_kind(const Completion &p_item) {
	if (p_item.is_keyword) {
		return CodeEdit::KIND_KEYWORD;
	}
	if (p_item.is_package) {
		return CodeEdit::KIND_PLAIN_TEXT;
	}
	if (p_item.is_const) {
		return CodeEdit::KIND_CONSTANT;
	}
	switch (p_item.kind) {
		case SymbolKind::Class:
		case SymbolKind::Interface:
			return CodeEdit::KIND_CLASS;
		case SymbolKind::Function:
		case SymbolKind::Method:
		case SymbolKind::LocalFunction:
			return CodeEdit::KIND_FUNCTION;
		case SymbolKind::Field:
		case SymbolKind::Property:
			return CodeEdit::KIND_MEMBER;
		default:
			return CodeEdit::KIND_VARIABLE;
	}
}

Array editor_options(const CompletionResult &p_result) {
	Array options;
	for (const Completion &item : p_result.items) {
		// Godot inserts a single string at the caret. A keyword can still be offered without its snippet.
		if (item.additional_edit || (item.edit && !item.is_keyword)) {
			continue;
		}
		const String name = String::utf8(item.name.c_str());
		Dictionary option;
		option["kind"] = option_kind(item);
		option["display"] = name;
		option["insert_text"] = name;
		option["font_color"] = Color();
		option["icon"] = Variant();
		option["default_value"] = Variant();
		const bool local = !item.is_keyword && !item.is_package &&
				(item.kind == SymbolKind::Local || item.kind == SymbolKind::Parameter);
		option["location"] = local ?
				CodeEdit::LOCATION_LOCAL : CodeEdit::LOCATION_OTHER;
		options.push_back(option);
	}
	return options;
}

} // namespace

void kas3::forget_editor_sources() {
	sources_changed.store(true);
}

void kas3::track_editor_buffer(uint64_t p_script, uint64_t p_editor) {
	editor_tabs().insert_or_assign(p_script, p_editor);
}

void kas3::forget_editor_buffer(uint64_t p_script) {
	editor_tabs().erase(p_script);
}

void kas3::forget_editor_buffers() {
	editor_tabs().clear();
	editor_completion().forget_sources();
}

Dictionary AS3Language::_complete_code(const String &p_code, const String &p_path, Object *) const {
	// CodeEdit sends the whole unsaved document with a caret marker. The compiler uses UTF-8 byte offsets.
	const int64_t caret = p_code.find(String::chr(0xffff));
	if (!Engine::get_singleton()->is_editor_hint() || caret < 0 || !p_path.begins_with("res://")) {
		return {};
	}
	std::string text = utf8(p_code.left(caret));
	strip_byte_order_mark(text);
	const uint32_t offset = static_cast<uint32_t>(text.size());
	text += utf8(p_code.substr(caret + 1));
	const Array options = editor_options(editor_completion().complete(p_path, std::move(text), offset));
	Dictionary result;
	result["options"] = options;
	result["force"] = false;
	result["call_hint"] = String();
	result["result"] = OK;
	return result;
}
#else
godot::Dictionary AS3Language::_complete_code(const godot::String &, const godot::String &, godot::Object *) const {
	return {};
}
#endif
