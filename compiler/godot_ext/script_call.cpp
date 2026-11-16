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

namespace {

// A function's parameters, as the arguments Godot passes reach them.
struct Parameters {
	const ir::Function &function;
	// Register 0 is `this` (or a function expression's environment), unless it takes none; the parameters
	// follow.
	const size_t first;
	// With a rest parameter, any number more, each untyped.
	const size_t fixed;

	explicit Parameters(const ir::Function &p_function) :
			function(p_function), first(p_function.is_static ? 0 : 1), fixed(p_function.rest ? p_function.parameters - 1 : p_function.parameters) {}

	ir::Kind kind_at(size_t p_at) const { return p_at >= fixed ? ir::Kind::Any : function.registers[p_at]; }

	// An Array parameter takes one of Godot's Arrays, copied, or null; a Dictionary one of Godot's
	// Dictionaries, copied, or null (members.h).
	Variant::Type container_at(size_t p_at) const {
		const bool object = p_at < fixed && function.registers[p_at] == ir::Kind::Obj && p_at < function.parameter_classes.size();
		return object ? kas3::container_type(function.parameter_classes[p_at]) : Variant::NIL;
	}
};

} // namespace

// Whether Godot passes as many arguments as the function takes; if not, Godot is told how many it takes.
static bool count_fits(const Parameters &p_parameters, int64_t p_argc, GDExtensionCallError *r_error) {
	const ir::Function &function = p_parameters.function;
	size_t wanted = function.parameters > p_parameters.first ? function.parameters - p_parameters.first : 0;
	if ((size_t)p_argc > wanted && !function.rest && !function.takes_extra) {
		r_error->error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_error->expected = (int32_t)wanted;
		return false;
	}
	size_t needed = function.required > p_parameters.first ? function.required - p_parameters.first : 0;
	if ((size_t)p_argc < needed) {
		r_error->error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error->expected = (int32_t)needed;
		return false;
	}
	return true;
}

// Every argument is checked before any is converted, so that a refusal leaves nothing to give back.
static bool arguments_fit(const Parameters &p_parameters, const Variant *const *p_args, int64_t p_argc, GDExtensionCallError *r_error) {
	const ir::Function &function = p_parameters.function;
	const size_t first = p_parameters.first;
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		ir::Kind kind = p_parameters.kind_at(i + first);
		const Variant::Type container = p_parameters.container_at(i + first);
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
				return false;
			}
			r_error->error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
			r_error->argument = (int32_t)i;
			r_error->expected = (int32_t)expected_type(kind);
			return false;
		}
	}
	return true;
}

// The arguments as the function's registers take them, after the object, if it takes one. False when there
// was no memory for one: what was made is given up, and the error said.
static bool convert_arguments(const Parameters &p_parameters, vm::Object *p_object, const Variant *const *p_args, int64_t p_argc,
		std::vector<vm::Slot> *r_arguments, GDExtensionCallError *r_error) {
	const ir::Function &function = p_parameters.function;
	const size_t first = p_parameters.first;
	std::vector<vm::Slot> &arguments = *r_arguments;
	if (first) {
		arguments[0].bits = 0;
		arguments[0].p = p_object;
	}
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		const std::optional<vm::Slot> taken = p_parameters.container_at(i + first) != Variant::NIL
				? kas3::container_from_variant(function.parameter_classes[i + first], *p_args[i])
				: kas3::from_variant(p_parameters.kind_at(i + first), *p_args[i]);
		if (!taken) {
			// As with any call that fails: the error said, and null for the caller.
			for (size_t made = 0; made < i; made++) {
				Project::get().interpreter().discard(p_parameters.kind_at(made + first), arguments[made + first]);
			}
			// As an uncaught AS3 error reads, at the method Godot called.
			UtilityFunctions::push_error(String("Error: ") + String::utf8(vm::OUT_OF_MEMORY.data(), (int64_t)vm::OUT_OF_MEMORY.size()) +
					String("\n\tat ") + String::utf8(function.name.c_str()) + String("()"));
			r_error->error = GDEXTENSION_CALL_OK;
			return false;
		}
		arguments[i + first] = *taken;
	}
	return true;
}

// What the function returned, as Godot takes it, and given up.
static void hand_back(const ir::Function &p_function, const vm::Slot &p_result, Variant *r_return) {
	const vm::Object *returned = p_function.result == ir::Kind::Obj ? static_cast<const vm::Object *>(p_result.p)
			: p_function.result == ir::Kind::Any && vm::any::tag(p_result) == vm::AnyTag::Object ? static_cast<const vm::Object *>(vm::any::pointer(p_result))
																								  : nullptr;
	Variant owner;
	std::string why;
	if (returned && kas3::object_element(returned, &owner, &why)) {
		*r_return = owner; // A script object that is one of Godot's: its Godot object; an Array: Godot's Array.
	} else if (returned) {
		UtilityFunctions::push_error(String("ActionScript: ") + String::utf8(p_function.name.c_str()) +
				" returns a script object Godot can't hold (" + String::utf8(why.c_str()) + "). The caller gets null.");
	} else if (p_function.result != ir::Kind::Void && p_function.result != ir::Kind::Obj) {
		*r_return = kas3::to_variant(p_function.result, p_result);
	}
	Project::get().interpreter().discard(p_function.result, p_result);
}

void kas3::call_script(vm::Object *p_object, uint32_t p_function, const Variant *const *p_args, int64_t p_argc, Variant *r_return,
		GDExtensionCallError *r_error) {
	Project &program = Project::get();
	uint32_t method = p_function;
	const ir::Function &function = program.module().functions[method];
	const Parameters parameters(function);
	if (!count_fits(parameters, p_argc, r_error) || !arguments_fit(parameters, p_args, p_argc, r_error)) {
		return;
	}
	const size_t first = parameters.first;
	std::vector<vm::Slot> arguments((size_t)p_argc + first);
	if (!convert_arguments(parameters, p_object, p_args, p_argc, &arguments, r_error)) {
		return;
	}
	vm::Slot result;
	result.bits = 0;
	bool ok = program.interpreter().call_function(method, arguments.data(), arguments.size(), &result);
	// What was made for the call is given up again.
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		program.interpreter().discard(parameters.kind_at(i + first), arguments[i + first]);
	}
	if (ok) {
		hand_back(function, result, r_return);
	}
	kas3::hold_if_referenced(p_object);
	r_error->error = GDEXTENSION_CALL_OK;
}
