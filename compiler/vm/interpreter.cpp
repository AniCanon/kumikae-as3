#include "interpreter_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

Interpreter::Interpreter(const ir::Module &p_module, Host p_host) :
		module_(p_module), host_(p_host) {
	for (const std::string &constant : module_.strings) {
		strings_.push_back(new_string(constant));
	}
	null_text_ = new_string("null");
	for (const char *name : { "undefined", "boolean", "number", "string", "object" }) {
		type_names_.push_back(new_string(name));
	}
	// A method's function by name, for calls from outside: the class's own and what it inherits.
	methods_.resize(module_.classes.size());
	for (size_t i = 0; i < module_.classes.size(); i++) {
		for (uint32_t method : module_.classes[i].methods) {
			const std::string &name = module_.functions[method].name;
			methods_[i][name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1)] = method;
		}
	}
	class_used_.assign(module_.classes.size(), 0);
	globals_.resize(module_.globals.size());
	for (size_t i = 0; i < globals_.size(); i++) {
		globals_[i] = empty_value(module_.globals[i].kind == Kind::F64, module_.globals[i].starts_null);
	}
}

Interpreter::~Interpreter() {
	while (!frames_.empty()) {
		pop();
	}
	for (size_t i = 0; i < globals_.size(); i++) {
		drop(module_.globals[i].kind, globals_[i]);
	}
	if (failing_) {
		drop_any(pending_);
	}
	for (Str *constant : strings_) {
		if (release(constant)) {
			free_string(constant);
		}
	}
	for (Str *name : type_names_) {
		if (release(name)) {
			free_string(name);
		}
	}
	if (release(null_text_)) {
		free_string(null_text_);
	}
}

void Interpreter::clear_globals() {
	for (size_t i = 0; i < globals_.size(); i++) {
		Slot value = globals_[i];
		globals_[i].bits = 0;
		drop(module_.globals[i].kind, value);
	}
}

int Interpreter::line_at(size_t p_frame) const {
	const Frame &frame = frames_[p_frame];
	const Function &function = module_.functions[frame.function];
	uint32_t pc = frame.pc < function.code.size() ? frame.pc : (uint32_t)function.code.size() - 1;
	// A frame that is waiting for a call to return is at the instruction after the call.
	if (p_frame + 1 < frames_.size() && pc > 0) {
		pc--;
	}
	uint32_t position = function.code.empty() ? function.position : function.code[pc].position;
	if (function.file >= module_.lines.size()) {
		return 0;
	}
	const std::vector<uint32_t> &lines = module_.lines[function.file];
	return (int)(std::upper_bound(lines.begin(), lines.end(), position) - lines.begin());
}

// "\tat Main/update()[Main.as:12]"
std::string Interpreter::site(const Frame &p_frame) const {
	const Function &function = module_.functions[p_frame.function];
	size_t index = (size_t)(&p_frame - frames_.data());
	std::string file = function.file < module_.files.size() ? module_.files[function.file] : "?";
	return "\tat " + function.name + "()[" + file + ":" + std::to_string(line_at(index)) + "]";
}

bool Interpreter::push(uint32_t p_function, const Slot *p_arguments, size_t p_count, uint32_t p_result) {
	const Function &function = module_.functions[p_function];
	if (frames_.size() >= MAX_FRAMES) {
		raise(module_.error_class, "Error #1023: Stack overflow occurred.");
		return false;
	}
	Frame frame;
	frame.function = p_function;
	frame.pc = 0;
	frame.base = stack_.size();
	frame.result = p_result;
	frame.arguments = (uint32_t)p_count;
	Slot zero;
	zero.bits = 0;
	// The arguments may live in stack_ itself, which is about to grow: copy them first.
	Slot arguments[16];
	std::vector<Slot> many;
	const Slot *source = p_arguments;
	if (p_count <= 16) {
		std::copy(p_arguments, p_arguments + p_count, arguments);
		source = arguments;
	} else {
		many.assign(p_arguments, p_arguments + p_count);
		source = many.data();
	}
	stack_.resize(stack_.size() + function.registers.size(), zero);
	size_t count = std::min<size_t>(std::min<size_t>(p_count, function.parameters), function.registers.size());
	for (size_t i = 0; i < count; i++) {
		stack_[frame.base + i] = share(function.registers[i], source[i]);
	}
	frames_.push_back(frame);
	return true;
}

void Interpreter::pop() {
	// Giving up a register can call back into the interpreter, which pushes and pops frames above this
	// one: nothing is kept across a drop but the frame's place.
	const size_t index = frames_.size() - 1;
	const size_t base = frames_[index].base;
	const Function &function = module_.functions[frames_[index].function];
	for (size_t i = 0; i < function.registers.size(); i++) {
		if (held(function.registers[i])) {
			Slot value = stack_[base + i];
			stack_[base + i].bits = 0;
			drop(function.registers[i], value);
		}
	}
	stack_.resize(base);
	frames_.resize(index);
}

// Starts an error on its way out: an object of the class with the message.
void Interpreter::raise_null() {
	raise(module_.type_error_class, "Error #1009: Cannot access a property or method of a null object reference.");
}

void Interpreter::raise(uint32_t p_class, const std::string &p_message) {
	if (failing_) {
		return;
	}
	Object *error = p_class != NONE ? new_object(p_class, module_.classes[p_class].fields.size()) : nullptr;
	if (error) {
		error->fields()[0].p = text(p_message);
		pending_ = any::object(error);
		failing_ = true;
	} else {
		trace_ = p_message + "\n";
		stopped_ = true; // Nothing to throw with: stop.
	}
}

// Catches the error, or leaves frames until p_depth remain, adding each to the stack trace.
bool Interpreter::catch_error(size_t p_depth) {
	bool innermost = true;
	while (frames_.size() > p_depth) {
		Frame &frame = frames_.back();
		if (!innermost && frame.pc > 0) {
			frame.pc--; // A caller: at its call, as for the trace.
		}
		innermost = false;
		const Function &function = module_.functions[frame.function];
		for (const ir::Handler &handler : function.handlers) {
			if (!failing_ || stopped_ || frame.pc < handler.start || frame.pc >= handler.end ||
					(handler.type != NONE && !any_is(pending_, handler.type))) {
				continue;
			}
			// Caught: the error goes to the catch's variable, as its kind holds it, and is no longer on
			// its way out.
			const Kind kind = function.registers[handler.reg];
			const Slot error = pending_;
			pending_.bits = 0;
			failing_ = false;
			trace_.clear();
			trace_lines_ = 0;
			Slot caught;
			caught.bits = 0;
			const bool converted = from_any(error, kind, NONE, &caught);
			drop_any(error);
			if (converted) {
				put(kind, stack_[frame.base + handler.reg], caught);
			}
			frame.pc = handler.target;
			return true;
		}
		leave_failed_frame();
	}
	return false;
}

void Interpreter::leave_failed_frame() {
	// A class whose static fields were being set when the error came is not ready: the next use of it
	// sets them again, and fails again, where carrying on would run with fields that were never set.
	const Function &leaving = module_.functions[frames_.back().function];
	if (leaving.owner != NONE && module_.classes[leaving.owner].static_init == frames_.back().function) {
		forget_class(leaving.owner);
	}
	// A runaway recursion has thousands of frames: the first few and a count say as much.
	if (trace_lines_ < MAX_TRACE) {
		trace_ += site(frames_.back()) + "\n";
	}
	trace_lines_++;
	pop();
}

// One entry from outside: runs the function to its end. An error is reported to the host and cleared, so
// that the next entry starts clean; an error that was already on its way out when this entry began is put
// back afterwards.
bool Interpreter::call_within(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) {
	if (entries_ >= MAX_ENTRIES) {
		raise(module_.error_class, "Error #1023: Stack overflow occurred.");
		return false;
	}
	const size_t depth = frames_.size();
	entries_++;
	const bool ok = push(p_function, p_arguments, p_count, NONE) && run(depth, r_result);
	entries_--;
	return ok;
}

bool Interpreter::call(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) {
	const Slot outer_error = pending_;
	const bool outer_failing = failing_;
	std::string outer_trace = std::move(trace_);
	size_t outer_lines = trace_lines_;
	bool outer_stopped = stopped_;
	bool outer_halted = halted_;
	pending_.bits = 0;
	failing_ = false;
	trace_.clear();
	trace_lines_ = 0;
	stopped_ = false;
	halted_ = false;

	size_t depth = frames_.size();
	bool ok = false;
	if (entries_ >= MAX_ENTRIES) {
		raise(module_.error_class, "Error #1023: Stack overflow occurred.");
	} else {
		entries_++;
		ok = push(p_function, p_arguments, p_count, NONE) && run(depth, r_result);
		entries_--;
	}
	if (!ok) {
		report();
	}
	pending_ = outer_error;
	failing_ = outer_failing;
	trace_ = std::move(outer_trace);
	trace_lines_ = outer_lines;
	stopped_ = outer_stopped;
	halted_ = outer_halted;
	return ok;
}

std::string Interpreter::error_text() {
	if (any::tag(pending_) == AnyTag::Object) {
		return object_text(static_cast<Object *>(any::pointer(pending_)));
	}
	if (const std::optional<std::string> plain = any::text(pending_)) {
		return *plain;
	}
	// A Godot value or object: Godot's own text for it.
	Str *words = host_.handles.text(any::pointer(pending_));
	std::string out = words ? std::string(words->view()) : std::string("null");
	if (release(words)) {
		free_string(words);
	}
	return out;
}

// Gives the host the error that ended an entry, and forgets it.
void Interpreter::report() {
	if (!(halted_ && !failing_)) { // Stopped on request: there is no error to report.
		if (trace_lines_ > MAX_TRACE) {
			trace_ += "\t... " + std::to_string(trace_lines_ - MAX_TRACE) + " more\n";
		}
		std::string text = failing_ ? error_text() + "\n" + trace_ : trace_;
		while (!text.empty() && text.back() == '\n') {
			text.pop_back();
		}
		host_.output.error(text);
	}
	if (failing_) {
		drop_any(pending_);
	}
	pending_.bits = 0;
	failing_ = false;
	trace_.clear();
	trace_lines_ = 0;
}

// The class and the bases above it that have not been used yet, the class first.
static std::vector<uint32_t> unused_chain(const ir::Module &p_module, std::vector<uint8_t> &r_used, uint32_t p_class) {
	std::vector<uint32_t> chain;
	for (uint32_t at = p_class; at != NONE && !r_used[at]; at = p_module.classes[at].base) {
		r_used[at] = 1; // Marked before anything runs: a class that reads itself in a circle sees defaults.
		chain.push_back(at);
	}
	return chain;
}

// The class and every class that extends it are unused again.
void Interpreter::forget_class(uint32_t p_class) {
	for (size_t i = 0; i < module_.classes.size(); i++) {
		for (uint32_t at = (uint32_t)i; at != NONE; at = module_.classes[at].base) {
			if (at == p_class) {
				class_used_[i] = 0;
				break;
			}
		}
	}
}

void Interpreter::use_class(uint32_t p_class) {
	// Pushed last runs first: the base class's fields are set before the subclass's.
	for (uint32_t klass : unused_chain(module_, class_used_, p_class)) {
		uint32_t initializer = module_.classes[klass].static_init;
		if (initializer != NONE && !push(initializer, nullptr, 0, NONE)) {
			forget_class(klass); // No room to run it. What was pushed is forgotten as its frames are left.
			return;
		}
	}
}

bool Interpreter::use_class_now(uint32_t p_class) {
	std::vector<uint32_t> chain = unused_chain(module_, class_used_, p_class);
	for (size_t i = chain.size(); i-- > 0;) {
		uint32_t initializer = module_.classes[chain[i]].static_init;
		if (initializer != NONE && !call(initializer, nullptr, 0)) {
			forget_class(chain[i]); // And with it the subclasses that were waiting their turn.
			return false;
		}
	}
	return true;
}

bool Interpreter::local_at(size_t p_frame, std::string_view p_name, Slot *r_value, Kind *r_kind) const {
	if (p_frame >= frames_.size()) {
		return false;
	}
	const Function &function = module_.functions[frames_[p_frame].function];
	for (const ir::Local &local : function.locals) {
		if (local.name == p_name && local.reg < function.registers.size()) {
			*r_value = stack_[frames_[p_frame].base + local.reg];
			*r_kind = function.registers[local.reg];
			return true;
		}
	}
	return false;
}

bool Interpreter::start() {
	if (started_) {
		return true;
	}
	started_ = true;
	bool ok = true;
	for (uint32_t initializer : module_.initializers) {
		ok = ok && call(initializer, nullptr, 0);
	}
	return ok;
}

bool Interpreter::call_method(Object *p_object, std::string_view p_name, const Slot *p_arguments, size_t p_count, Slot *r_result) {
	uint32_t method = p_object ? find_method(p_object->klass, p_name) : NONE;
	if (method == NONE) {
		return false;
	}
	std::vector<Slot> arguments(p_count + 1);
	arguments[0].bits = 0;
	arguments[0].p = p_object;
	for (size_t i = 0; i < p_count; i++) {
		arguments[i + 1] = p_arguments[i];
	}
	return call(method, arguments.data(), arguments.size(), r_result);
}

bool Interpreter::run_program() {
	if (!start()) {
		return false;
	}
	if (module_.entry_class == NONE) {
		return true;
	}
	Object *entry = construct(module_.entry_class);
	if (!entry) {
		return false;
	}
	release_object(entry);
	return true;
}

} // namespace vm
} // namespace kas3
