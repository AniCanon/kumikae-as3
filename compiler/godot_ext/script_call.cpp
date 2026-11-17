// Godot calling a function of a script object (program.h): the arguments checked and converted, the
// result handed back.
#include "any.h"
#include "godot_host.h"
#include "instance.h"
#include "members.h"
#include "program.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <new>
#include <optional>
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

// The most parameters a function has for its arguments to be taken as they come (take_plain).
constexpr size_t MOST_PLAIN = 8;

// What a function's parameters take from Godot with nothing to work out: for each, Godot's type whose
// value is the parameter's own as it comes (an int for a whole number, a float for a Number, a bool, the
// value type the parameter names). Worked out once for a function, for each load of the scripts.
struct Plain {
	enum class Known : uint8_t { No, Yes, Never } known = Known::No;
	uint8_t count = 0;
	ir::Kind kinds[MOST_PLAIN];
	Variant::Type types[MOST_PLAIN];
};

// The values with parts given to one call as they came: copies of Godot's, on the stack, which the function
// reads as handles it was lent. A parameter that is written, or kept, copies what it was lent as it does
// any other (it may change the value in place, which must not be Godot's own); none is a handle to give back.
class Lent {
	alignas(Variant) unsigned char memory_[MOST_PLAIN][sizeof(Variant)];
	size_t made_ = 0;

public:
	Lent() = default;
	Lent(const Lent &) = delete;
	Lent &operator=(const Lent &) = delete;
	~Lent() {
		for (size_t i = 0; i < made_; i++) {
			reinterpret_cast<Variant *>(memory_[i])->~Variant();
		}
	}
	void *copy(const Variant &p_value) { return new (memory_[made_++]) Variant(p_value); }
};

Plain worked_out(const Parameters &p_parameters) {
	const ir::Function &function = p_parameters.function;
	Plain plain;
	plain.known = Plain::Known::Never;
	const size_t count = function.parameters - std::min<size_t>(function.parameters, p_parameters.first);
	if (function.rest || count > MOST_PLAIN) {
		return plain;
	}
	for (size_t i = 0; i < count; i++) {
		const size_t at = i + p_parameters.first;
		const ir::Kind kind = function.registers[at];
		std::optional<int> type;
		if (kind == ir::Kind::I32 || kind == ir::Kind::U32 || kind == ir::Kind::I64) {
			type = Variant::INT;
		} else if (kind == ir::Kind::F64) {
			type = Variant::FLOAT;
		} else if (kind == ir::Kind::Bool) {
			type = Variant::BOOL;
		} else if (kind == ir::Kind::Val && at < function.engine_types.size()) {
			type = kas3::value_type(function.engine_types[at]);
		}
		// A parameter that takes any of Godot's values, or an object, is checked the long way.
		if (!type || *type == Variant::NIL || *type == Variant::OBJECT) {
			return plain;
		}
		plain.kinds[i] = kind;
		plain.types[i] = static_cast<Variant::Type>(*type);
	}
	plain.count = static_cast<uint8_t>(count);
	plain.known = Plain::Known::Yes;
	return plain;
}

// What worked_out() gives for a function, kept for as long as the scripts are the ones loaded.
const Plain &plain_of(const Project &p_program, uint32_t p_function) {
	static std::vector<Plain> kept;
	static uint64_t loaded = 0;
	const std::vector<ir::Function> &functions = p_program.module().functions;
	if (loaded != Project::generation() || kept.size() != functions.size()) {
		kept.assign(functions.size(), Plain());
		loaded = Project::generation();
	}
	Plain &plain = kept[p_function];
	if (plain.known == Plain::Known::No) {
		plain = worked_out(Parameters(functions[p_function]));
	}
	return plain;
}

// The arguments as the function's registers take them, where Godot passes every one and each is exactly of
// the type its parameter holds: nothing is checked twice, converted or allocated. False, with nothing
// taken, for anything else (fewer arguments, a float for an int, null, a number too wide): the long way
// then says what fits and what does not, in its own words.
bool take_plain(const Plain &p_plain, const Variant *const *p_args, int64_t p_argc, vm::Slot *r_arguments, Lent *r_lent) {
	if (p_plain.known != Plain::Known::Yes || p_argc != p_plain.count) {
		return false;
	}
	for (size_t i = 0; i < p_plain.count; i++) {
		if (p_args[i]->get_type() != p_plain.types[i]) {
			return false;
		}
		vm::Slot &slot = r_arguments[i];
		slot.bits = 0;
		switch (p_plain.kinds[i]) {
			case ir::Kind::I32:
			case ir::Kind::U32: {
				const int64_t value = static_cast<int64_t>(*p_args[i]);
				if (p_plain.kinds[i] == ir::Kind::I32 ? value != static_cast<int32_t>(value) : value != static_cast<uint32_t>(value)) {
					return false;
				}
				// The low four bytes, which is where either is read.
				slot.u = static_cast<uint32_t>(value);
				break;
			}
			case ir::Kind::I64:
				slot.l = static_cast<int64_t>(*p_args[i]);
				break;
			case ir::Kind::F64:
				slot.d = static_cast<double>(*p_args[i]);
				break;
			case ir::Kind::Bool:
				slot.b = static_cast<bool>(*p_args[i]);
				break;
			default:
				// Copied only once every argument before it has been found to fit: one that does not fit
				// after it leaves the copy to `r_lent`, and the long way makes its own.
				slot.p = r_lent->copy(*p_args[i]);
				break;
		}
	}
	return true;
}

// What the function returned, as Godot takes it, and given up.
void hand_back(Project &p_program, const ir::Function &p_function, const vm::Slot &p_result, Variant *r_return);

// The call where take_plain() takes the arguments: true, with the call made. Kept short and apart, since
// Godot makes it for every listener a signal has and for every frame.
bool call_plain(Project &p_program, vm::Object *p_object, uint32_t p_function, const Variant *const *p_args, int64_t p_argc, Variant *r_return,
		GDExtensionCallError *r_error) {
	const Plain &plain = plain_of(p_program, p_function);
	const ir::Function &function = p_program.module().functions[p_function];
	const size_t first = function.is_static ? 0 : 1;
	vm::Slot arguments[MOST_PLAIN + 1];
	Lent lent;
	if (!take_plain(plain, p_args, p_argc, arguments + first, &lent)) {
		return false;
	}
	if (first) {
		arguments[0].bits = 0;
		arguments[0].p = p_object;
	}
	const bool counted = !p_object || p_program.module().classes[p_object->klass].counted;
	vm::Slot result;
	result.bits = 0;
	const bool ok = p_program.call_function(p_function, arguments, static_cast<size_t>(p_argc) + first, &result);
	// A function that gives nothing has nothing to hand back or to give up.
	if (ok && function.result != ir::Kind::Void) {
		hand_back(p_program, function, result, r_return);
	}
	if (counted) {
		kas3::hold_if_referenced(p_object);
	}
	r_error->error = GDEXTENSION_CALL_OK;
	return true;
}

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
		vm::Slot *r_arguments, GDExtensionCallError *r_error) {
	const ir::Function &function = p_parameters.function;
	const size_t first = p_parameters.first;
	vm::Slot *const arguments = r_arguments;
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

namespace {

void hand_back(Project &p_program, const ir::Function &p_function, const vm::Slot &p_result, Variant *r_return) {
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
	p_program.interpreter().discard(p_function.result, p_result);
}

} // namespace

void kas3::call_script(vm::Object *p_object, uint32_t p_function, const Variant *const *p_args, int64_t p_argc, Variant *r_return,
		GDExtensionCallError *r_error) {
	Project &program = Project::get();
	if (call_plain(program, p_object, p_function, p_args, p_argc, r_return, r_error)) {
		return;
	}
	uint32_t method = p_function;
	const ir::Function &function = program.module().functions[method];
	const Parameters parameters(function);
	if (!count_fits(parameters, p_argc, r_error) || !arguments_fit(parameters, p_args, p_argc, r_error)) {
		return;
	}
	const size_t first = parameters.first;
	// Nothing is allocated for a call of the usual size: Godot makes one for every listener and every frame.
	const size_t count = (size_t)p_argc + first;
	vm::Slot few[8];
	std::vector<vm::Slot> many;
	vm::Slot *arguments = few;
	if (count > 8) {
		many.resize(count);
		arguments = many.data();
	}
	for (size_t i = 0; i < count; i++) {
		arguments[i].bits = 0;
	}
	if (!convert_arguments(parameters, p_object, p_args, p_argc, arguments, r_error)) {
		return;
	}
	// Only an object of a class whose Godot object may be reference-counted is ever held by Godot's count
	// (asked before the call, which may be the object's end).
	const bool counted = !p_object || program.module().classes[p_object->klass].counted;
	vm::Slot result;
	result.bits = 0;
	bool ok = program.call_function(method, arguments, count, &result);
	// What was made for the call is given up again.
	for (size_t i = 0; i < (size_t)p_argc; i++) {
		program.interpreter().discard(parameters.kind_at(i + first), arguments[i + first]);
	}
	if (ok) {
		hand_back(program, function, result, r_return);
	}
	if (counted) {
		kas3::hold_if_referenced(p_object);
	}
	r_error->error = GDEXTENSION_CALL_OK;
}
