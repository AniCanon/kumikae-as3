#include "script.h"

#include "language.h"
#include "program.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <unordered_set>

using namespace godot;
using kas3::Project;

static std::unordered_set<AS3Script *> &scripts() {
	static std::unordered_set<AS3Script *> all;
	return all;
}

AS3Script::AS3Script() {
	scripts().insert(this);
}

AS3Script::~AS3Script() {
	scripts().erase(this);
}

void AS3Script::refresh_all() {
	for (AS3Script *script : scripts()) {
		if (!script->file_.empty()) {
			script->set_file(String::utf8(script->file_.c_str()));
		}
	}
}

void AS3Script::set_file(const String &p_resource_path) {
	file_ = p_resource_path.utf8().get_data();
	source_ = FileAccess::get_file_as_string(p_resource_path);
	Project &program = Project::get();
	class_ = program.valid() ? program.class_of_file(file_) : kas3::ir::NONE;
	// Only a class that extends a Godot class can be attached to an object.
	if (class_ != kas3::ir::NONE && program.module().classes[class_].native_base.empty()) {
		UtilityFunctions::push_error(String("ActionScript: ") + p_resource_path +
				": its class does not extend a Godot class, so it can't be attached to anything.");
		class_ = kas3::ir::NONE;
	}
}

StringName AS3Script::_get_global_name() const {
	return StringName();
}

StringName AS3Script::_get_instance_base_type() const {
	if (class_ == kas3::ir::NONE) {
		return StringName("Object");
	}
	return StringName(Project::get().module().classes[class_].native_base.c_str());
}

void *AS3Script::_instance_create(Object *p_for_object) const {
	if (class_ == kas3::ir::NONE) {
		return nullptr;
	}
	// The script's code reaches the object's members as members of the class it extends.
	StringName base = _get_instance_base_type();
	if (!p_for_object->is_class(String(base))) {
		UtilityFunctions::push_error(String("ActionScript: ") + String::utf8(file_.c_str()) + " can only be attached to a " +
				String(base) + ", not to a " + p_for_object->get_class() + ".");
		return nullptr;
	}
	return create_script_instance(this, p_for_object);
}

bool AS3Script::_has_method(const StringName &p_method) const {
	return class_ != kas3::ir::NONE && script_method(class_, p_method) != kas3::ir::NONE;
}

namespace {

// The signals the class declares with [Signal], and those of the classes it extends.
std::vector<std::string> script_signals(uint32_t p_class) {
	std::vector<std::string> all;
	Project &program = Project::get();
	for (uint32_t at = p_class; program.valid() && at < program.module().classes.size(); at = program.module().classes[at].base) {
		const std::vector<std::string> &own = program.module().classes[at].signals;
		all.insert(all.end(), own.begin(), own.end());
	}
	return all;
}

} // namespace

bool AS3Script::_has_script_signal(const StringName &p_signal) const {
	const String wanted(p_signal);
	for (const std::string &name : script_signals(class_)) {
		if (String::utf8(name.c_str()) == wanted) {
			return true;
		}
	}
	return false;
}

// Each as Godot describes a signal: a name, and no arguments said (it is given what emit() is given).
TypedArray<Dictionary> AS3Script::_get_script_signal_list() const {
	TypedArray<Dictionary> list;
	for (const std::string &name : script_signals(class_)) {
		Dictionary returned;
		returned["name"] = String();
		returned["type"] = Variant::NIL;
		returned["class_name"] = StringName();
		returned["hint"] = 0;
		returned["hint_string"] = String();
		returned["usage"] = PROPERTY_USAGE_DEFAULT;
		Dictionary signal;
		signal["name"] = String::utf8(name.c_str());
		signal["args"] = Array();
		signal["default_args"] = Array();
		signal["return"] = returned;
		signal["flags"] = METHOD_FLAG_NORMAL;
		signal["id"] = 0;
		list.push_back(signal);
	}
	return list;
}

ScriptLanguage *AS3Script::_get_language() const {
	return AS3Language::singleton;
}

uint32_t script_method(uint32_t p_class, const StringName &p_name) {
	Project &program = Project::get();
	if (!program.valid() || p_class >= program.module().classes.size()) {
		return kas3::ir::NONE;
	}
	CharString utf8 = String(p_name).utf8();
	std::string_view name(utf8.get_data(), (size_t)utf8.length());
	for (const auto &entry : program.module().classes[p_class].virtuals) {
		if (entry.first == name) {
			return entry.second;
		}
	}
	return program.interpreter().find_method(p_class, name);
}

PackedStringArray AS3ScriptLoader::_get_recognized_extensions() const {
	PackedStringArray extensions;
	extensions.push_back("as");
	return extensions;
}

bool AS3ScriptLoader::_handles_type(const StringName &p_type) const {
	return p_type == StringName("Script") || p_type == StringName("AS3Script");
}

String AS3ScriptLoader::_get_resource_type(const String &p_path) const {
	return p_path.get_extension().to_lower() == "as" ? String("AS3Script") : String();
}

Variant AS3ScriptLoader::_load(const String &p_path, const String &, bool, int32_t) const {
	Ref<AS3Script> script;
	script.instantiate();
	script->set_file(p_path);
	return script;
}
