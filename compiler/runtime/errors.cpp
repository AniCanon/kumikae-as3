// Errors in generated code: raising, catching, the stack trace, and the report of one nothing caught.
// Each follows what the interpreter does (vm/interpreter_errors.cpp), so that both say the same.
#include "native_host.h"

#include "stack.h"

namespace kas3 {
namespace rt {

using ir::Kind;

namespace {

// "\tat Main/update()[Main.as:12]"
std::string site_line(const Host &p_host, uint32_t p_function, uint32_t p_line) {
	// The module's names for the files: the embedder's, where it compiled the program itself.
	const ir::Function &function = p_host.module.functions[p_function];
	const std::string file = function.file < p_host.module.files.size() ? p_host.module.files[function.file] : "?";
	return "\tat " + function.name + "()[" + file + ":" + std::to_string(p_line) + "]";
}

// A function that an error leaves, added to its trace: a runaway recursion has thousands, and the first
// few and a count say as much.
void traced(Host &r_host, uint32_t p_function, uint32_t p_line, std::string *r_trace, size_t *r_lines) {
	if (*r_lines < vm::Interpreter::MAX_TRACE) {
		*r_trace += site_line(r_host, p_function, p_line) + "\n";
	}
	(*r_lines)++;
}

// An object in the library's own words for it (an Error's name and message), whatever its class says.
std::string object_words(Host &r_host, Object *p_object) {
	const Slot object = slot(p_object);
	const Slot words = r_host.builtin(Builtin::ObjectText, &object);
	Str *text = static_cast<Str *>(words.p);
	std::string out = text ? std::string(text->view()) : std::string(vm::OUT_OF_MEMORY);
	release(&r_host.context, text);
	return out;
}

// The error on its way out, as its report begins.
std::string error_text(Host &r_host) {
	const Slot error = r_host.context.pending;
	if (vm::any::tag(error) != vm::AnyTag::Object) {
		return vm::any::text(error).value_or(std::string());
	}
	// Put aside while the library is asked for the object's words.
	r_host.context.failing = false;
	std::string out = object_words(r_host, static_cast<Object *>(vm::any::pointer(error)));
	if (r_host.context.failing) {
		r_host.interpreter->discard(Kind::Any, r_host.context.pending);
	}
	r_host.context.pending = error;
	r_host.context.failing = true;
	return out;
}

// Whether the function is the constructor of Error or of a class that extends it.
bool error_constructor(const Host &p_host, uint32_t p_function) {
	const uint32_t owner = p_host.program.functions[p_function].owner;
	if (owner == NONE || p_host.program.classes[owner].constructor != p_function) {
		return false;
	}
	for (uint32_t at = owner; at != NONE; at = p_host.program.classes[at].base) {
		if (at == p_host.program.known.error) {
			return true;
		}
	}
	return false;
}

} // namespace

bool overdue(Context *c) {
	Host &host = *c->host;
	return host.limit.look([&](uint32_t p_limit) { host.asked->raise(host.module.error_class, vm::timeout_message(p_limit)); });
}

bool stack_overflow(Context *c) {
	// Asked by the watch, and not by the depth or the stack themselves: the call goes on if it has time left.
	if (c->host->limit.asked()) {
		if (overdue(c)) {
			return true;
		}
		if (c->depth < MAX_DEPTH && !vm::stack_below(c->stack_floor.load(std::memory_order_relaxed))) {
			return false;
		}
	}
	c->host->library().raise_overflow();
	return true;
}

void raise_null(Context *c) {
	const Slot nothing = empty();
	c->host->builtin(Builtin::NullCheck, &nothing);
}

bool call_failed(Context *c) {
	c->listener_failed = false;
	Host &host = *c->host;
	if (host.held.active) {
		// Kept by Host::enter() for this call: this function's now, with the trace it had.
		host.held.active = false;
		host.waiting.push_back(std::move(host.held.error));
		host.restore();
		return true;
	}
	if (c->failing) {
		return true; // It never left the context: the interpreter was not given it.
	}
	vm::NativeError carried;
	if (!host.interpreter->take_carried(&carried)) {
		return false; // It was reported where it happened, or a thunk has given it out already.
	}
	// The error is this function's now, with the trace it had: the reverse of Host::hand_over().
	c->pending = carried.error;
	c->failing = true;
	host.trace = std::move(carried.trace);
	host.trace_lines = carried.lines;
	host.thrown_function = NONE;
	for (uint32_t i = 0; i < host.program.function_count && !carried.function.empty(); i++) {
		if (host.module.functions[i].name + "()" == carried.function) {
			host.thrown_function = i;
			host.thrown_line = static_cast<uint32_t>(carried.line);
			break;
		}
	}
	return true;
}

void out_of_memory(Context *c) {
	// The library raises it, and words it: asked for an object's text with no memory to write it in.
	vm::fail_allocations(1);
	const Slot nothing = empty();
	c->host->builtin(Builtin::ObjectText, &nothing);
}

void throw_value(Context *c, Slot p_any) {
	c->pending = shared(c, p_any);
	c->failing = true;
}

void rethrow(Context *c, Slot p_any, uint32_t p_register, bool *r_traced) {
	Host &host = *c->host;
	throw_value(c, p_any);
	for (size_t i = host.kept.size(); i-- > 0 && host.kept[i].depth == c->depth;) {
		if (host.kept[i].reg == p_register) {
			host.trace = std::move(host.kept[i].trace);
			host.trace_lines = host.kept[i].lines;
			host.kept.erase(host.kept.begin() + static_cast<std::ptrdiff_t>(i));
			*r_traced = true; // The function's line, where the error was thrown, is in.
			return;
		}
	}
}

bool catches(Context *c, uint32_t p_type) {
	Host &host = *c->host;
	if (host.stopped || host.carried || host.limit.up) {
		return false; // Nothing generated code holds yet (see thunk()), or Error #1502, which nothing catches.
	}
	if (p_type == NONE) {
		return true;
	}
	const Slot error = c->pending;
	if (p_type < host.program.class_count) {
		return vm::any::tag(error) == vm::AnyTag::Object && is_class(c, static_cast<const Object *>(vm::any::pointer(error)), p_type);
	}
	// One of the ANY_IS_ codes, which follow one another as the host's thunks for them do.
	const uint32_t which = static_cast<uint32_t>(Builtin::IsInt) + (p_type - ir::ANY_IS_INT);
	c->failing = false;
	const bool is = host.builtin(static_cast<Builtin>(which), &error).b;
	c->pending = error;
	c->failing = true;
	return is;
}

Slot caught(Context *c, uint32_t p_function, uint32_t p_line, uint32_t p_register, bool p_rethrows, bool *r_traced) {
	Host &host = *c->host;
	if (p_rethrows) {
		KeptTrace kept{ c->depth, p_register, host.trace, host.trace_lines };
		if (!*r_traced) {
			traced(host, p_function, p_line, &kept.trace, &kept.lines);
		}
		// A finally that catches again replaces what it kept: one left behind is never taken by another.
		bool replaced = false;
		for (size_t i = host.kept.size(); i-- > 0 && host.kept[i].depth == kept.depth && !replaced;) {
			if (host.kept[i].reg == kept.reg) {
				host.kept[i] = kept;
				replaced = true;
			}
		}
		if (!replaced) {
			host.kept.push_back(std::move(kept));
		}
	}
	*r_traced = false;
	const Slot error = c->pending;
	c->pending = empty();
	c->failing = false;
	host.trace.clear();
	host.trace_lines = 0;
	return error;
}

void leave_failed(Context *c, uint32_t p_function, uint32_t p_line, bool p_traced) {
	if (!p_traced && c->host->trace_lines == 0) {
		c->host->thrown_function = p_function;
		c->host->thrown_line = p_line;
	}
	if (!p_traced) {
		traced(*c->host, p_function, p_line, &c->host->trace, &c->host->trace_lines);
	}
}

void forget_kept(Context *c) {
	std::vector<KeptTrace> &kept = c->host->kept;
	while (!kept.empty() && kept.back().depth > c->depth) {
		kept.pop_back();
	}
}

std::string Host::stack_text(Object *p_error, bool p_skip_constructors) {
	std::string text = object_words(*this, p_error);
	const StackFrame *frame = context.frames;
	while (p_skip_constructors && frame && error_constructor(*this, frame->function)) {
		frame = frame->below;
	}
	for (size_t lines = 0; frame && lines < vm::Interpreter::MAX_TRACE; frame = frame->below, lines++) {
		text += "\n" + site_line(*this, frame->function, frame->line);
	}
	return text;
}

Str *error_stack(Context *c, Object *p_error) {
	return vm::new_string(c->host->stack_text(p_error, true));
}

void Host::report() {
	if (!stopped) {
		if (trace_lines > vm::Interpreter::MAX_TRACE) {
			trace += "\t... " + std::to_string(trace_lines - vm::Interpreter::MAX_TRACE) + " more\n";
		}
		std::string text = error_text(*this) + "\n" + trace;
		while (!text.empty() && text.back() == '\n') {
			text.pop_back();
		}
		output.error(text);
	}
	interpreter->discard(Kind::Any, context.pending);
	context.pending = empty();
	context.failing = false;
	trace.clear();
	trace_lines = 0;
}

} // namespace rt
} // namespace kas3
