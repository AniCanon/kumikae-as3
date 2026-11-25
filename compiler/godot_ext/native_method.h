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

// What Godot declares for the parameters a call leaves out, each of the parameter's own type: none if it
// declares none there, or one of another type, an object or text.
std::optional<std::vector<godot::Variant>> declared_defaults(const ir::EngineName &p_entry, const std::vector<int> &p_parameters, size_t p_given);
std::unique_ptr<NativeMethod> prepare_native_method(const ir::EngineName &p_name, const ir::Function &p_function,
		const ir::Instruction &p_in, const GodotHost::Worked &p_worked, std::vector<godot::Variant> p_defaults);
void call_method_bind(const void *p_bind, void *p_object, const void *const *p_arguments, void *r_result);
void call_native_method(rt::Context *c, const rt::Resolved &p_use, rt::Ref p_receiver, void *p_object,
		const void **p_arguments, uint32_t p_handles, vm::Slot *r_result, void *r_parts);

} // namespace kas3
