// The engine calling a function of the program, as generated code: what vm::Interpreter::call() does for
// one of its own, with no frame. Where the interpreter has something in progress (a thunk, its library
// calling back), the call is the interpreter's as before, which runs the function through its stub.
#include "native_host.h"

#include "stack.h"

namespace kas3 {
namespace rt {

using ir::Kind;

namespace {

// The error on its way out of generated code, taken from the context with its trace.
Host::Waiting taken(Host &r_host) {
	r_host.set_aside();
	Host::Waiting out = std::move(r_host.waiting.back());
	r_host.waiting.pop_back();
	return out;
}

// An error nothing took, said as the interpreter says one of its own.
void report_outside(Host &r_host, Host::Waiting p_error) {
	if (r_host.stopped) {
		r_host.interpreter->discard(Kind::Any, p_error.error); // The library has said why it could not go on.
		return;
	}
	vm::NativeError error;
	error.error = p_error.error;
	error.trace = std::move(p_error.trace);
	error.lines = p_error.lines;
	if (p_error.thrown_function != NONE) {
		const ir::Function &function = r_host.module.functions[p_error.thrown_function];
		error.function = function.name + "()";
		error.file = function.file < r_host.module.files.size() ? r_host.module.files[function.file] : "?";
		error.line = static_cast<int32_t>(p_error.thrown_line);
	}
	r_host.interpreter->report_native(std::move(error));
}

} // namespace

bool Host::enter(uint32_t p_function, const Slot *p_arguments, uint32_t p_count, Slot *r_result) {
	const bool asked = direct > 0 && direct == asking;
	if (p_function >= program.function_count || program.functions[p_function].rest || nesting > 0 || (direct > 0 && !asked) || stopped ||
			carried || !interpreter->idle(direct)) {
		return interpreter->call_function(p_function, p_arguments, p_count, r_result);
	}
	const FunctionInfo &info = program.functions[p_function];
	// A slot for every parameter: those the caller left out are empty, and take their defaults.
	Slot few[16];
	std::vector<Slot> many;
	Slot *arguments = few;
	if (info.parameters > 16) {
		many.resize(info.parameters);
		arguments = many.data();
	}
	const uint32_t given = std::min(p_count, info.parameters);
	for (uint32_t i = 0; i < info.parameters; i++) {
		arguments[i] = i < given ? p_arguments[i] : empty();
	}
	// Called with an error on its way out already (what an object's end runs, as generated code leaves by
	// an error): the function runs as any other, and that error is on its way again afterwards.
	const bool outer = context.failing;
	if (outer) {
		set_aside();
	}
	const uint32_t before = context.depth;
	Slot result = empty();
	// The function looks at the stack itself (stack_low): calls from outside nest as deep as it allows.
	if (!context.stack_floor) {
		context.stack_floor = vm::stack_floor(); // Once: the context is one thread's, whose stack does not move.
	}
	// A method's object is kept for as long as the method runs: what it does may be the end of every
	// other holder (its Godot object freed), and it borrows what it is given.
	Object *const self = !info.is_static && given > 0 && static_cast<Kind>(info.kinds[0]) == Kind::Obj ? static_cast<Object *>(arguments[0].p) : nullptr;
	retain(&context, self);
	interpreter->entered();
	// Only a class's static initializer is ever one the library sets up itself.
	const bool initializer = info.owner != NONE && program.classes[info.owner].static_init == p_function;
	if (!initializer || !set_up_by_library(p_function)) {
		info.entry(&context, arguments, given, &result);
	}
	interpreter->left();
	release(&context, self);
	bool ok = !context.failing;
	// A listener's error kept for a call this function made, which did not ask for it: said here, where
	// the function ends, as the interpreter says one it carried and nothing took.
	const bool unasked = held.active && held.depth > before;
	if (!ok) {
		// Generated code is running a call of the engine's beneath this one: the error goes back to it, as
		// Flash has a listener's error come out of the dispatch that ran it. Only the first.
		if (before > 0 && !outer && !(held.active && !unasked) && !stopped) {
			Waiting own = taken(*this);
			if (unasked) {
				report_outside(*this, std::move(held.error));
			}
			held.error = std::move(own);
			held.depth = before;
			held.active = true;
			context.listener_failed = true;
		} else {
			report_outside(*this, taken(*this));
			if (unasked) {
				held.active = false;
				report_outside(*this, std::move(held.error));
			}
		}
	} else if (unasked) {
		// The function failed with it, as it does where the interpreter raises what it carried.
		interpreter->discard(static_cast<Kind>(info.result), result);
		held.active = false;
		report_outside(*this, std::move(held.error));
		ok = false;
	}
	if (outer) {
		restore();
	}
	if (!ok) {
		return false;
	}
	if (r_result) {
		*r_result = result;
	} else {
		interpreter->discard(static_cast<Kind>(info.result), result);
	}
	return true;
}

} // namespace rt
} // namespace kas3
