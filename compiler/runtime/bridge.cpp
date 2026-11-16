// The calls the host's interpreter makes back: a stub's body runs the generated function, a thunk's catch
// hands the error to the context. Everything else an engine would answer is refused, as vm::NoEngine does.
#include "host.h"

namespace kas3 {
namespace rt {

using ir::Kind;

namespace {

bool refuse(std::string *r_error) {
	*r_error = "there is no engine here";
	return false;
}

// A register of the frame that made the call, by its local's name: as it is held, with no new reference.
Slot local(vm::Interpreter &p_interpreter, std::string_view p_name) {
	Slot value = empty();
	Kind kind = Kind::Void;
	const bool found = p_interpreter.local_at(p_interpreter.depth() - 1, p_name, &value, &kind);
	(void)found; // The stubs declare every local they are asked for (runtime/stub_module.cpp).
	return value;
}

// A stub's body: the generated function, with the arguments the stub's frame was given.
bool call_generated(Host &r_host, Slot *r_result, std::string *r_error) {
	vm::Interpreter &interpreter = *r_host.interpreter;
	const ir::Function &stub = interpreter.function_at(interpreter.depth() - 1);
	const size_t index = static_cast<size_t>(&stub - r_host.module.functions.data());
	const FunctionInfo &info = r_host.program.functions[index];
	Slot few[16];
	std::vector<Slot> many;
	Slot *arguments = few;
	if (info.parameters > 16) {
		many.resize(info.parameters);
		arguments = many.data();
	}
	const uint32_t fixed = info.rest ? info.parameters - 1 : info.parameters;
	uint32_t given = fixed;
	for (uint32_t i = 0; i < info.parameters; i++) {
		arguments[i] = local(interpreter, parameter_local(i));
		if (i >= info.required && i < fixed && given == fixed && local(interpreter, missing_local(i)).b) {
			given = i;
		}
	}
	Slot result = empty();
	info.entry(&r_host.context, arguments, given, &result);
	if (r_host.context.failing) {
		// The error stays the context's. What the interpreter raises in its place only leaves its frames,
		// as far as the thunk that began this, whose catch finds the context failing and drops it.
		*r_error = "an error is on its way out of generated code";
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
void take_error(Host &r_host) {
	if (r_host.context.failing) {
		return;
	}
	const Slot error = local(*r_host.interpreter, LOCAL_ERROR);
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

void take_updated(Host &r_host) {
	if (r_host.updated) {
		*r_host.updated = local(*r_host.interpreter, LOCAL_UPDATED);
	}
}

} // namespace

bool Bridge::call_static(std::string_view p_class, std::string_view p_method, const vm::Argument *, size_t, const vm::Wanted &, Slot *r_result,
		std::string *r_error) {
	if (p_class != BRIDGE) {
		return refuse(r_error);
	}
	if (p_method == BRIDGE_CALL) {
		return call_generated(host_, r_result, r_error);
	}
	if (p_method == BRIDGE_CAUGHT) {
		take_error(host_);
	} else if (p_method == BRIDGE_UPDATED) {
		take_updated(host_);
	}
	return true;
}

void *Bridge::construct(std::string_view, const vm::Argument *, size_t, std::string *r_error) {
	refuse(r_error);
	return nullptr;
}
void *Bridge::attach_new(uint32_t, Object *, std::string *r_error) {
	refuse(r_error);
	return nullptr;
}
void *Bridge::collection(std::string_view, ir::Kind, const Slot *, size_t, std::string *r_error) {
	refuse(r_error);
	return nullptr;
}
bool Bridge::constant(std::string_view, std::string_view, const vm::Wanted &, Slot *, std::string *r_error) {
	return refuse(r_error);
}
bool Bridge::evaluate(std::string_view, const vm::Wanted &, Slot *, std::string *r_error) {
	return refuse(r_error);
}
void *Bridge::singleton(std::string_view, std::string *r_error) {
	refuse(r_error);
	return nullptr;
}
void *Bridge::bind(Object *, uint32_t, void *, std::string *r_error) {
	refuse(r_error);
	return nullptr;
}
void *Bridge::copy(void *p_value) {
	return p_value;
}
void Bridge::drop(void *) {}
bool Bridge::present(void *p_value) {
	return p_value != nullptr;
}
bool Bridge::is_class(void *, std::string_view) {
	return false;
}
Str *Bridge::text(void *) {
	return nullptr;
}
bool Bridge::keep(Object *) {
	return false;
}
bool Bridge::call(void *, std::string_view, const vm::Argument *, size_t, const vm::Wanted &, Slot *, std::string *r_error) {
	return refuse(r_error);
}
bool Bridge::get(void *, std::string_view, const vm::Wanted &, Slot *, std::string *r_error) {
	return refuse(r_error);
}
bool Bridge::set(void *, std::string_view, const vm::Argument &, std::string *r_error) {
	return refuse(r_error);
}
bool Bridge::member(void *, std::string_view, const vm::Wanted &, Slot *, std::string *r_error) {
	return refuse(r_error);
}
bool Bridge::index_get(void *, const vm::Argument &, const vm::Wanted &, Slot *, std::string *r_error) {
	return refuse(r_error);
}
bool Bridge::index_set(void *, const vm::Argument &, const vm::Argument &, std::string *r_error) {
	return refuse(r_error);
}
bool Bridge::operate(std::string_view, const vm::Argument *, size_t, const vm::Wanted &, Slot *, std::string *r_error) {
	return refuse(r_error);
}
uint64_t Bridge::node(void *) {
	return 0;
}
bool Bridge::node_edges(uint64_t, vm::EngineNode *) {
	return false;
}
std::vector<uint64_t> Bridge::let_go(const std::vector<uint64_t> &) {
	return {};
}

} // namespace rt
} // namespace kas3
