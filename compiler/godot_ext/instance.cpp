// The object Godot holds for each object a script is attached to: the engine's calls arrive here and go
// on into the interpreter.
#include "any.h"
#include "instance.h"
#include "language.h"
#include "members.h"
#include "program.h"
#include "script.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace godot;
using kas3::Project;
namespace ir = kas3::ir;
namespace vm = kas3::vm;

// Every script instance there is: a reload gives each its object's counterpart in the new program.
static std::unordered_set<ScriptInstance *> &instances() {
	static std::unordered_set<ScriptInstance *> all;
	return all;
}
// The same, by the Godot object each is attached to.
static std::unordered_map<const Object *, ScriptInstance *> &instances_on() {
	static std::unordered_map<const Object *, ScriptInstance *> all;
	return all;
}

vm::Object *kas3::script_object(const Object *p_owner) {
	auto found = instances_on().find(p_owner);
	return found == instances_on().end() ? nullptr : found->second->object;
}

std::vector<std::pair<vm::Object *, Object *>> kas3::instances_outside(const Node *p_scene) {
	std::vector<std::pair<vm::Object *, Object *>> outside;
	for (const ScriptInstance *instance : instances()) {
		const Node *node = Object::cast_to<Node>(instance->owner);
		if (!p_scene || !node || (node != p_scene && !p_scene->is_ancestor_of(const_cast<Node *>(node)))) {
			outside.emplace_back(instance->object, instance->owner);
		}
	}
	return outside;
}

bool kas3::instances_can_move(vm::Reload &p_reload) {
	for (const ScriptInstance *instance : instances()) {
		vm::Object *moved = p_reload.object(instance->object);
		if (!moved) {
			return false;
		}
		vm::release(moved); // The reload keeps its own until it is done.
	}
	return true;
}

void kas3::move_instances(vm::Interpreter &p_old, vm::Reload &p_reload) {
	for (ScriptInstance *instance : instances()) {
		vm::Object *old = instance->object;
		if (instance->counted) {
			vm::retain(old); // For the move, the instance holds it as any other instance does.
		}
		instance->object = p_reload.object(old);
		if (instance->counted) {
			kas3::counted_moved(old, instance);
		}
		p_old.release_object(old);
		if (instance->counted && vm::release(instance->object)) {
			// Nothing in the program holds it: the instance keeps it, as before.
		}
		if (Object::cast_to<RefCounted>(instance->owner)) {
			// The reload copied its hold on its Godot object as an ordinary handle, which would keep the object.
			vm::Slot borrowed;
			borrowed.bits = 0;
			borrowed.p = Project::get().host().borrow(instance->owner);
			p_reload.target().set_field(instance->object, 0, borrowed);
		}
	}
}

static GDExtensionVariantType expected_type(ir::Kind p_kind) {
	switch (p_kind) {
		case ir::Kind::F64:
			return GDEXTENSION_VARIANT_TYPE_FLOAT;
		case ir::Kind::I32:
		case ir::Kind::U32:
		case ir::Kind::I64:
			return GDEXTENSION_VARIANT_TYPE_INT;
		case ir::Kind::Bool:
			return GDEXTENSION_VARIANT_TYPE_BOOL;
		case ir::Kind::Str:
			return GDEXTENSION_VARIANT_TYPE_STRING;
		default:
			return GDEXTENSION_VARIANT_TYPE_OBJECT;
	}
}

// The script's public getter or setter of that AS3 name, "get x" or "set x" among its methods; NONE if it has none.
static uint32_t accessor(const vm::Object *p_object, const char *p_half, GDExtensionConstStringNamePtr p_name) {
	CharString name = String(*reinterpret_cast<const StringName *>(p_name)).utf8();
	const uint32_t found = Project::get().interpreter().find_method(p_object->klass, std::string(p_half) + name.get_data());
	return found != ir::NONE && Project::get().module().functions[found].is_public ? found : ir::NONE;
}

// GDScript writing `node.x = v` where the script has a setter.
static GDExtensionBool set_by_setter(ScriptInstance *p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
	const uint32_t setter = accessor(p_instance->object, "set ", p_name);
	if (setter == ir::NONE) {
		return false;
	}
	if (!kas3::on_main_thread()) {
		kas3::refuse_thread(setter);
		return false;
	}
	const Variant *value = reinterpret_cast<const Variant *>(p_value);
	Variant ignored;
	GDExtensionCallError error{};
	kas3::call_script(p_instance->object, setter, &value, 1, &ignored, &error);
	return error.error == GDEXTENSION_CALL_OK; // A value of the wrong kind: Godot says it could not be set.
}

// A field asked for from another thread is refused, and said so, as a call is: AS3 runs on the main thread.
static bool field_on_main_thread(const StringName &p_name) {
	if (kas3::on_main_thread()) {
		return true;
	}
	UtilityFunctions::push_error(String("Error: Godot asked for the field '") + String(p_name) +
			"' from another thread. AS3 runs on the main thread only: it was not read or written.");
	return false;
}

// A field of the script, by its AS3 name, written by Godot: only a public field, with what fits it, or a
// public setter.
static GDExtensionBool instance_set(GDExtensionScriptInstanceDataPtr p_data, GDExtensionConstStringNamePtr p_name,
		GDExtensionConstVariantPtr p_value) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	const kas3::Member member = kas3::find_member(instance->object->klass, name);
	if (member.what != kas3::Member::What::Field) {
		return set_by_setter(instance, p_name, p_value); // A setter, if it has one.
	}
	return field_on_main_thread(name) && kas3::write_field(instance->object, member.index, *reinterpret_cast<const Variant *>(p_value));
}

// A script object as Godot holds it: the Godot object it is attached to, for a class that extends one of
// Godot's. False for any other script object, which has no form Godot can hold.
bool kas3::as_godot_object(const vm::Object *p_object, Variant *r_out) {
	const ir::Module &module = Project::get().module();
	if (!p_object || p_object->klass >= module.classes.size() || module.classes[p_object->klass].native_base.empty()) {
		return false;
	}
	const void *owner = const_cast<vm::Object *>(p_object)->fields()[0].p;
	*r_out = owner ? *static_cast<const Variant *>(owner) : Variant();
	return true;
}

// The script's getter of that name, called as GDScript reads `node.x`.
static GDExtensionBool get_by_getter(ScriptInstance *p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_value) {
	const uint32_t getter = accessor(p_instance->object, "get ", p_name);
	if (getter == ir::NONE) {
		return false;
	}
	if (!kas3::on_main_thread()) {
		kas3::refuse_thread(getter);
		return false;
	}
	GDExtensionCallError error{};
	Variant *out = reinterpret_cast<Variant *>(r_value);
	*out = Variant(); // An error in the getter, reported as Godot reports a script's own, reads as null.
	kas3::call_script(p_instance->object, getter, nullptr, 0, out, &error);
	return error.error == GDEXTENSION_CALL_OK;
}

// A field of the script, by its AS3 name, read by Godot: only a public field, or a public getter.
static GDExtensionBool instance_get(GDExtensionScriptInstanceDataPtr p_data, GDExtensionConstStringNamePtr p_name,
		GDExtensionVariantPtr r_value) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	const kas3::Member member = kas3::find_member(instance->object->klass, name);
	if (member.what != kas3::Member::What::Field) {
		return get_by_getter(instance, p_name, r_value); // A getter, if it has one.
	}
	if (!field_on_main_thread(name)) {
		return false;
	}
	*reinterpret_cast<Variant *>(r_value) = kas3::read_field(instance->object, member.index);
	return true;
}

// The lists Godot asks an instance for, in the form the interface takes, kept until Godot frees them.
struct Listed {
	std::deque<StringName> names;
	std::deque<String> texts;
	std::deque<std::vector<GDExtensionPropertyInfo>> arguments;
	// Godot reads a method's defaults as Variants side by side, whatever the field's declared type says.
	std::deque<std::vector<Variant>> defaults;
	std::vector<GDExtensionPropertyInfo> properties;
	std::vector<GDExtensionMethodInfo> methods;

	GDExtensionPropertyInfo property(const Dictionary &p_property) {
		GDExtensionPropertyInfo info = {};
		info.type = (GDExtensionVariantType)(int)p_property["type"];
		info.name = &names.emplace_back(p_property["name"]);
		info.class_name = &names.emplace_back(p_property["class_name"]);
		info.hint = (uint32_t)(int)p_property["hint"];
		info.hint_string = &texts.emplace_back(p_property["hint_string"]);
		info.usage = (uint32_t)(int)p_property["usage"];
		return info;
	}
};

static std::unordered_map<const void *, std::unique_ptr<Listed>> &listed() {
	static std::unordered_map<const void *, std::unique_ptr<Listed>> all;
	return all;
}

static const GDExtensionPropertyInfo *instance_get_property_list(GDExtensionScriptInstanceDataPtr p_data, uint32_t *r_count) {
	const TypedArray<Dictionary> list = kas3::property_list(static_cast<ScriptInstance *>(p_data)->object->klass);
	std::unique_ptr<Listed> made = std::make_unique<Listed>();
	for (int64_t i = 0; i < list.size(); i++) {
		made->properties.push_back(made->property(list[i]));
	}
	*r_count = (uint32_t)made->properties.size();
	const GDExtensionPropertyInfo *out = made->properties.empty() ? nullptr : made->properties.data();
	if (out) {
		listed()[out] = std::move(made);
	}
	return out;
}

static void instance_free_property_list(GDExtensionScriptInstanceDataPtr, const GDExtensionPropertyInfo *p_list, uint32_t) {
	listed().erase(p_list);
}

static const GDExtensionMethodInfo *instance_get_method_list(GDExtensionScriptInstanceDataPtr p_data, uint32_t *r_count) {
	const TypedArray<Dictionary> list = kas3::method_list(static_cast<ScriptInstance *>(p_data)->object->klass);
	std::unique_ptr<Listed> made = std::make_unique<Listed>();
	for (int64_t i = 0; i < list.size(); i++) {
		const Dictionary method = list[i];
		GDExtensionMethodInfo info = {};
		info.name = &made->names.emplace_back(method["name"]);
		info.return_value = made->property(method["return"]);
		info.flags = (uint32_t)(int)method["flags"];
		const Array arguments = method["args"];
		std::vector<GDExtensionPropertyInfo> &own = made->arguments.emplace_back();
		for (int64_t a = 0; a < arguments.size(); a++) {
			own.push_back(made->property(arguments[a]));
		}
		info.argument_count = (uint32_t)own.size();
		info.arguments = own.empty() ? nullptr : own.data();
		const Array defaults = method["default_args"];
		std::vector<Variant> &values = made->defaults.emplace_back();
		for (int64_t d = 0; d < defaults.size(); d++) {
			values.push_back(defaults[d]);
		}
		info.default_argument_count = (uint32_t)values.size();
		info.default_arguments = values.empty() ? nullptr : reinterpret_cast<GDExtensionVariantPtr *>(values.data());
		made->methods.push_back(info);
	}
	*r_count = (uint32_t)made->methods.size();
	const GDExtensionMethodInfo *out = made->methods.empty() ? nullptr : made->methods.data();
	if (out) {
		listed()[out] = std::move(made);
	}
	return out;
}

static void instance_free_method_list(GDExtensionScriptInstanceDataPtr, const GDExtensionMethodInfo *p_list, uint32_t) {
	listed().erase(p_list);
}

static GDExtensionObjectPtr instance_get_owner(GDExtensionScriptInstanceDataPtr p_data) {
	return static_cast<ScriptInstance *>(p_data)->owner->_owner;
}

static GDExtensionBool instance_has_method(GDExtensionScriptInstanceDataPtr p_data, GDExtensionConstStringNamePtr p_name) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	return script_method(instance->object->klass, *reinterpret_cast<const StringName *>(p_name)) != ir::NONE;
}

// The engine calling a script method by name.
static void instance_call(GDExtensionScriptInstanceDataPtr p_data, GDExtensionConstStringNamePtr p_method,
		const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argc, GDExtensionVariantPtr r_return,
		GDExtensionCallError *r_error) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	uint32_t method = script_method(instance->object->klass, *reinterpret_cast<const StringName *>(p_method));
	if (method == ir::NONE) {
		r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return;
	}
	if (!kas3::on_main_thread()) {
		kas3::refuse_thread(method);
		r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return;
	}
	kas3::call_script(instance->object, method, reinterpret_cast<const Variant *const *>(p_args), p_argc,
			reinterpret_cast<Variant *>(r_return), r_error);
}

void kas3::call_script(vm::Object *p_object, uint32_t p_function, const Variant *const *p_args, int64_t p_argc, Variant *r_return,
		GDExtensionCallError *r_error) {
	Project &program = Project::get();
	uint32_t method = p_function;
	const ir::Function &function = program.module().functions[method];
	// Register 0 is `this` (or a function expression's environment), unless it takes none; the parameters
	// follow.
	const size_t first = function.is_static ? 0 : 1;
	size_t wanted = function.parameters > first ? function.parameters - first : 0;
	// With a rest parameter, any number more, each untyped.
	const size_t fixed = function.rest ? function.parameters - 1 : function.parameters;
	const auto kind_at = [&](size_t p_at) { return p_at >= fixed ? ir::Kind::Any : function.registers[p_at]; };
	if ((size_t)p_argc > wanted && !function.rest) {
		r_error->error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_error->expected = (int32_t)wanted;
		return;
	}
	size_t needed = function.required > first ? function.required - first : 0;
	if ((size_t)p_argc < needed) {
		r_error->error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error->expected = (int32_t)needed;
		return;
	}
	// An Array parameter takes one of Godot's Arrays, copied, or null; a Dictionary one of Godot's
	// Dictionaries, copied, or null.
	const auto container_parameter = [&](size_t p_at, Variant::Type *r_type) {
		const uint32_t klass = p_at < function.parameter_classes.size() ? function.parameter_classes[p_at] : ir::NONE;
		const bool object = p_at < fixed && function.registers[p_at] == ir::Kind::Obj && klass != ir::NONE;
		*r_type = object && program.module().classes[klass].array ? Variant::ARRAY : Variant::DICTIONARY;
		return object && (program.module().classes[klass].array || program.module().classes[klass].dictionary);
	};
	// Every argument is checked before any is converted, so that a refusal leaves nothing to give back.
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		ir::Kind kind = kind_at(i + first);
		Variant::Type container = Variant::NIL;
		if (container_parameter(i + first, &container) && (p_args[i]->get_type() == container || p_args[i]->get_type() == Variant::NIL)) {
			continue;
		}
		const std::string godot_type = i + first < function.engine_types.size() ? function.engine_types[i + first] : std::string();
		if (!kas3::arrives(kind, godot_type, *p_args[i])) {
			// A whole number too wide for an int or a uint parameter: AS3's RangeError, said as an uncaught
			// AS3 error is, and null for the caller. (Godot would say it can't convert an int to an int.)
			if ((kind == ir::Kind::I32 || kind == ir::Kind::U32) && p_args[i]->get_type() == Variant::INT) {
				const std::string number = std::to_string((int64_t)*p_args[i]);
				UtilityFunctions::push_error(String::utf8(("RangeError: " + number + " does not fit " + (kind == ir::Kind::I32 ? "an int" : "a uint") +
																   ".\n\tat " + function.name + "()")
																  .c_str()));
				r_error->error = GDEXTENSION_CALL_OK;
				return;
			}
			r_error->error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
			r_error->argument = (int32_t)i;
			r_error->expected = (int32_t)expected_type(kind);
			return;
		}
	}
	std::vector<vm::Slot> arguments((size_t)p_argc + first);
	if (first) {
		arguments[0].bits = 0;
		arguments[0].p = p_object;
	}
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		std::optional<vm::Slot> taken;
		Variant::Type container = Variant::NIL;
		if (container_parameter(i + first, &container)) {
			taken = vm::Slot();
			taken->bits = 0;
			if (p_args[i]->get_type() == container) {
				Variant held = *p_args[i];
				const uint32_t klass = function.parameter_classes[i + first];
				taken->p = container == Variant::ARRAY ? program.interpreter().copy_engine_array(&held, klass)
													   : program.interpreter().copy_engine_dictionary(&held, klass);
				taken = taken->p ? taken : std::nullopt;
			}
		} else {
			taken = kas3::from_variant(kind_at(i + first), *p_args[i]);
		}
		if (!taken) {
			// As with any call that fails: the error said, and null for the caller.
			for (size_t made = 0; made < i; made++) {
				program.interpreter().discard(kind_at(made + first), arguments[made + first]);
			}
			// As an uncaught AS3 error reads, at the method Godot called.
			UtilityFunctions::push_error(String("Error: ") + String::utf8(vm::OUT_OF_MEMORY.data(), (int64_t)vm::OUT_OF_MEMORY.size()) +
					String("\n\tat ") + String::utf8(function.name.c_str()) + String("()"));
			r_error->error = GDEXTENSION_CALL_OK;
			return;
		}
		arguments[i + first] = *taken;
	}
	vm::Slot result;
	result.bits = 0;
	bool ok = program.interpreter().call_function(method, arguments.data(), arguments.size(), &result);
	// What was made for the call is given up again.
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		program.interpreter().discard(kind_at(i + first), arguments[i + first]);
	}
	const vm::Object *returned = function.result == ir::Kind::Obj ? static_cast<const vm::Object *>(result.p)
			: function.result == ir::Kind::Any && vm::any::tag(result) == vm::AnyTag::Object ? static_cast<const vm::Object *>(vm::any::pointer(result))
																							  : nullptr;
	Variant owner;
	std::string why;
	if (ok && returned && kas3::object_element(returned, &owner, &why)) {
		*r_return = owner; // A script object that is one of Godot's: its Godot object; an Array: Godot's Array.
	} else if (ok && returned) {
		UtilityFunctions::push_error(String("ActionScript: ") + String::utf8(function.name.c_str()) +
				" returns a script object Godot can't hold (" + String::utf8(why.c_str()) + "). The caller gets null.");
	} else if (ok && function.result != ir::Kind::Void && function.result != ir::Kind::Obj) {
		*r_return = kas3::to_variant(function.result, result);
	}
	if (ok) {
		program.interpreter().discard(function.result, result);
	}
	kas3::hold_if_referenced(p_object);
	r_error->error = GDEXTENSION_CALL_OK;
}

static void instance_notification(GDExtensionScriptInstanceDataPtr, int32_t, GDExtensionBool) {}

// Godot asking how the object reads (`str(node)`, `print(node)`): the script's toString(), if it has one.
// Without one, Godot's own text stands.
static void instance_to_string(GDExtensionScriptInstanceDataPtr p_data, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	Project &program = Project::get();
	*r_is_valid = false;
	uint32_t own = program.module().classes[instance->object->klass].to_string;
	if (own == ir::NONE) {
		return;
	}
	vm::Slot self, result;
	self.bits = 0;
	self.p = instance->object;
	result.bits = 0;
	const bool called = program.interpreter().call_function(own, &self, 1, &result);
	kas3::hold_if_referenced(instance->object);
	if (!called) {
		return; // The error has been reported.
	}
	const vm::Str *text = static_cast<const vm::Str *>(result.p);
	*reinterpret_cast<String *>(r_out) = text ? String::utf8(text->view().data(), (int64_t)text->view().size()) : String("null");
	program.interpreter().discard(ir::Kind::Str, result);
	*r_is_valid = true;
}

static GDExtensionObjectPtr instance_get_script(GDExtensionScriptInstanceDataPtr p_data) {
	return static_cast<ScriptInstance *>(p_data)->script->_owner;
}

static GDExtensionBool instance_is_placeholder(GDExtensionScriptInstanceDataPtr) {
	return false;
}

static GDExtensionScriptLanguagePtr instance_get_language(GDExtensionScriptInstanceDataPtr) {
	return AS3Language::singleton->_owner;
}

// For a script on a reference-counted object, Godot asks before freeing it when its count reaches zero.
// Nothing of the script's holds the object then (counted.cpp), so the answer is always yes.
static void instance_refcount_incremented(GDExtensionScriptInstanceDataPtr) {}

static GDExtensionBool instance_refcount_decremented(GDExtensionScriptInstanceDataPtr) {
	return true;
}

static void instance_free(GDExtensionScriptInstanceDataPtr p_data) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	instances().erase(instance);
	auto on = instances_on().find(instance->owner);
	if (on != instances_on().end() && on->second == instance) {
		instances_on().erase(on);
	}
	if (instance->counted) {
		kas3::counted_freed(instance);
	} else {
		Project::get().interpreter().release_object(instance->object);
	}
	delete instance;
}

// The script object being made by AS3's `new` (GodotHost::attach_new), for the script instance that its
// Godot object's set_script() makes next: that instance takes it rather than making one of its own.
static vm::Object *pending_object = nullptr;

void *create_script_instance(const AS3Script *p_script, Object *p_owner) {
	static GDExtensionScriptInstanceInfo3 info = {};
	static bool ready = false;
	if (!ready) {
		info.set_func = instance_set;
		info.get_func = instance_get;
		info.get_property_list_func = instance_get_property_list;
		info.free_property_list_func = instance_free_property_list;
		info.get_owner_func = instance_get_owner;
		info.get_method_list_func = instance_get_method_list;
		info.free_method_list_func = instance_free_method_list;
		info.has_method_func = instance_has_method;
		info.call_func = instance_call;
		info.notification_func = instance_notification;
		info.to_string_func = instance_to_string;
		info.get_script_func = instance_get_script;
		info.is_placeholder_func = instance_is_placeholder;
		info.get_language_func = instance_get_language;
		info.refcount_incremented_func = instance_refcount_incremented;
		info.refcount_decremented_func = instance_refcount_decremented;
		info.free_func = instance_free;
		ready = true;
	}
	Project &program = Project::get();
	if (!program.valid()) {
		return nullptr;
	}
	const bool counted = program.module().classes[p_script->get_class_index()].counted;
	vm::Object *object = nullptr;
	bool made_here = false;
	if (pending_object && pending_object->klass == p_script->get_class_index()) {
		object = pending_object; // Made by `new`: its constructor runs when the Godot object is ready.
		if (!counted) {
			vm::retain(object);
		}
		pending_object = nullptr;
	} else {
		// The script's handle on the object it is attached to does not keep that object alive.
		object = program.interpreter().construct(p_script->get_class_index(), program.host().borrow(p_owner));
		made_here = true;
	}
	if (!object) {
		return nullptr;
	}
	ScriptInstance *instance = new ScriptInstance();
	instance->object = object;
	instance->owner = p_owner;
	instance->script = Ref<AS3Script>(const_cast<AS3Script *>(p_script));
	instance->counted = counted;
	instances().insert(instance);
	instances_on()[p_owner] = instance;
	if (counted) {
		if (made_here && vm::release(object)) {
			// Its constructor left no other reference: the instance keeps it (counted.cpp).
		}
		kas3::counted_attached(instance);
	}
	return gdextension_interface::script_instance_create3(&info, instance);
}

namespace kas3 {

// `new` of a script class that extends one of Godot's: Godot's object of that class, made as GDScript's
// `Node2D.new()` makes one, with the class's script attached, whose object is p_object.
void *GodotHost::attach_new(uint32_t p_class, vm::Object *p_object, std::string *r_error) {
	Project &program = Project::get();
	const ir::Module &module = program.module();
	const ir::Class &klass = module.classes[p_class];
	const uint32_t file = klass.constructor < module.functions.size() ? module.functions[klass.constructor].file : ir::NONE;
	const Ref<AS3Script> script = file < module.files.size() ? ResourceLoader::get_singleton()->load(String::utf8(module.files[file].c_str())) : Ref<Resource>();
	if (script.is_null() || script->get_class_index() != p_class) {
		*r_error = "the script of '" + klass.name + "' can't be found to attach";
		return nullptr;
	}
	Variant made = ClassDBSingleton::get_singleton()->instantiate(StringName(klass.native_base.c_str()));
	Object *owner = made.get_validated_object();
	if (!owner) {
		*r_error = "Godot can't make a '" + klass.native_base + "' for '" + klass.name + "'";
		return nullptr;
	}
	pending_object = p_object;
	owner->set_script(script);
	const bool attached = pending_object == nullptr;
	pending_object = nullptr;
	if (!attached) {
		if (!Object::cast_to<RefCounted>(owner)) {
			memdelete(owner); // A counted one goes with `made`.
		}
		*r_error = "the script of '" + klass.name + "' could not be attached to its '" + klass.native_base + "'";
		return nullptr;
	}
	return borrow(owner);
}

} // namespace kas3
