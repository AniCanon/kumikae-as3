// Errors in generated code: raising, catching, the stack trace, and the report of one nothing caught.
// Each follows what the interpreter does (vm/interpreter_errors.cpp), so that both say the same.
#include "host.h"

namespace kas3 {
namespace rt {

using ir::Kind;

namespace {

// "\tat Main/update()[Main.as:12]"
std::string site_line(const Host &p_host, const Site *p_site, uint32_t p_line) {
	const char *file = p_site->file < p_host.program.file_count ? p_host.program.files[p_site->file] : "?";
	return "\tat " + std::string(p_site->function) + "()[" + file + ":" + std::to_string(p_line) + "]";
}

// A function that an error leaves, added to its trace: a runaway recursion has thousands, and the first
// few and a count say as much.
void traced(Host &r_host, const Site *p_site, uint32_t p_line, std::string *r_trace, size_t *r_lines) {
	if (*r_lines < vm::Interpreter::MAX_TRACE) {
		*r_trace += site_line(r_host, p_site, p_line) + "\n";
	}
	(*r_lines)++;
}

// The error on its way out, as its report begins.
std::string error_text(Host &r_host) {
	const Slot error = r_host.context.pending;
	if (vm::any::tag(error) != vm::AnyTag::Object) {
		return vm::any::text(error).value_or(std::string());
	}
	// Put aside while the library is asked for the object's own words.
	r_host.context.failing = false;
	Slot object = empty();
	object.p = vm::any::pointer(error);
	const Slot words = r_host.builtin(Builtin::ObjectText, &object);
	Str *text = static_cast<Str *>(words.p);
	std::string out = text ? std::string(text->view()) : std::string(vm::OUT_OF_MEMORY);
	release(&r_host.context, text);
	if (r_host.context.failing) {
		r_host.interpreter->discard(Kind::Any, r_host.context.pending);
	}
	r_host.context.pending = error;
	r_host.context.failing = true;
	return out;
}

} // namespace

void stack_overflow(Context *c) {
	c->host->builtin(Builtin::Overflow, nullptr);
}

void raise_null(Context *c) {
	const Slot nothing = empty();
	c->host->builtin(Builtin::NullCheck, &nothing);
}

void throw_value(Context *c, Slot p_any) {
	retain(c, p_any);
	c->pending = p_any;
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
	if (host.stopped) {
		return false;
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

Slot caught(Context *c, const Site *p_site, uint32_t p_line, uint32_t p_register, bool p_rethrows, bool *r_traced) {
	Host &host = *c->host;
	if (p_rethrows) {
		KeptTrace kept{ c->depth, p_register, host.trace, host.trace_lines };
		if (!*r_traced) {
			traced(host, p_site, p_line, &kept.trace, &kept.lines);
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

void leave_failed(Context *c, const Site *p_site, uint32_t p_line, bool p_traced) {
	if (!p_traced) {
		traced(*c->host, p_site, p_line, &c->host->trace, &c->host->trace_lines);
	}
}

void forget_kept(Context *c) {
	std::vector<KeptTrace> &kept = c->host->kept;
	while (!kept.empty() && kept.back().depth > c->depth) {
		kept.pop_back();
	}
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
