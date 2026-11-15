// The object Godot holds for each object a script is attached to: the engine's calls arrive here and go
// on into the interpreter.
#include "any.h"
#include "language.h"
#include "program.h"
#include "script.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace godot;
using kas3::Project;
namespace ir = kas3::ir;
namespace vm = kas3::vm;

struct ScriptInstance {
	vm::Object *object;
	Object *owner;
	Ref<AS3Script> script; // Kept alive for as long as anything runs it.
};

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
		instance->object = p_reload.object(old);
		p_old.release_object(old);
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

static GDExtensionBool instance_set(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr, GDExtensionConstVariantPtr) {
	return false;
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

// A field of the script, by its AS3 name.
static GDExtensionBool instance_get(GDExtensionScriptInstanceDataPtr p_data, GDExtensionConstStringNamePtr p_name,
		GDExtensionVariantPtr r_value) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	CharString name = String(*reinterpret_cast<const StringName *>(p_name)).utf8();
	vm::Slot value;
	ir::Kind kind = ir::Kind::Void;
	if (!Project::get().interpreter().get_field(instance->object, std::string_view(name.get_data(), (size_t)name.length()), &value, &kind) ||
			name.get_data()[0] == '$') {
		return false;
	}
	const vm::Object *held = kind == ir::Kind::Obj ? static_cast<const vm::Object *>(value.p)
			: kind == ir::Kind::Any && vm::any::tag(value) == vm::AnyTag::Object ? static_cast<const vm::Object *>(vm::any::pointer(value))
																					 : nullptr;
	if (held || (kind == ir::Kind::Obj && !value.p)) {
		Variant owner;
		std::string why;
		if (held && !kas3::object_element(held, &owner, &why)) {
			UtilityFunctions::push_error(String("ActionScript: the field '") + String::utf8(name.get_data()) +
					"' holds a script object of a class that extends none of Godot's, which Godot can't hold. Godot gets null.");
		}
		*reinterpret_cast<Variant *>(r_value) = owner;
		return true;
	}
	*reinterpret_cast<Variant *>(r_value) = kas3::to_variant(kind, value);
	return true;
}

static const GDExtensionPropertyInfo *instance_get_property_list(GDExtensionScriptInstanceDataPtr, uint32_t *r_count) {
	*r_count = 0;
	return nullptr;
}

static void instance_free_property_list(GDExtensionScriptInstanceDataPtr, const GDExtensionPropertyInfo *, uint32_t) {}

static const GDExtensionMethodInfo *instance_get_method_list(GDExtensionScriptInstanceDataPtr, uint32_t *r_count) {
	*r_count = 0;
	return nullptr;
}

static void instance_free_method_list(GDExtensionScriptInstanceDataPtr, const GDExtensionMethodInfo *, uint32_t) {}

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
				" returns a script object of a class that extends none of Godot's, which Godot can't hold. The caller gets null.");
	} else if (ok && function.result != ir::Kind::Void && function.result != ir::Kind::Obj) {
		*r_return = kas3::to_variant(function.result, result);
	}
	if (ok) {
		program.interpreter().discard(function.result, result);
	}
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
	if (!program.interpreter().call_function(own, &self, 1, &result)) {
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
// The script holds no reference of its own to the object, so the answer is always yes.
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
	Project::get().interpreter().release_object(instance->object);
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
	vm::Object *object = nullptr;
	if (pending_object && pending_object->klass == p_script->get_class_index()) {
		object = pending_object; // Made by `new`: its constructor runs when the Godot object is ready.
		vm::retain(object);
		pending_object = nullptr;
	} else {
		// The script's handle on the object it is attached to does not keep that object alive.
		object = program.interpreter().construct(p_script->get_class_index(), program.host().borrow(p_owner));
	}
	if (!object) {
		return nullptr;
	}
	ScriptInstance *instance = new ScriptInstance();
	instance->object = object;
	instance->owner = p_owner;
	instance->script = Ref<AS3Script>(const_cast<AS3Script *>(p_script));
	instances().insert(instance);
	instances_on()[p_owner] = instance;
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
		memdelete(owner);
		*r_error = "the script of '" + klass.name + "' could not be attached to its '" + klass.native_base + "'";
		return nullptr;
	}
	return borrow(owner);
}

} // namespace kas3
