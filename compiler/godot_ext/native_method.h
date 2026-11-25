// Conversion at the ptrcall boundary for a typed method in a native program.
#pragma once

#include "native_host.h"
#include "worked.h"

namespace kas3 {

struct NativeMethod {
	const GodotHost::Worked &worked;
	vm::Wanted result;
	bool returns_reference;
};

std::unique_ptr<NativeMethod> prepare_native_method(const ir::EngineName &p_name, const ir::Function &p_function,
		const ir::Instruction &p_in, const GodotHost::Worked &p_worked);
void call_method_bind(const void *p_bind, void *p_object, const void *const *p_arguments, void *r_result);
void call_native_method(rt::Context *c, const rt::Resolved &p_use, rt::Ref p_receiver, void *p_object,
		const void **p_arguments, uint32_t p_handles, vm::Slot *r_result, void *r_parts);

} // namespace kas3
