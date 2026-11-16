#include "script.h"

#include "language.h"
#include "members.h"
#include "program.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <mutex>
#include <unordered_set>
#include <vector>

using namespace godot;
using kas3::Project;

// Every script resource there is. Godot loads them on any thread (a scene loaded in a thread loads its
// scripts there, several at once), so the set is changed and looked through under a lock. Made once and
// never destroyed, as a thread may free a script while the game shuts down.
struct AllScripts {
	std::mutex lock;
	std::unordered_set<AS3Script *> scripts;
};

static AllScripts &all_scripts() {
	static AllScripts *all = new AllScripts();
	return *all;
}

AS3Script::AS3Script() {
	AllScripts &all = all_scripts();
	const std::lock_guard<std::mutex> held(all.lock);
	all.scripts.insert(this);
}

AS3Script::~AS3Script() {
	AllScripts &all = all_scripts();
	const std::lock_guard<std::mutex> held(all.lock);
	all.scripts.erase(this);
}

void AS3Script::refresh_all() {
	// Held while each is refreshed, outside the lock: refreshing one calls into Godot. One being freed (its
	// count gone to zero) gives a null reference, and is passed over.
	std::vector<Ref<AS3Script>> scripts;
	{
		AllScripts &all = all_scripts();
		const std::lock_guard<std::mutex> held(all.lock);
		scripts.reserve(all.scripts.size());
		for (AS3Script *script : all.scripts) {
			if (Ref<AS3Script> kept(script); kept.is_valid()) {
				scripts.push_back(kept);
			}
		}
	}
	for (const Ref<AS3Script> &script : scripts) {
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
	_update_exports();
}

bool AS3Script::_can_instantiate() const {
	return class_ != kas3::ir::NONE && !Engine::get_singleton()->is_editor_hint();
}

void *AS3Script::_placeholder_instance_create(Object *p_for_object) const {
	void *placeholder = gdextension_interface::placeholder_script_instance_create(AS3Language::singleton->_owner, _owner, p_for_object->_owner);
	placeholders_.insert(placeholder);
	const_cast<AS3Script *>(this)->_update_exports();
	return placeholder;
}

// The stand-ins are told of the exported fields and their first values. While the scripts have errors there
// is no class to tell of: the stand-ins keep what they hold.
void AS3Script::_update_exports() {
	if (class_ == kas3::ir::NONE) {
		return;
	}
	Array exported;
	Dictionary values;
	const TypedArray<Dictionary> properties = kas3::property_list(class_);
	for (int64_t i = 0; i < properties.size(); i++) {
		const Dictionary property = properties[i];
		if ((int)property["usage"] & PROPERTY_USAGE_EDITOR) {
			exported.push_back(property);
			Variant first;
			if (kas3::field_default(class_, property["name"], &first)) {
				values[property["name"]] = first;
			}
		}
	}
	for (void *placeholder : placeholders_) {
		gdextension_interface::placeholder_script_instance_update(placeholder, &exported, &values);
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
	if (!kas3::on_main_thread()) {
		// Not made later on the main thread: until then the object would have a script that is not there, and
		// what Godot does next on this thread (a loaded resource's values set) is refused anyway.
		UtilityFunctions::push_error(String("ActionScript: ") + String::utf8(file_.c_str()) + " was not attached to a " +
				p_for_object->get_class() + ": Godot attached it from another thread (a task of its worker threads, or a resource " +
				"loaded in a thread). AS3 runs on the main thread only: attach it, or load what has it, there.");
		return nullptr;
	}
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
	const kas3::Member member = kas3::find_member(p_class, p_name);
	return member.what == kas3::Member::What::Method ? member.index : kas3::ir::NONE;
}

TypedArray<Dictionary> AS3Script::_get_script_method_list() const {
	return kas3::method_list(class_);
}

TypedArray<Dictionary> AS3Script::_get_script_property_list() const {
	return kas3::property_list(class_);
}

bool AS3Script::_has_property_default_value(const StringName &p_property) const {
	Variant value;
	return kas3::field_default(class_, p_property, &value);
}

Variant AS3Script::_get_property_default_value(const StringName &p_property) const {
	Variant value;
	kas3::field_default(class_, p_property, &value);
	return value;
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
