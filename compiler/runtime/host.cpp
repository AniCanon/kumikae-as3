#include "native_host.h"

#include "fast.h"
#include "held.h"

#include <cstdio>
#include <cstdlib>

namespace kas3 {
namespace rt {

using ir::Kind;
using ir::Op;

static_assert(MAX_DEPTH == vm::Interpreter::MAX_FRAMES, "generated code and the interpreter overflow at the same depth");
static_assert(NONE == ir::NONE, "the program table and the IR write `none` alike");

Host::Host(const Program &p_program, const Embedder &p_embedder) :
		program(p_program), module(p_embedder.module ? native_module(*p_embedder.module, p_program) : stub_module(p_program)),
		output(p_embedder.output), handles(p_embedder.handles), limit(context, p_embedder.clock) {
	interpreter = std::make_unique<vm::Interpreter>(module, vm::Host(p_embedder.output, p_embedder.handles, p_embedder.source, p_embedder.clock));
	interpreter->set_native(this);
	// Before anything is made: an object is made with room for what it was constructed with.
	if (p_embedder.remember && program.remembers) {
		interpreter->remember_constructions(true);
		context.remembers = interpreter->remembers();
	}
	asked = runner_for(&context);
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
		made_here.push_back(klass.element == Kind::Void && !klass.table && !klass.is_interface && klass.native_base.empty());
	}
	// Kept for the next `new`: objects that are numbers and Booleans and nothing more, which the collector
	// does not look at and no one but the program holds.
	for (uint32_t i = 0; i < module.classes.size(); i++) {
		const ir::Class &klass = module.classes[i];
		// Where what objects are constructed with is remembered, an object has room for that after its
		// fields (vm/constructions.h): the block kept is the whole of it, and is emptied whole as it is
		// made again. Only where nothing remembered is counted: where an argument may be text, the heap
		// frees the object, and gives the text up.
		vm::Constructions *const remembered = interpreter->heap().constructions;
		const size_t slots = klass.fields.size() + (remembered ? remembered->room(i) : 0);
		bool plain = made_here[i] && !klass.counted && !klass.array && !klass.dictionary && !klass.built_in && slots + 1 < ObjectCache::SIZES &&
				!interpreter->heap().list_for(i) && (!remembered || remembered->room(i) == 0 || remembered->plain_arguments(i));
		uint32_t numbers = 0;
		for (size_t at = 0; at < klass.fields.size() && plain; at++) {
			plain = !ir::is_held(klass.fields[at].kind);
			numbers |= klass.fields[at].kind == Kind::F64 ? uint32_t(1) << at : 0;
		}
		class_cached.push_back(plain ? static_cast<uint8_t>(slots + 1) : 0);
		if (plain && remembered) {
			remembered->recycled(i);
		}
		class_numbers.push_back(numbers);
	}
	context.class_numbers = class_numbers.data();
	context.cache = &cache;
	context.class_cached = class_cached.data();
	if (std::getenv("KAS3_NATIVE_COUNTS")) {
		thunk_counts.assign(module.functions.size() - program.function_count, 0);
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
	context.api = runtime_api();
	context.host = this;
	limit.set(p_embedder.time_limit);
}

void Host::clear_globals() {
	for (uint32_t i = 0; i < program.global_count; i++) {
		const Slot value = globals[i];
		globals[i].bits = 0;
		interpreter->discard(static_cast<Kind>(program.globals[i].kind), value);
	}
}

// What a thunk asks of the library, for the counts: the operation, and Godot's name where it asks Godot.
static std::string asked_of(const ir::Module &p_module, const ir::Instruction &p_in) {
	std::string text = ir::op_name(p_in.op);
	const bool by_b = p_in.op == Op::HostCall || p_in.op == Op::HostNew || p_in.op == Op::HostOperate || p_in.op == Op::HostSet ||
			p_in.op == Op::HostSingleton || p_in.op == Op::HostConstant || p_in.op == Op::HostCallStatic;
	const uint32_t name = by_b ? p_in.b : p_in.op == Op::HostGet ? p_in.c : NONE;
	if (name < p_module.engine_names.size()) {
		text += " " + (p_module.engine_names[name].key.empty() ? p_module.engine_names[name].name : p_module.engine_names[name].key);
	}
	return text;
}

void Host::say_counts() const {
	uint64_t all = 0;
	for (uint64_t each : thunk_counts) {
		all += each;
	}
	std::fprintf(stderr, "kas3: counts thunks=%llu unresolved=%zu handles=%llu lookups=%llu\n", static_cast<unsigned long long>(all), unresolved.size(),
			static_cast<unsigned long long>(handle_copies), static_cast<unsigned long long>(object_lookups ? object_lookups() : 0));
	for (size_t i = 0; i < thunk_counts.size(); i++) {
		if (thunk_counts[i]) {
			const std::string what = asked_of(module, module.functions[thunk_function(static_cast<uint32_t>(i))].code[0]);
			std::fprintf(stderr, "kas3:   %llu %s\n", static_cast<unsigned long long>(thunk_counts[i]), what.c_str());
		}
	}
	if (context.remembers) {
		std::fprintf(stderr, "kas3:   told of %llu constructions, %zu remembered now\n", static_cast<unsigned long long>(told), interpreter->remembered());
	}
	for (uint32_t use : unresolved) {
		std::fprintf(stderr, "kas3:   unresolved %s\n", asked_of(module, module.functions[thunk_function(program.uses[use])].code[0]).c_str());
	}
}

Host::~Host() {
	limit.set(0); // Its thread reads the context.
	if (!thunk_counts.empty()) {
		say_counts();
	}
	Host::clear_globals();
	if (context.failing) {
		interpreter->discard(Kind::Any, context.pending);
	}
	// What the cache kept is the heap's again.
	for (Object *&list : cache.spare) {
		while (Object *const block = list) {
			list = block->gc_next;
			block->gc_next = nullptr;
			vm::free_object(block);
		}
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
	const ir::Class &klass = c->host->module.classes[p_class];
	Object *object = vm::new_object(p_class, c->host->interpreter->heap().object_slots(p_class), c->host->interpreter->heap().list_for(p_class));
	if (object) {
		vm::reset_fields(klass, object);
	}
	return object;
}

void constructing(Context *c, uint32_t p_function, const Slot *p_arguments, uint32_t p_count) {
	c->host->told++;
	c->host->interpreter->constructing(p_function, p_arguments, p_count);
}

void *copy_handle(Context *c, void *p_handle) {
	c->host->handle_copies++;
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
	// The interpreter refuses an entry with the stack nearly used up, and one that is refused has no frame
	// to catch its error in: Error #1023 is raised here, where generated code can catch it.
	const uintptr_t floor = c->stack_floor.load(std::memory_order_relaxed);
	if (vm::stack_below(floor ? floor : vm::stack_floor()) && stack_overflow(c)) {
		return empty();
	}
	const uint32_t function = host.thunk_function(p_thunk);
	if (KAS3_UNLIKELY(!host.thunk_counts.empty())) {
		host.thunk_counts[p_thunk]++;
	}
	if (p_thunk < host.program.thunk_count && asks_engine(host.program.thunks[p_thunk].op)) {
		return engine_thunk(c, function, host.program.thunks[p_thunk], p_given, r_updated);
	}
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
	// Each is a call from outside, counted with the interpreter's own: what the library calls back
	// meanwhile is inside it, and its time is the call's (Error #1502).
	for (uint32_t i = 0; i < program.initializer_count; i++) {
		Slot none = empty();
		interpreter->entered();
		program.functions[program.initializers[i]].entry(&context, nullptr, 0, &none);
		interpreter->left();
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
		interpreter->entered();
		constructor.entry(&context, arguments.data(), 1, &none);
		interpreter->left();
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
