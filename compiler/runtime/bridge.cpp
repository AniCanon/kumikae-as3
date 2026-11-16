// The host's side of the Native instruction (vm/native_code.h): a function's body is the generated function,
// and a thunk hands back the error it caught or a register its instruction moved on.
#include "host.h"

namespace kas3 {
namespace rt {

using ir::Kind;

namespace {

// A stub's body: the generated function, with the arguments the stub's frame was given.
bool call_generated(Host &r_host, uint32_t p_function, const Slot *p_registers, uint32_t p_given, Slot *r_result) {
	const FunctionInfo &info = r_host.program.functions[p_function];
	// Copied out of the frame first: what the function calls may move the interpreter's stack.
	Slot few[16];
	std::vector<Slot> many;
	Slot *arguments = few;
	if (info.parameters > 16) {
		many.resize(info.parameters);
		arguments = many.data();
	}
	for (uint32_t i = 0; i < info.parameters; i++) {
		arguments[i] = p_registers[i];
	}
	Slot result = empty();
	if (!r_host.set_up_by_library(p_function)) {
		info.entry(&r_host.context, arguments, p_given, &result);
	}
	if (r_host.context.failing) {
		// The error stays the context's. What the interpreter raises in its place only leaves its frames,
		// as far as the thunk that began this, whose catch finds the context failing and drops it.
		return false;
	}
	if (r_result) {
		*r_result = result;
	} else {
		r_host.interpreter->discard(static_cast<Kind>(info.result), result);
	}
	return true;
}

// A thunk's catch: an error the library raised is the context's now. With the context failing already,
// this is what the interpreter raised in place of the error of a generated function (call_generated).
void take_error(Host &r_host, Slot p_error) {
	if (r_host.context.failing && !r_host.carried) {
		return;
	}
	r_host.carried = false;
	const Slot error = p_error;
	vm::retain(vm::any::counted(error));
	// An Error the library made took its stack from the interpreter's frames, which are the thunk's: where
	// the program reads stacks, it is given the generated functions'.
	Object *object = vm::any::tag(error) == vm::AnyTag::Object ? static_cast<Object *>(vm::any::pointer(error)) : nullptr;
	if (r_host.program.keeps_stack && object && is_class(&r_host.context, object, r_host.program.known.error)) {
		Str *&stack = field<Str *>(object, ir::ERROR_STACK);
		put(&r_host.context, stack, vm::new_string(r_host.stack_text(object, false)));
	}
	r_host.context.pending = error;
	r_host.context.failing = true;
}

} // namespace

bool Host::run_native(uint32_t p_function, uint32_t p_what, uint32_t p_register, Slot *p_registers, uint32_t p_given, Slot *r_result) {
	switch (static_cast<NativeWhat>(p_what)) {
		case NativeWhat::Body:
			return p_function < program.function_count && call_generated(*this, p_function, p_registers, p_given, r_result);
		case NativeWhat::Caught:
			take_error(*this, p_registers[p_register]);
			return true;
		case NativeWhat::Updated:
			if (updated) {
				*updated = p_registers[p_register];
			}
			return true;
	}
	return false;
}

} // namespace rt
} // namespace kas3
