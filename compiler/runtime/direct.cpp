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

constexpr uint32_t USUAL_PARAMETERS = 16; // A function with more has its values on the heap for the call.

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
	bool failing() const override { return c->failing || c->host->interpreter->left_error(); }

	// The function's entry has a slot for every parameter: those left out are empty and take their
	// defaults there, and the arguments past the others are the rest parameter's Array, made here.
	bool run(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) override {
		const FunctionInfo &callee = c->program->functions[p_function];
		const uint32_t fixed = callee.rest ? callee.parameters - 1 : callee.parameters;
		const uint32_t given = p_count < fixed ? static_cast<uint32_t>(p_count) : fixed;
		Slot few[USUAL_PARAMETERS];
		std::vector<Slot> many;
		Slot *values = few;
		if (callee.parameters > USUAL_PARAMETERS) {
			many.resize(callee.parameters);
			values = many.data();
		}
		for (uint32_t i = 0; i < callee.parameters; i++) {
			values[i] = i < given ? p_arguments[i] : empty();
		}
		Object *rest = nullptr;
		if (callee.rest) {
			rest = new_array(c, p_arguments + given, static_cast<uint32_t>(p_count) - given);
			if (!rest) {
				return false;
			}
			values[fixed].p = rest;
		}
		callee.entry(c, values, given, r_result);
		release(c, rest);
		return !c->failing;
	}

	uint32_t carries() const override { return c->host->interpreter->carries(); }
};

// What a function made goes to whoever asked for it, or is let go of where nobody did.
void hand_back(Context *c, Kind p_kind, Slot p_made, Slot *r_out) {
	if (r_out) {
		*r_out = p_made;
	} else {
		c->host->interpreter->discard(p_kind, p_made);
	}
}

// What is still the interpreter's own of the library (vm::Unmoved) may have left an error there: it is the
// context's from here, as a thunk's catch makes it.
void settle(Context *c, uint32_t p_carries) {
	Host &host = *c->host;
	if (!host.interpreter->left_error()) {
		return;
	}
	const vm::Interpreter::Left left = host.interpreter->take_error(host.interpreter->carries() != p_carries);
	if (left.stopped) {
		host.stopped = true;
		c->pending = empty();
		c->failing = true;
	} else if (c->failing) {
		// What the interpreter raised to leave its own frames, in place of an error that is the context's.
		host.interpreter->discard(Kind::Any, left.value);
	} else {
		Object *object = vm::any::tag(left.value) == AnyTag::Object ? static_cast<Object *>(vm::any::pointer(left.value)) : nullptr;
		if (host.program.keeps_stack && object && is_class(c, object, host.program.known.error)) {
			put(c, field<Str *>(object, ir::ERROR_STACK), vm::new_string(host.stack_text(object, false)));
		}
		c->pending = left.value;
		c->failing = true;
	}
}

// One of the library's functions in progress for generated code, where it may ask the interpreter for a
// part that is still its own.
struct Direct {
	Context *const c;
	const uint32_t carries; // How many errors the interpreter had carried back from the engine's calls.
	explicit Direct(Context *p_context) :
			c(p_context), carries(p_context->host->interpreter->carries()) {
		c->host->direct++;
		c->host->interpreter->direct_began();
	}
	~Direct() {
		c->host->interpreter->direct_ended();
		c->host->direct--;
		settle(c, carries);
	}
	vm::Library library() const { return c->host->library(); }
};

// The vector an entry is handed first. Null, with AS3's TypeError #1009 raised, where there is none.
Vector *vector_in(Context *c, const Slot *p_values) {
	Vector *vector = static_cast<Vector *>(p_values[0].p);
	if (!vector) {
		raise_null(c);
	}
	return vector;
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
			made = text_of(c, p_values[0].i);
			break;
		case Kind::U32:
			made = text_of(c, p_values[0].u);
			break;
		case Kind::I64:
			made = text_of(c, static_cast<int64_t>(p_values[0].l));
			break;
		case Kind::Bool:
			made = text_of(c, p_values[0].b);
			break;
		default:
			made = text_of(c, p_values[0].d);
			break;
	}
	if (made) {
		hand_back(c, Kind::Str, slot(made), r_out);
	} else {
		out_of_memory(c);
	}
}

void text_joined(Context *c, uint32_t, const Slot *p_values, uint32_t, Slot *r_out) {
	if (Str *made = join(c, static_cast<const Str *>(p_values[0].p), static_cast<const Str *>(p_values[1].p))) {
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

void value_to_any(Context *c, uint32_t p_which, const Slot *p_values, uint32_t, Slot *r_out) {
	vm::Library library = c->host->library();
	const Slot made = vm::to_any(library, static_cast<Kind>(p_which), p_values[0]);
	if (!c->failing) {
		*r_out = made;
	}
}

void any_to_value(Context *c, uint32_t p_which, const Slot *p_values, uint32_t, Slot *r_out) {
	const Direct direct(c);
	vm::Library library = direct.library();
	Slot made = empty();
	if (vm::from_any(library, p_values[0], static_cast<Kind>(p_values[1].u), p_which, &made)) {
		*r_out = made;
	}
}

void any_is_type(Context *c, uint32_t p_which, const Slot *p_values, uint32_t, Slot *r_out) {
	vm::Library library = c->host->library();
	r_out->b = vm::any_is(library, p_values[0], p_which);
}

void object_new(Context *c, uint32_t p_which, const Slot *, uint32_t, Slot *r_out) {
	vm::Library library = c->host->library();
	if (Object *made = vm::object_new(library, p_which)) {
		r_out->p = made;
	}
}

void table_new(Context *c, uint32_t p_which, const Slot *p_values, uint32_t, Slot *r_out) {
	vm::Library library = c->host->library();
	if (Object *made = vm::table_new(library, p_which, p_values[0].b)) {
		r_out->p = made;
	}
}

void vector_call(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out) {
	const Direct direct(c);
	vm::Library library = direct.library();
	Slot made = empty();
	if (vm::vector_call(library, static_cast<ir::VectorMethod>(p_which), p_values, p_count, r_out ? &made : nullptr) && r_out) {
		*r_out = made;
	}
}

void vector_get(Context *c, uint32_t p_which, const Slot *p_values, uint32_t, Slot *r_out) {
	if (Vector *vector = vector_in(c, p_values)) {
		const Direct direct(c);
		vm::Library library = direct.library();
		vm::vector_get(library, vector, static_cast<Kind>(p_which), p_values[1], r_out);
	}
}

void vector_set(Context *c, uint32_t p_which, const Slot *p_values, uint32_t, Slot *) {
	if (Vector *vector = vector_in(c, p_values)) {
		const Direct direct(c);
		vm::Library library = direct.library();
		vm::vector_set(library, vector, static_cast<Kind>(p_which), p_values[1], p_values[2]);
	}
}

void vector_length(Context *c, uint32_t, const Slot *p_values, uint32_t, Slot *r_out) {
	if (const Vector *vector = vector_in(c, p_values)) {
		r_out->u = vector->length;
	}
}

void vector_set_length(Context *c, uint32_t, const Slot *p_values, uint32_t, Slot *) {
	if (Vector *vector = vector_in(c, p_values)) {
		const Direct direct(c);
		vm::Library library = direct.library();
		vm::vector_set_length(library, vector, p_values[1].u);
	}
}

void vector_fixed(Context *c, uint32_t, const Slot *p_values, uint32_t, Slot *r_out) {
	if (const Vector *vector = vector_in(c, p_values)) {
		r_out->b = vector->fixed;
	}
}

void vector_set_fixed(Context *c, uint32_t, const Slot *p_values, uint32_t, Slot *) {
	if (Vector *vector = vector_in(c, p_values)) {
		vector->fixed = p_values[1].b;
	}
}

void vector_new(Context *c, uint32_t p_which, const Slot *p_values, uint32_t, Slot *r_out) {
	const Direct direct(c);
	vm::Library library = direct.library();
	if (Vector *made = vm::vector_new(library, p_which, p_values[0].u, p_values[1].b)) {
		r_out->p = made;
	}
}

void array_new(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out) {
	const Direct direct(c);
	vm::Library library = direct.library();
	if (Vector *made = vm::array_new(library, p_which, p_values, p_count)) {
		r_out->p = made;
	}
}

void vector_from(Context *c, uint32_t p_which, const Slot *p_values, uint32_t, Slot *r_out) {
	const Direct direct(c);
	vm::Library library = direct.library();
	Slot made = empty();
	if (vm::vector_from(library, p_which, p_values[0], &made)) {
		*r_out = made;
	}
}

void text_of_any(Context *c, uint32_t, const Slot *p_values, uint32_t, Slot *r_out) {
	Direct direct(c);
	vm::Library library = direct.library();
	std::string words;
	if (vm::any_text(library, p_values[0], &words)) {
		if (Str *made = library.text(words)) {
			hand_back(c, Kind::Str, slot(made), r_out);
		}
	}
}

void text_of_object(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out) {
	const Slot untyped = p_values[0].p ? vm::any::object(static_cast<Object *>(p_values[0].p)) : vm::any::null();
	text_of_any(c, p_which, &untyped, p_count, r_out);
}

void function_call(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out) {
	Direct direct(c);
	vm::Library library = direct.library();
	Object *function = vm::function_in(library, static_cast<Kind>(p_which), p_values[0]);
	Slot made = empty();
	if (function && vm::function_call(library, function, p_values + 1, p_count - 1, &made)) {
		hand_back(c, Kind::Any, made, r_out);
	}
}

} // namespace rt
} // namespace kas3
