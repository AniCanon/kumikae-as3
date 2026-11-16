#include "native_host.h"

#include "fast.h"
#include "held.h"


namespace kas3 {
namespace rt {

using ir::Kind;

static_assert(MAX_DEPTH == vm::Interpreter::MAX_FRAMES, "generated code and the interpreter overflow at the same depth");
static_assert(NONE == ir::NONE, "the program table and the IR write `none` alike");

Host::Host(const Program &p_program, const Embedder &p_embedder) :
		program(p_program), module(p_embedder.module ? native_module(*p_embedder.module, p_program) : stub_module(p_program)),
		output(p_embedder.output), handles(p_embedder.handles) {
	interpreter = std::make_unique<vm::Interpreter>(module, vm::Host(p_embedder.output, p_embedder.handles, p_embedder.source, p_embedder.clock));
	interpreter->set_native(this);
	globals.resize(program.global_count);
	for (uint32_t i = 0; i < program.global_count; i++) {
		globals[i] = vm::empty_value(static_cast<Kind>(program.globals[i].kind) == Kind::F64, program.globals[i].starts_null);
	}
	for (uint32_t i = 0; i < program.string_count; i++) {
		strings.push_back(vm::new_string(std::string_view(program.strings[i].data, program.strings[i].size)));
	}
	null_text = vm::new_string("null");
	context.null_text = null_text;
	class_used.assign(program.class_count, 0);
	for (const ir::Class &klass : module.classes) {
		made_here.push_back(klass.element == Kind::Void && !klass.table && !klass.is_interface && !vm::may_hold_objects(klass));
	}
	resolved.resize(program.use_count);
	names.assign(program.name_count, nullptr);
	context.resolved = resolved.data();
	context.names = names.data();
	context.globals = globals.data();
	context.strings = strings.data();
	context.class_used = class_used.data();
	context.classes = program.classes;
	context.program = &program;
	context.host = this;
}

void Host::clear_globals() {
	for (uint32_t i = 0; i < program.global_count; i++) {
		const Slot value = globals[i];
		globals[i].bits = 0;
		interpreter->discard(static_cast<Kind>(program.globals[i].kind), value);
	}
}

Host::~Host() {
	Host::clear_globals();
	if (context.failing) {
		interpreter->discard(Kind::Any, context.pending);
	}
	// As the interpreter goes, its collector frees what is left in loops.
	interpreter.reset();
	strings.push_back(null_text);
	for (Str *constant : strings) {
		if (vm::release(constant)) {
			vm::free_string(constant);
		}
	}
}

Object *new_object(Context *c, uint32_t p_class) {
	if (!c->host->made_here[p_class]) {
		return nullptr;
	}
	const ClassInfo &klass = c->classes[p_class];
	Object *object = vm::new_object(p_class, klass.field_count, nullptr);
	for (uint32_t i = 0; object && i < klass.field_count; i++) {
		object->fields()[i] = vm::empty_value(static_cast<Kind>(klass.fields[i].kind) == Kind::F64, klass.fields[i].starts_null);
	}
	return object;
}

void *copy_handle(Context *c, void *p_handle) {
	return c->host->handles.copy(p_handle);
}

void drop_handle(Context *c, void *p_handle) {
	c->host->handles.drop(p_handle);
}

Slot shared_any(Context *c, Slot p_any) {
	const vm::AnyTag tag = vm::any::tag(p_any);
	if (tag == vm::AnyTag::Value) {
		return vm::any::godot_value(copy_handle(c, vm::any::pointer(p_any)));
	}
	if (tag == vm::AnyTag::Godot) {
		return vm::any::godot_object(copy_handle(c, vm::any::pointer(p_any)));
	}
	vm::retain(vm::any::counted(p_any));
	return p_any;
}

void free_object(Context *c, Object *p_object) {
	c->host->interpreter->free_kept(p_object);
}

void release_any_slow(Context *c, Slot p_value) {
	c->host->interpreter->discard(Kind::Any, p_value);
}

void trace(Context *c, Str *const *p_texts, uint32_t p_count) {
	std::string line;
	for (uint32_t i = 0; i < p_count; i++) {
		line += i ? " " : "";
		line += p_texts[i] ? p_texts[i]->view() : std::string_view("null");
	}
	c->host->output.print(line);
}

Slot thunk(Context *c, uint32_t p_thunk, const Slot *p_given, Slot *r_updated) {
	Host &host = *c->host;
	// The interpreter lets entries nest only so deep, and one that is refused has no frame to catch its
	// error in. The last entry is kept for raising Error #1023 here, where generated code can catch it.
	const uint32_t overflow = host.program.thunk_count + static_cast<uint32_t>(Builtin::Overflow);
	if (host.nesting + 1 >= static_cast<uint32_t>(vm::Interpreter::MAX_ENTRIES) && p_thunk != overflow) {
		stack_overflow(c);
		return empty();
	}
	const uint32_t function = host.thunk_function(p_thunk);
	Slot *const outer = host.updated;
	host.updated = r_updated;
	host.nesting++;
	Slot result = empty();
	const bool done = host.interpreter->call_function(function, p_given, host.module.functions[function].parameters, &result);
	host.nesting--;
	host.updated = outer;
	if (!done && !c->failing) {
		// Not an error the thunk caught. With the interpreter's frames beneath (it counts entries its own
		// library makes too), the interpreter carries the error to the instruction that led here, whose
		// thunk catches it: until then generated code is only left. With none, the library could not go on
		// and has said why: everything is left as if by an error nothing catches.
		(host.interpreter->depth() > 0 ? host.carried : host.stopped) = true;
		c->pending = empty();
		c->failing = true;
	}
	return result;
}

bool is_class(const Context *c, const Object *p_object, uint32_t p_class) {
	if (!p_object) {
		return false;
	}
	const ClassInfo *classes = c->classes;
	if (classes[p_class].is_interface) {
		const ClassInfo &own = classes[p_object->klass];
		for (uint32_t i = 0; i < own.interface_count; i++) {
			if (own.interfaces[i] == p_class) {
				return true;
			}
		}
		return false;
	}
	for (uint32_t at = p_object->klass; at != NONE; at = classes[at].base) {
		if (at == p_class) {
			return true;
		}
	}
	return false;
}

// The class and every class that extends it are unused again (as vm::Interpreter::forget_class).
static void forget_class(Host &r_host, uint32_t p_class) {
	for (uint32_t i = 0; i < r_host.program.class_count; i++) {
		for (uint32_t at = i; at != NONE; at = r_host.program.classes[at].base) {
			if (at == p_class) {
				r_host.class_used[i] = 0;
				break;
			}
		}
	}
}

void use_class(Context *c, uint32_t p_class) {
	c->host->use_class(p_class, true);
}

bool Host::set_up_by_library(uint32_t p_function) {
	const uint32_t owner = program.functions[p_function].owner;
	if (owner == NONE || program.classes[owner].static_init != p_function) {
		return false;
	}
	if (class_used[owner]) {
		return true; // Generated code used the class first: its fields have their values.
	}
	class_used[owner] = 1;
	Slot none = empty();
	program.functions[p_function].entry(&context, nullptr, 0, &none);
	if (context.failing) {
		forget_class(*this, owner);
	}
	return true;
}

void Host::use_class(uint32_t p_class, bool p_from_code) {
	Host &host = *this;
	Context *c = &context;
	// Marked before anything runs: a class that reads itself in a circle sees defaults.
	std::vector<uint32_t> chain;
	for (uint32_t at = p_class; at != NONE && !host.class_used[at]; at = host.program.classes[at].base) {
		host.class_used[at] = 1;
		chain.push_back(at);
	}
	// The base class's fields are set before the subclass's.
	for (size_t i = chain.size(); i-- > 0;) {
		const uint32_t initializer = host.program.classes[chain[i]].static_init;
		if (initializer == NONE) {
			continue;
		}
		Slot none = empty();
		host.program.functions[initializer].entry(c, nullptr, 0, &none);
		if (c->failing) {
			// Not ready: the next use sets the fields again, and fails again, where carrying on would
			// run with fields that were never set. The subclasses that were waiting are forgotten too.
			forget_class(host, chain[i]);
			// Under the interpreter, code that uses a class has every initializer waiting as a frame of
			// its own before the first runs, and an error leaves each: a line for each one that had not
			// begun, the nearest base's first.
			for (size_t waiting = i; p_from_code && waiting-- > 0;) {
				const uint32_t later = host.program.classes[chain[waiting]].static_init;
				if (later != NONE) {
					leave_failed(c, later, host.program.functions[later].line, false);
				}
			}
			return;
		}
	}
}

bool Host::run() {
	for (uint32_t i = 0; i < program.initializer_count; i++) {
		Slot none = empty();
		program.functions[program.initializers[i]].entry(&context, nullptr, 0, &none);
		if (context.failing) {
			report();
			return false;
		}
	}
	const uint32_t entry = program.known.entry;
	if (entry == NONE) {
		return true;
	}
	if (!class_used[entry]) {
		use_class(entry, false);
	}
	Slot made = empty();
	if (!context.failing) {
		made = builtin(Builtin::NewEntry, nullptr);
	}
	Object *object = static_cast<Object *>(made.p);
	if (!context.failing && program.classes[entry].constructor != NONE) {
		// As Flash constructs a document class: with no arguments, so every parameter takes its default.
		const FunctionInfo &constructor = program.functions[program.classes[entry].constructor];
		std::vector<Slot> arguments(constructor.parameters, empty());
		arguments[0] = made;
		Slot none = empty();
		constructor.entry(&context, arguments.data(), 1, &none);
	}
	const bool ran = !context.failing;
	if (!ran) {
		report();
	}
	release(&context, object);
	return ran;
}

} // namespace rt
} // namespace kas3
