// Godot calling a function of a script object (program.h): the arguments checked and converted, the
// result handed back.
#include "any.h"
#include "godot_host.h"
#include "instance.h"
#include "members.h"
#include "program.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <string>
#include <vector>

using namespace godot;
using kas3::Project;
namespace ir = kas3::ir;
namespace vm = kas3::vm;

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
	// Dictionaries, copied, or null (members.h).
	const auto container_parameter = [&](size_t p_at) {
		const bool object = p_at < fixed && function.registers[p_at] == ir::Kind::Obj && p_at < function.parameter_classes.size();
		return object ? kas3::container_type(function.parameter_classes[p_at]) : Variant::NIL;
	};
	// Every argument is checked before any is converted, so that a refusal leaves nothing to give back.
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		ir::Kind kind = kind_at(i + first);
		const Variant::Type container = container_parameter(i + first);
		if (container != Variant::NIL && (p_args[i]->get_type() == container || p_args[i]->get_type() == Variant::NIL)) {
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
		const std::optional<vm::Slot> taken = container_parameter(i + first) != Variant::NIL
				? kas3::container_from_variant(function.parameter_classes[i + first], *p_args[i])
				: kas3::from_variant(kind_at(i + first), *p_args[i]);
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
