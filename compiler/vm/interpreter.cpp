#include "interpreter_internal.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
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
		module_(p_module), host_(p_host), heap_(p_module, p_host.handles, p_host.source) {
	for (const std::string &constant : module_.strings) {
		strings_.push_back(new_string(constant));
	}
	null_text_ = new_string("null");
	memory_text_ = new_string(OUT_OF_MEMORY);
	for (const char *name : { "undefined", "boolean", "number", "string", "object", "function" }) {
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
	prepare_engine();
}

// Each engine instruction is worked out by the host once, now, on the thread that loads the program: none
// is looked up by its name as the program runs. A function with none keeps an empty list.
void Interpreter::prepare_engine() {
	operators_.resize(module_.engine_names.size());
	for (size_t i = 0; i < module_.engine_names.size(); i++) {
		// The description's names for the unary ones are "unary-" and "unary+".
		std::string_view spelling = module_.engine_names[i].name;
		if (spelling.substr(0, 5) == "unary") {
			spelling.remove_prefix(5);
		}
		// An operator's entry is "<name> <Godot's number>"; anything else is not an operator and has none.
		const std::string &value = module_.engine_names[i].value;
		const size_t space = value.find(' ');
		const bool numbered = module_.engine_names[i].key.rfind("@operator.", 0) == 0 && space != std::string::npos;
		operators_[i] = OperatorName{ spelling, spelling == "==" || spelling == "!=", numbered ? std::atoi(value.c_str() + space + 1) : -1 };
	}
	prepared_.resize(module_.functions.size());
	for (size_t f = 0; f < module_.functions.size(); f++) {
		const Function &function = module_.functions[f];
		for (size_t i = 0; i < function.code.size(); i++) {
			const char *shape = ir::op_shape(function.code[i].op);
			if (std::strchr(shape, 'E') == nullptr) {
				continue;
			}
			if (const Prepared *prepared = host_.handles.prepare(module_, function, function.code[i])) {
				prepared_[f].resize(function.code.size());
				prepared_[f][i] = EngineStep{ prepared, untyped_operand(function, function.code[i]) };
			}
		}
	}
}

Interpreter::~Interpreter() {
	while (!frames_.empty()) {
		pop();
	}
	for (void *kept : singletons_) {
		if (kept) {
			host_.handles.drop(kept);
		}
	}
	for (Object *kept : lone_functions_) {
		release_object(kept);
	}
	for (auto &[code, kept] : class_values_) {
		release_object(kept);
	}
	for (size_t i = 0; i < globals_.size(); i++) {
		drop(module_.globals[i].kind, globals_[i]);
	}
	if (failing_) {
		drop_any(pending_);
	}
	forget_subject();
	// With every root let go of, what is left in loops is held only by itself, or by the host.
	heap_.collector.collect();
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
	for (Str *character : characters_) {
		if (release(character)) {
			free_string(character);
		}
	}
	if (release(null_text_)) {
		free_string(null_text_);
	}
	if (release(memory_text_)) {
		free_string(memory_text_);
	}
}

void Interpreter::clear_globals() {
	if (native_) {
		native_->clear_globals();
	}
	for (size_t i = 0; i < globals_.size(); i++) {
		Slot value = globals_[i];
		globals_[i].bits = 0;
		drop(module_.globals[i].kind, value);
	}
	forget_subject();
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

bool Interpreter::push(uint32_t p_function, const Slot *p_arguments, size_t p_count, uint32_t p_result) {
	const Function &function = module_.functions[p_function];
	if (frames_.size() >= MAX_FRAMES) {
		stack_overflow();
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
	// With a rest parameter, the arguments past the others (untyped) are its Array.
	const size_t fixed = function.rest ? function.parameters - 1 : function.parameters;
	Vector *rest = nullptr;
	if (function.rest) {
		rest = fresh_vector(module_.array_class, p_count > fixed ? p_count - fixed : 0);
		if (!rest) {
			return false;
		}
		for (size_t i = fixed; i < p_count; i++) {
			rest->data[i - fixed] = share(Kind::Any, source[i]);
		}
		frame.arguments = static_cast<uint32_t>(std::min(p_count, fixed));
	}
	stack_.resize(stack_.size() + function.registers.size(), zero);
	size_t count = std::min<size_t>(std::min<size_t>(p_count, fixed), function.registers.size());
	for (size_t i = 0; i < count; i++) {
		stack_[frame.base + i] = share(function.registers[i], source[i]);
	}
	if (rest) {
		stack_[frame.base + fixed].p = rest; // Its one reference, the frame's.
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
	while (!kept_traces_.empty() && kept_traces_.back().depth > frames_.size()) {
		kept_traces_.pop_back(); // Kept for a frame that is gone.
	}
}

// One entry from outside: runs the function to its end. An error is reported to the host and cleared, so
// that the next entry starts clean; an error that was already on its way out when this entry began is put
// back afterwards.
bool Interpreter::call_within(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) {
	if (entries_ >= MAX_ENTRIES) {
		stack_overflow();
		return false;
	}
	const size_t depth = frames_.size();
	entering();
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
	// An error already carried for the instruction beneath (an earlier listener's) is that call's: set
	// aside, so that what this call's own engine calls carry is judged and raised on its own.
	Carried outer_carried;
	const bool aside = carried_.held;
	if (aside) {
		outer_carried = std::move(carried_);
		carried_ = Carried();
	}

	size_t depth = frames_.size();
	bool ok = false;
	if (entries_ >= MAX_ENTRIES) {
		stack_overflow();
	} else {
		entering();
		entries_++;
		ok = push(p_function, p_arguments, p_count, NONE) && run(depth, r_result);
		entries_--;
	}
	// This program's code is running an instruction beneath this call: the error goes back to it, as Flash
	// has an error in a listener come out of the dispatch that ran it. Only the first: a later listener's
	// error, with one already carried, is reported here, where it happened.
	const bool carry = !ok && (depth > 0 || direct_ > 0) && failing_ && !outer_failing && !outer_stopped && !aside;
	if (!ok && !carry) {
		report();
	}
	if (carried_.held) {
		report_carried(); // Carried inside this call and taken back by nothing: reported here, as it would have been.
	}
	if (carry) {
		carried_.held = true;
		carried_.error = pending_;
		carried_.trace = std::move(trace_);
		carried_.lines = trace_lines_;
		carried_.at = thrown_at_;
		carried_.stopped = stopped_;
		carried_.frames = depth;
		carries_++;
	} else if (aside) {
		carried_ = std::move(outer_carried);
	}
	pending_ = outer_error;
	failing_ = outer_failing;
	trace_ = std::move(outer_trace);
	trace_lines_ = outer_lines;
	// Stopped, the instruction that made the engine's call ends as soon as it can: run() raises the error
	// there (raise_carried()). Nothing on the path that succeeds looks for it.
	stopped_ = outer_stopped || carry;
	halted_ = outer_halted;
	return ok;
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
	// The constants are made with the interpreter, which can't raise: a program missing one does not run,
	// now or later.
	const auto missing = [](const std::vector<Str *> &p_strings) {
		return std::find(p_strings.begin(), p_strings.end(), nullptr) != p_strings.end();
	};
	if (!null_text_ || !memory_text_ || missing(strings_) || missing(type_names_)) {
		host_.output.error(OUT_OF_MEMORY);
		return false;
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
