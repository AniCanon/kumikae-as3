// The object Godot holds for each object a script is attached to: the engine's calls arrive here and go
// on into the interpreter.
#include "any.h"
#include "language.h"
#include "program.h"
#include "script.h"

#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

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

// A field of the script, by its AS3 name.
static GDExtensionBool instance_get(GDExtensionScriptInstanceDataPtr p_data, GDExtensionConstStringNamePtr p_name,
		GDExtensionVariantPtr r_value) {
	ScriptInstance *instance = static_cast<ScriptInstance *>(p_data);
	CharString name = String(*reinterpret_cast<const StringName *>(p_name)).utf8();
	vm::Slot value;
	ir::Kind kind = ir::Kind::Void;
	if (!Project::get().interpreter().get_field(instance->object, std::string_view(name.get_data(), (size_t)name.length()), &value, &kind) ||
			kind == ir::Kind::Obj || name.get_data()[0] == '$') {
		return false;
	}
	if (kind == ir::Kind::Any && vm::any::tag(value) == vm::AnyTag::Object) {
		UtilityFunctions::push_error(String("ActionScript: the field '") + String::utf8(name.get_data()) +
				"' holds a script object, which can't be handed to Godot yet. Godot gets null.");
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
	// Register 0 is `this`; the parameters follow.
	size_t wanted = function.parameters > 0 ? function.parameters - 1 : 0;
	if ((size_t)p_argc > wanted) {
		r_error->error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_error->expected = (int32_t)wanted;
		return;
	}
	size_t needed = function.required > 0 ? function.required - 1 : 0;
	if ((size_t)p_argc < needed) {
		r_error->error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error->expected = (int32_t)needed;
		return;
	}
	// Every argument is checked before any is converted, so that a refusal leaves nothing to give back.
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		ir::Kind kind = function.registers[i + 1];
		const std::string godot_type = i + 1 < function.engine_types.size() ? function.engine_types[i + 1] : std::string();
		if (!kas3::arrives(kind, godot_type, *p_args[i])) {
			r_error->error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
			r_error->argument = (int32_t)i;
			r_error->expected = (int32_t)expected_type(kind);
			return;
		}
	}
	std::vector<vm::Slot> arguments((size_t)p_argc + 1);
	arguments[0].bits = 0;
	arguments[0].p = p_object;
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		const std::optional<vm::Slot> taken = kas3::from_variant(function.registers[i + 1], *p_args[i]);
		if (!taken) {
			// As with any call that fails: the error said, and null for the caller.
			for (size_t made = 0; made < i; made++) {
				program.interpreter().discard(function.registers[made + 1], arguments[made + 1]);
			}
			// As an uncaught AS3 error reads, at the method Godot called.
			UtilityFunctions::push_error(String("Error: ") + String::utf8(vm::OUT_OF_MEMORY.data(), (int64_t)vm::OUT_OF_MEMORY.size()) +
					String("\n\tat ") + String::utf8(function.name.c_str()) + String("()"));
			r_error->error = GDEXTENSION_CALL_OK;
			return;
		}
		arguments[i + 1] = *taken;
	}
	vm::Slot result;
	result.bits = 0;
	bool ok = program.interpreter().call_function(method, arguments.data(), arguments.size(), &result);
	// What was made for the call is given up again.
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		program.interpreter().discard(function.registers[i + 1], arguments[i + 1]);
	}
	const bool script_object = (function.result == ir::Kind::Obj && result.p) ||
			(function.result == ir::Kind::Any && vm::any::tag(result) == vm::AnyTag::Object);
	if (ok && script_object) {
		UtilityFunctions::push_error(String("ActionScript: ") + String::utf8(function.name.c_str()) +
				" returns a script object, which can't be handed to Godot yet. The caller gets null.");
	} else if (ok && function.result != ir::Kind::Void) {
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
	Project::get().interpreter().release_object(instance->object);
	delete instance;
}

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
	// The script's handle on the object it is attached to does not keep that object alive.
	vm::Object *object = program.interpreter().construct(p_script->get_class_index(), program.host().borrow(p_owner));
	if (!object) {
		return nullptr;
	}
	ScriptInstance *instance = new ScriptInstance();
	instance->object = object;
	instance->owner = p_owner;
	instance->script = Ref<AS3Script>(const_cast<AS3Script *>(p_script));
	instances().insert(instance);
	return gdextension_interface::script_instance_create3(&info, instance);
}
