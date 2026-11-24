// Where values come from in Godot (EngineSource): its value types' defaults, constants and constructors,
// its singletons, and its classes' static methods; and the time (Clock).
#include "godot_calls.h"
#include "godot_host.h"
#include "handles.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/expression.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>

using namespace godot;

namespace kas3 {

// The canonical UTF-8 name of each Variant type, shared by class checks and lookups by name.
std::string_view value_name(int p_type) {
	static const auto names = [] {
		std::array<std::string, Variant::VARIANT_MAX> found;
		for (int i = 0; i < Variant::VARIANT_MAX; i++) {
			const CharString text = Variant::get_type_name(static_cast<Variant::Type>(i)).utf8();
			found[i].assign(text.get_data(), static_cast<size_t>(text.length()));
		}
		return found;
	}();
	return names[p_type];
}

std::optional<int> value_type(std::string_view p_name) {
	for (int i = 1; i < Variant::VARIANT_MAX; i++) {
		if (i != Variant::OBJECT && value_name(i) == p_name) {
			return i;
		}
	}
	return std::nullopt;
}

namespace {
// Every default Godot worked out, by its text, for the whole run: a program loaded again (a reload, a new
// play in the editor) finds them here. Let go of before the extension goes, while Godot is still there.
std::unordered_map<std::string, Variant> &defaults() {
	static std::unordered_map<std::string, Variant> *kept = new std::unordered_map<std::string, Variant>();
	return *kept;
}
} // namespace

void forget_defaults() {
	defaults().clear();
}

const Variant *GodotHost::evaluated(std::string_view p_expression, std::string *r_error) {
	Evaluated &entry = evaluated_[Place{ p_expression.data(), p_expression.size() }];
	if (entry.value && entry.text == p_expression) {
		return entry.value;
	}
	entry.text.assign(p_expression.data(), p_expression.size());
	auto found = defaults().find(entry.text);
	if (found == defaults().end()) {
		// Godot reads its own way of writing a value.
		Ref<Expression> expression;
		expression.instantiate();
		Variant value;
		if (expression->parse(to_godot(p_expression)) == OK) {
			value = expression->execute(Array(), nullptr, false);
		}
		if (value.get_type() == Variant::NIL || expression->has_execute_failed()) {
			*r_error = "Godot could not work out its default '" + entry.text + "'";
			entry.value = nullptr;
			return nullptr;
		}
		found = defaults().emplace(entry.text, value).first;
	}
	entry.value = &found->second;
	return entry.value;
}

bool GodotHost::evaluate(std::string_view p_expression, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	const Variant *kept = evaluated(p_expression, r_error);
	if (!kept) {
		return false;
	}
	// Godot shares an Array or a Dictionary between everything that holds it, and may write into one it is
	// given: each call gets its own.
	const Variant::Type type = kept->get_type();
	const Variant value = type == Variant::ARRAY || type == Variant::DICTIONARY ? kept->duplicate() : *kept;
	return take(p_result, value, p_expression, r_result, r_error);
}

// Asked once for each singleton (the interpreter keeps it), so the list may be looked through.
void *GodotHost::singleton(std::string_view p_class, std::string *r_error) {
	const StringName &singleton_name = name(p_class);
	Engine *engine = Engine::get_singleton();
	// An editor-only one is not in the list outside the editor; asking Godot for it would make Godot report
	// it too, besides the AS3 error.
	const bool listed = engine && engine->has_singleton(singleton_name) && engine->get_singleton_list().has(String(singleton_name));
	Object *object = listed ? engine->get_singleton(singleton_name) : nullptr;
	if (!object) {
		// Some exist only in the editor, or only on one platform.
		*r_error = "Godot has no '" + std::string(p_class) + "' here";
		return nullptr;
	}
	// Each singleton is named for its class: the class the script names is the one it gets.
	if (!object->is_class(String(singleton_name))) {
		CharString got = object->get_class().utf8();
		*r_error = "Godot's '" + std::string(p_class) + "' is a " + std::string(got.get_data()) + ", not a " + std::string(p_class);
		return nullptr;
	}
	return make_handle(object);
}

bool GodotHost::constant(std::string_view p_class, std::string_view p_name, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	const std::optional<int> type = value_type(p_class);
	Variant value;
	if (type) {
		gdextension_interface::variant_get_constant_value((GDExtensionVariantType)*type, name(p_name)._native_ptr(), value._native_ptr());
	}
	// Godot answers an unknown name with the integer -1. No constant asked for here is a number (those have
	// their values in the declarations), so a number where a Godot value is wanted means there is none.
	if (value.get_type() == Variant::NIL || (value.get_type() == Variant::INT && ir::is_engine(p_result.kind))) {
		*r_error = "Godot has no constant '" + std::string(p_name) + "' in '" + std::string(p_class) + "'";
		return false;
	}
	return take(p_result, value, p_name, r_result, r_error);
}

bool GodotHost::call_static(std::string_view p_class, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count,
		const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	if (p_class.empty()) {
		return call_utility(p_method, p_arguments, p_count, p_result, r_result, r_error);
	}
	// A class's static method comes with its entry in the name table ("print_orphan_nodes method 2648703342
	// void -"): the hash is its third word.
	const std::string_view method = p_method.substr(0, p_method.find(' '));
	const StringName &method_name = name(method);
	Arguments arguments(p_arguments, p_count);
	Variant result;
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	if (const std::optional<int> type = value_type(p_class)) {
		Variant::callp_static((Variant::Type)*type, method_name, arguments.pointers(), (int)p_count, result, error);
	} else {
		const std::string key = std::string(p_class) + "." + std::string(p_method);
		auto found = static_methods_.find(key);
		if (found == static_methods_.end()) {
			const std::vector<std::string_view> words = godot_words(p_method);
			const bool hashed = words.size() > 2 && words[1] == "method" && words[2] != "-";
			const GDExtensionInt hash = hashed ? std::strtoll(std::string(words[2]).c_str(), nullptr, 10) : 0;
			const StringName class_name(String::utf8(p_class.data(), (int64_t)p_class.size()));
			GDExtensionMethodBindPtr bound = !hashed ? nullptr
					: gdextension_interface::classdb_get_method_bind(class_name._native_ptr(), method_name._native_ptr(), hash);
			found = static_methods_.emplace(key, bound).first;
		}
		if (!found->second) {
			*r_error = "Godot has no '" + std::string(method) + "' in '" + std::string(p_class) + "'";
			return false;
		}
		gdextension_interface::object_method_bind_call(found->second, nullptr, reinterpret_cast<GDExtensionConstVariantPtr *>(arguments.pointers()),
				(GDExtensionInt)p_count, result._native_ptr(), &error);
	}
	if (error.error != GDEXTENSION_CALL_OK) {
		*r_error = describe(error, std::string(p_class) + "." + std::string(method));
		return false;
	}
	return take(p_result, result, method, r_result, r_error);
}

void *GodotHost::construct(std::string_view p_type, const vm::Argument *p_arguments, size_t p_count, std::string *r_error) {
	const std::optional<int> type = value_type(p_type);
	if (!type) {
		// Not a value type: one of Godot's classes, made as `ClassName.new()` makes it.
		const StringName &klass = name(p_type);
		ClassDBSingleton *classes = ClassDBSingleton::get_singleton();
		if (p_count != 0 || !classes->class_exists(klass)) {
			*r_error = "Godot has no value type '" + std::string(p_type) + "'";
			return nullptr;
		}
		Variant object = classes->can_instantiate(klass) ? classes->instantiate(klass) : Variant();
		if (object.get_type() != Variant::OBJECT) {
			*r_error = "Godot can't make a '" + std::string(p_type) + "': it is a class to extend, not one to create";
			return nullptr;
		}
		return make_handle(object);
	}
	Arguments arguments(p_arguments, p_count);
	Variant *made = make_handle();
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	gdextension_interface::variant_construct((GDExtensionVariantType)*type, made->_native_ptr(),
			reinterpret_cast<GDExtensionConstVariantPtr *>(arguments.pointers()), (int32_t)p_count, &error);
	if (error.error != GDEXTENSION_CALL_OK) {
		free_handle(made);
		*r_error = describe(error, "new " + std::string(p_type));
		return nullptr;
	}
	return made;
}

double GodotHost::now() {
	return std::floor(Time::get_singleton()->get_unix_time_from_system() * 1000.0);
}

double GodotHost::local_offset(double p_utc) {
	return vm::SystemClock::system_offset(p_utc);
}

} // namespace kas3
