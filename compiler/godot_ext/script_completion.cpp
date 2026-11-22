#include "language.h"

#if !defined(IOS_ENABLED) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
#include "builtins.h"
#include "query.h"
#include "script_editor.h"
#include "sources.h"

#include <godot_cpp/classes/code_edit.hpp>
#include <godot_cpp/classes/file_access.hpp>

#include <algorithm>

using namespace godot;
using namespace kas3;

namespace {

std::string utf8(const String &p_text) {
	const CharString bytes = p_text.utf8();
	return std::string(bytes.get_data(), static_cast<size_t>(bytes.length()));
}

class EditorCompletion {
	std::vector<std::shared_ptr<Parsed>> builtins_;
	std::shared_ptr<Parsed> godot_;
	std::unique_ptr<Compilation> compilation_;
	std::unordered_map<std::string, Unit *> units_;
	Unit *active_ = nullptr;
	std::shared_ptr<Parsed> on_disk_;

	Unit &build(const std::string &p_path, const std::string &p_text) {
		if (!godot_) {
			builtins_ = parse_builtins();
			godot_ = parse_file(GODOT_DECLARATIONS_PATH, godot_declarations());
		}
		forget_sources();
		compilation_ = std::make_unique<Compilation>(builtins_);
		compilation_->add(godot_, true);
		std::vector<std::string> paths;
		collect_scripts("res://", paths, true);
		paths.push_back(p_path);
		std::sort(paths.begin(), paths.end());
		paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
		for (const std::string &path : paths) {
			auto disk = parse_file(path, utf8(FileAccess::get_file_as_string(String::utf8(path.c_str()))));
			if (path == p_path) {
				on_disk_ = std::move(disk);
				active_ = compilation_->add(parse_file(p_path, p_text));
				units_.emplace(path, active_);
			} else {
				units_.emplace(path, compilation_->add(std::move(disk)));
			}
		}
		compilation_->prepare();
		return *active_;
	}

	Unit &sync(const std::string &p_path, const std::string &p_text) {
		const auto found = units_.find(p_path);
		if (found == units_.end()) {
			return build(p_path, p_text);
		}
		Unit *unit = found->second;
		std::vector<std::pair<Unit *, std::shared_ptr<Parsed>>> changes;
		if (unit != active_) {
			if (active_->parsed != on_disk_) {
				changes.emplace_back(active_, std::move(on_disk_));
			}
			on_disk_ = unit->parsed;
			active_ = unit;
		}
		if (unit->source.text() != p_text) {
			changes.emplace_back(unit, parse_file(p_path, p_text));
		}
		if (!changes.empty() && !compilation_->update(changes)) {
			return build(p_path, p_text);
		}
		return *unit;
	}

public:
	void forget_sources() {
		active_ = nullptr;
		on_disk_.reset();
		units_.clear();
		compilation_.reset();
	}

	CompletionResult complete(const std::string &p_path, const std::string &p_text, uint32_t p_offset) {
		Unit &unit = sync(p_path, p_text);
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
	editor_completion().forget_sources();
}

Dictionary AS3Language::_complete_code(const String &p_code, const String &p_path, Object *) const {
	// CodeEdit sends the whole unsaved document with a caret marker. The compiler uses UTF-8 byte offsets.
	const int64_t caret = p_code.find(String::chr(0xffff));
	if (!Engine::get_singleton()->is_editor_hint() || caret < 0 || !p_path.begins_with("res://")) {
		return {};
	}
	std::string text = utf8(p_code.left(caret));
	if (text.compare(0, BYTE_ORDER_MARK.size(), BYTE_ORDER_MARK) == 0) {
		text.erase(0, BYTE_ORDER_MARK.size());
	}
	const uint32_t offset = static_cast<uint32_t>(text.size());
	text += utf8(p_code.substr(caret + 1));
	const Array options = editor_options(editor_completion().complete(utf8(p_path), text, offset));
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
