// The library's functions of plain values (vm/library.h) called by generated code with no interpreter
// between: what they ask of whoever runs the program is answered here for native code, and each entry the
// generator names (ir/library_entries.h) hands its values straight on.
#include "direct.h"

#include "fast.h"
#include "library.h"
#include "library_entries.h"
#include "native_host.h"

namespace kas3 {
namespace rt {

using ir::Kind;
using vm::AnyTag;

namespace {

// Whoever runs the program, where that is generated code. A function of the program is called as the C++
// it is; an error is the context's from the moment it is raised.
struct Asked final : vm::Runner {
	Context *const c;
	explicit Asked(Context *p_context) :
			c(p_context) {}

	void raise(uint32_t p_class, const std::string &p_message) override {
		if (c->failing) {
			return;
		}
		Host &host = *c->host;
		Str *message = vm::new_string(p_message);
		Object *error = message ? vm::new_error(host.interpreter->heap(), p_class, message) : nullptr;
		if (!error) {
			rt::out_of_memory(c);
			return;
		}
		// Where the program reads stacks, an Error has the generated functions in progress as its own.
		if (host.program.keeps_stack) {
			field<Str *>(error, ir::ERROR_STACK) = vm::new_string(host.stack_text(error, false));
		}
		c->pending = vm::any::object(error);
		c->failing = true;
	}

	void out_of_memory() override { rt::out_of_memory(c); }

	bool call(Object *p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) override {
		if (call_value(c, p_function, p_arguments, static_cast<uint32_t>(p_count), r_result)) {
			return !c->failing;
		}
		// Anything but the usual (arguments to convert, to leave out or to gather) is the library's call,
		// as apply() makes it, of an Array of them.
		Object *list = new_array(c, p_arguments, static_cast<uint32_t>(p_count));
		if (!list) {
			return false;
		}
		const Slot given[] = { vm::any::object(p_function), vm::any::object(list) };
		*r_result = c->host->builtin(Builtin::Apply, given);
		release(c, list);
		return !c->failing;
	}

	bool text(Slot p_value, std::string *r_text) override {
		if (std::optional<std::string> plain = vm::any::text(p_value)) {
			*r_text = std::move(*plain);
			return true;
		}
		// An object whose class has a toString() of its own is asked as the C++ it is; the rest (a vector,
		// an Object, one of Godot's) are the library's words.
		Str *said = nullptr;
		if (!any_text(c, p_value, &said)) {
			said = static_cast<Str *>(c->host->builtin(Builtin::AnyText, &p_value).p);
		}
		if (c->failing) {
			return false;
		}
		r_text->assign(said ? said->view() : std::string_view("null"));
		release(c, said);
		return true;
	}
};

// What a function made goes to whoever asked for it, or is let go of where nobody did.
void hand_back(Context *c, Kind p_kind, Slot p_made, Slot *r_out) {
	if (r_out) {
		*r_out = p_made;
	} else {
		c->host->interpreter->discard(p_kind, p_made);
	}
}

bool is_regexp(const Context *c, Slot p_any) {
	const uint32_t regexp = c->program->known.regexp;
	return regexp != NONE && vm::any::tag(p_any) == AnyTag::Object && is_class(c, static_cast<const Object *>(vm::any::pointer(p_any)), regexp);
}

} // namespace

std::unique_ptr<vm::Runner> runner_for(Context *c) {
	return std::make_unique<Asked>(c);
}

void text_call(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out) {
	Host &host = *c->host;
	const ir::StringFunction which = static_cast<ir::StringFunction>(p_which);
	const Kind gives = which == ir::StringFunction::Split ? Kind::Obj : Kind::Str;
	if (which == ir::StringFunction::Split && is_regexp(c, p_values[1])) {
		// RegExp's methods are the interpreter's still: this one case goes by the host's own thunk.
		const Slot made = host.builtin(Builtin::SplitByPattern, p_values);
		if (!c->failing) {
			hand_back(c, gives, made, r_out);
		}
		return;
	}
	vm::Library library = host.library();
	Slot made = empty();
	if (vm::text_call(library, which, p_values, p_count, &made)) {
		hand_back(c, gives, made, r_out);
	}
}

void text_of_value(Context *c, uint32_t p_which, const Slot *p_values, uint32_t, Slot *r_out) {
	Str *made = nullptr;
	switch (static_cast<Kind>(p_which)) {
		case Kind::I32:
			made = text_of(p_values[0].i);
			break;
		case Kind::U32:
			made = text_of(p_values[0].u);
			break;
		case Kind::I64:
			made = text_of(static_cast<int64_t>(p_values[0].l));
			break;
		case Kind::Bool:
			made = text_of(p_values[0].b);
			break;
		default:
			made = text_of(p_values[0].d);
			break;
	}
	if (made) {
		hand_back(c, Kind::Str, slot(made), r_out);
	} else {
		out_of_memory(c);
	}
}

void text_joined(Context *c, uint32_t, const Slot *p_values, uint32_t, Slot *r_out) {
	if (Str *made = join(static_cast<const Str *>(p_values[0].p), static_cast<const Str *>(p_values[1].p))) {
		hand_back(c, Kind::Str, slot(made), r_out);
	} else {
		out_of_memory(c);
	}
}

void number_text(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out) {
	vm::Library library = c->host->library();
	const ir::NumberFormat format = static_cast<ir::NumberFormat>(p_which & (ir::NUMBER_TEXT_WIDE - 1));
	Slot made = empty();
	if (vm::number_text(library, format, (p_which & ir::NUMBER_TEXT_WIDE) != 0, p_values, p_count, &made)) {
		hand_back(c, Kind::Str, made, r_out);
	}
}

} // namespace rt
} // namespace kas3
