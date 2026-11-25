// Conversion at the ptrcall boundary for a typed method in a native program.
#pragma once

#include "native_host.h"
#include "worked.h"

#include <optional>
#include <vector>

namespace kas3 {

struct NativeMethod {
	const GodotHost::Worked &worked;
	vm::Wanted result;
	bool returns_reference;
	size_t given; // The arguments the call passes; Godot's defaults follow for the rest.
	std::vector<godot::Variant> defaults; // Made once when the place is prepared; never resized after.
	const void *default_at[rt::MOST_DEFAULTS] = {};
};

// Whether a signature's parameter types (the program's, "-" for none) number as many as the running
// Godot's: generated code passes an array with a place for each one the program was built against.
bool prepared_arity(std::string_view p_types, size_t p_running);
// Whether a declared default of the type can be made once and passed to every call: not an object, not
// text (passed converted) and not a value two calls would share and could change (an Array, a Dictionary).
bool keeps_default(int p_type);
// What Godot declares for the parameters a call leaves out, each of the parameter's own type: none if it
// declares none there, one of another type or one keeps_default() refuses, or if the running Godot's
// method has another number of parameters.
std::optional<std::vector<godot::Variant>> declared_defaults(const ir::EngineName &p_entry, const std::vector<int> &p_parameters, size_t p_given);
std::unique_ptr<NativeMethod> prepare_native_method(const ir::EngineName &p_name, const ir::Function &p_function,
		const ir::Instruction &p_in, const GodotHost::Worked &p_worked, std::vector<godot::Variant> p_defaults);
void call_native_method(rt::Context *c, const rt::Resolved &p_use, rt::Ref p_receiver, void *p_object,
		const void **p_arguments, uint32_t p_handles, vm::Slot *r_result, void *r_parts);

} // namespace kas3
