#include "interpreter.h"

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

Interpreter::Interpreter(const ir::Module &p_module, Host &p_host) :
		module_(p_module), host_(p_host) {
	for (const std::string &constant : module_.strings) {
		strings_.push_back(new_string(constant));
	}
	null_text_ = new_string("null");
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
		globals_[i].bits = 0;
		if (module_.globals[i].kind == Kind::F64) {
			globals_[i].d = std::numeric_limits<double>::quiet_NaN();
		}
	}
}

Interpreter::~Interpreter() {
	while (!frames_.empty()) {
		pop();
	}
	for (size_t i = 0; i < globals_.size(); i++) {
		drop(module_.globals[i].kind, globals_[i]);
	}
	if (pending_ && release(pending_)) {
		drop_object(pending_);
	}
	for (Str *constant : strings_) {
		if (release(constant)) {
			free_string(constant);
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

// Gives up the reference a slot holds.
void Interpreter::drop(Kind p_kind, Slot &p_slot) {
	if (p_kind == Kind::Str) {
		Str *string = static_cast<Str *>(p_slot.p);
		if (release(string)) {
			free_string(string);
		}
	} else if (p_kind == Kind::Obj) {
		Object *object = static_cast<Object *>(p_slot.p);
		if (release(object)) {
			drop_object(object);
		}
	} else if (ir::is_engine(p_kind) && p_slot.p) {
		// Letting go of an engine value can run engine code that calls back in and moves the registers:
		// the slot is emptied first and not touched again.
		void *handle = p_slot.p;
		p_slot.bits = 0;
		host_.drop(handle);
		return;
	}
	p_slot.bits = 0;
}

// A Number as an Int64, the way a conversion the program wrote does it: not-a-number is 0, and a value
// past either end is held at that end.
static int64_t clamp_wide(double p_number) {
	if (p_number != p_number) {
		return 0;
	}
	if (p_number >= 9223372036854775808.0) {
		return INT64_MAX;
	}
	if (p_number < -9223372036854775808.0) {
		return INT64_MIN;
	}
	return (int64_t)p_number;
}

// Text as an Int64: digits are read exactly, so that every Int64 can be written; anything else is read
// as a Number first, as int("12.7") is.
static int64_t string_to_wide(std::string_view p_text) {
	size_t start = 0, end = p_text.size();
	while (start < end && (p_text[start] == ' ' || p_text[start] == '\t' || p_text[start] == '\n' || p_text[start] == '\r')) {
		start++;
	}
	while (end > start && (p_text[end - 1] == ' ' || p_text[end - 1] == '\t' || p_text[end - 1] == '\n' || p_text[end - 1] == '\r')) {
		end--;
	}
	size_t at = start;
	bool negative = at < end && p_text[at] == '-';
	if (at < end && (p_text[at] == '-' || p_text[at] == '+')) {
		at++;
	}
	// Hex, as AS3 reads "0x10".
	const bool hex = at + 1 < end && p_text[at] == '0' && (p_text[at + 1] == 'x' || p_text[at + 1] == 'X');
	const uint64_t base = hex ? 16 : 10;
	at += hex ? 2 : 0;
	bool digits = at < end;
	uint64_t value = 0;
	const uint64_t limit = negative ? (uint64_t)1 << 63 : (uint64_t)INT64_MAX;
	bool past = false;
	for (size_t i = at; i < end && digits; i++) {
		const char c = p_text[i];
		const uint64_t digit = c >= '0' && c <= '9' ? (uint64_t)(c - '0')
				: hex && c >= 'a' && c <= 'f' ? (uint64_t)(c - 'a' + 10)
				: hex && c >= 'A' && c <= 'F' ? (uint64_t)(c - 'A' + 10)
											  : base;
		if (digit >= base) {
			digits = false;
		} else if (!past) {
			past = value > (limit - digit) / base;
			value = past ? limit : value * base + digit;
		}
	}
	if (!digits) {
		return clamp_wide(string_to_number(p_text));
	}
	return negative ? (int64_t)(0 - value) : (int64_t)value;
}

// An Int64 against a Number, exactly: below, equal or above as -1, 0 or 1, and not-a-number for one.
static double compare_wide(int64_t p_wide, double p_number) {
	if (p_number != p_number) {
		return p_number;
	}
	if (p_number >= 9223372036854775808.0) {
		return -1;
	}
	if (p_number < -9223372036854775808.0) {
		return 1;
	}
	int64_t whole = (int64_t)p_number; // Towards zero.
	if (p_wide != whole) {
		return p_wide < whole ? -1 : 1;
	}
	double fraction = p_number - (double)whole;
	return fraction > 0 ? -1 : (fraction < 0 ? 1 : 0);
}

static bool held(Kind p_kind) {
	return ir::is_reference(p_kind) || ir::is_engine(p_kind);
}

Slot Interpreter::share(Kind p_kind, Slot p_value) {
	if (ir::is_reference(p_kind)) {
		retain(static_cast<Counted *>(p_value.p));
	} else if (ir::is_engine(p_kind) && p_value.p) {
		p_value.p = host_.copy(p_value.p);
	}
	return p_value;
}

void Interpreter::put(Kind p_kind, Slot &p_target, Slot p_owned) {
	Slot old = p_target;
	p_target = p_owned;
	if (held(p_kind)) {
		drop(p_kind, old);
	}
}

// Frees an object whose last reference is gone, and whatever that in turn leaves unreferenced. A list
// rather than recursion: a long chain of objects must not overflow the C++ stack.
void Interpreter::drop_object(Object *p_object) {
	std::vector<Object *> dead;
	dead.push_back(p_object);
	while (!dead.empty()) {
		Object *object = dead.back();
		dead.pop_back();
		const ir::Class &klass = module_.classes[object->klass];
		for (size_t i = 0; i < klass.fields.size(); i++) {
			Slot &field = object->fields()[i];
			if (klass.fields[i].kind == Kind::Str) {
				Str *string = static_cast<Str *>(field.p);
				if (release(string)) {
					free_string(string);
				}
			} else if (klass.fields[i].kind == Kind::Obj) {
				Object *other = static_cast<Object *>(field.p);
				if (release(other)) {
					dead.push_back(other);
				}
			} else if (ir::is_engine(klass.fields[i].kind) && field.p) {
				void *handle = field.p;
				field.p = nullptr;
				host_.drop(handle);
			}
		}
		free_object(object);
	}
}

void Interpreter::assign(Kind p_kind, Slot &p_slot, void *p_value) {
	Slot old = p_slot;
	p_slot.bits = 0;
	p_slot.p = p_value;
	drop(p_kind, old);
}

Str *Interpreter::text(const std::string &p_text) {
	return new_string(p_text);
}

bool Interpreter::is_class(const Object *p_object, uint32_t p_class) const {
	if (!p_object) {
		return false;
	}
	for (uint32_t at = p_object->klass; at != NONE; at = module_.classes[at].base) {
		if (at == p_class) {
			return true;
		}
	}
	return false;
}

std::string Interpreter::object_text(Object *p_object) const {
	if (!p_object) {
		return "null";
	}
	const std::string &name = module_.classes[p_object->klass].name;
	std::string simple = name.substr(name.rfind('.') == std::string::npos ? 0 : name.rfind('.') + 1);
	if (module_.error_class != NONE && is_class(p_object, module_.error_class)) {
		// An Error reads as its class and its message.
		Str *message = static_cast<Str *>(p_object->fields()[0].p);
		return message && message->size ? simple + ": " + std::string(message->view()) : simple;
	}
	return "[object " + simple + "]";
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
void Interpreter::raise(uint32_t p_class, const std::string &p_message) {
	if (pending_) {
		return;
	}
	Object *error = p_class != NONE ? new_object(p_class, module_.classes[p_class].fields.size()) : nullptr;
	if (error) {
		error->fields()[0].p = text(p_message);
	} else {
		trace_ = p_message + "\n";
		stopped_ = true; // Nothing to throw with: stop.
	}
	pending_ = error;
}

// Leaves frames until p_depth remain, adding each to the stack trace.
void Interpreter::unwind(size_t p_depth) {
	bool innermost = true;
	while (frames_.size() > p_depth) {
		// A caller is at the instruction after its call: step back to the call for the line number.
		if (!innermost && frames_.back().pc > 0) {
			frames_.back().pc--;
		}
		innermost = false;
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
}

// One entry from outside: runs the function to its end. An error is reported to the host and cleared, so
// that the next entry starts clean; an error that was already on its way out when this entry began is put
// back afterwards.
bool Interpreter::call(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) {
	Object *outer_error = pending_;
	std::string outer_trace = std::move(trace_);
	size_t outer_lines = trace_lines_;
	bool outer_stopped = stopped_;
	bool outer_halted = halted_;
	pending_ = nullptr;
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
	trace_ = std::move(outer_trace);
	trace_lines_ = outer_lines;
	stopped_ = outer_stopped;
	halted_ = outer_halted;
	return ok;
}

// Gives the host the error that ended an entry, and forgets it.
void Interpreter::report() {
	if (!(halted_ && !pending_)) { // Stopped on request: there is no error to report.
		if (trace_lines_ > MAX_TRACE) {
			trace_ += "\t... " + std::to_string(trace_lines_ - MAX_TRACE) + " more\n";
		}
		std::string text = pending_ ? object_text(pending_) + "\n" + trace_ : trace_;
		while (!text.empty() && text.back() == '\n') {
			text.pop_back();
		}
		host_.error(text);
	}
	if (pending_ && release(pending_)) {
		drop_object(pending_);
	}
	pending_ = nullptr;
	trace_.clear();
	trace_lines_ = 0;
}

// One request to the engine. Raises an AS3 error if the engine refuses.
const char *Interpreter::host_op(const Function &p_function, const Instruction &p_in) {
	const size_t base = frames_.back().base;
	const size_t frame_index = frames_.size() - 1;
	Slot *r = stack_.data() + base;
	static const char *const NULL_ERROR = "Error #1009: Cannot access a property or method of a null object reference.";
	std::string error;
	bool ok = false;
	Slot result;
	result.bits = 0;
	Kind kind = Kind::Void;
	uint32_t target = NONE;
	// What the engine gives must be what the register was declared to hold.
	auto wanted = [&](uint32_t p_target) {
		Host::Wanted want;
		if (p_target != NONE) {
			want.kind = p_function.registers[p_target];
			if (p_target < p_function.engine_types.size()) {
				want.type = p_function.engine_types[p_target];
			}
		}
		return want;
	};
	Host::Argument arguments[16];
	std::vector<Host::Argument> many;
	Host::Argument *values = arguments;
	size_t count = 0;
	auto gather = [&](const std::vector<uint32_t> &p_list, size_t p_from) {
		count = p_list.size() - p_from;
		if (count > 16) {
			many.resize(count);
			values = many.data();
		}
		for (size_t i = 0; i < count; i++) {
			values[i].kind = p_function.registers[p_list[p_from + i]];
			values[i].slot = r[p_list[p_from + i]];
		}
	};
	switch (p_in.op) {
		case Op::HostNew: {
			gather(p_function.argument_lists[p_in.c], 0);
			target = p_in.a;
			kind = Kind::Val;
			result.p = host_.construct(module_.strings[p_in.b], values, count, &error);
			ok = result.p != nullptr;
			break;
		}
		case Op::HostCall: {
			const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
			void *receiver = list.empty() ? nullptr : r[list[0]].p;
			if (!receiver) {
				raise(module_.type_error_class, NULL_ERROR);
				return NULL_ERROR;
			}
			gather(list, 1);
			target = p_in.a;
			kind = target == NONE ? Kind::Void : p_function.registers[target];
			ok = host_.call(receiver, module_.strings[p_in.b], values, count, wanted(target), target == NONE ? nullptr : &result, &error);
			break;
		}
		case Op::HostOperate: {
			gather(p_function.argument_lists[p_in.c], 0);
			const std::string &op = module_.strings[p_in.b];
			// Only a comparison takes a missing value: `node == null`. Arithmetic on one is an error.
			if (op != "==" && op != "!=") {
				for (size_t i = 0; i < count; i++) {
					if (ir::is_engine(values[i].kind) && !values[i].slot.p) {
						raise(module_.type_error_class, NULL_ERROR);
						return NULL_ERROR;
					}
				}
			}
			target = p_in.a;
			kind = p_function.registers[target];
			ok = host_.operate(op, values, count, wanted(target), &result, &error);
			break;
		}
		case Op::HostSingleton: {
			target = p_in.a;
			kind = Kind::Ref;
			result.p = host_.singleton(module_.strings[p_in.b], &error);
			ok = result.p != nullptr;
			break;
		}
		case Op::HostConstant: {
			target = p_in.a;
			kind = p_function.registers[target];
			ok = host_.constant(module_.strings[p_in.b], module_.strings[p_in.c], wanted(target), &result, &error);
			break;
		}
		case Op::HostCallStatic: {
			gather(p_function.argument_lists[p_in.c], 0);
			// "Vector2.from_angle": the type, and the method.
			std::string_view name = module_.strings[p_in.b];
			size_t dot = name.rfind('.');
			target = p_in.a;
			kind = target == NONE ? Kind::Void : p_function.registers[target];
			ok = host_.call_static(name.substr(0, dot), name.substr(dot + 1), values, count, wanted(target), target == NONE ? nullptr : &result, &error);
			break;
		}
		case Op::HostToStr: {
			target = p_in.a;
			kind = Kind::Str;
			result.p = r[p_in.b].p ? host_.text(r[p_in.b].p) : nullptr;
			if (!result.p) {
				retain(null_text_);
				result.p = null_text_;
			}
			ok = true;
			break;
		}
		case Op::HostGet: {
			void *receiver = r[p_in.b].p;
			if (!receiver) {
				raise(module_.type_error_class, NULL_ERROR);
				return NULL_ERROR;
			}
			target = p_in.a;
			kind = p_function.registers[target];
			ok = host_.get(receiver, module_.strings[p_in.c], wanted(target), &result, &error);
			break;
		}
		case Op::HostBind: {
			Object *object = static_cast<Object *>(r[p_in.b].p);
			if (!object) {
				raise(module_.type_error_class, NULL_ERROR);
				return NULL_ERROR;
			}
			const ir::Class &klass = module_.classes[object->klass];
			uint32_t function = (p_in.c & ir::BIND_SLOT) ? klass.methods[p_in.c & ~ir::BIND_SLOT] : p_in.c;
			target = p_in.a;
			kind = Kind::Val;
			result.p = host_.bind(object, function, klass.native_base.empty() ? nullptr : object->fields()[0].p, &error);
			ok = result.p != nullptr;
			break;
		}
		case Op::HostMember: {
			void *receiver = r[p_in.b].p;
			const Str *name = static_cast<const Str *>(r[p_in.c].p);
			if (!receiver || !name) {
				raise(module_.type_error_class, NULL_ERROR);
				return NULL_ERROR;
			}
			target = p_in.a;
			kind = p_function.registers[target];
			if (p_function.registers[p_in.b] == Kind::Obj) {
				// A script object: one of its own fields by that name, or else the Godot object behind it.
				Object *object = static_cast<Object *>(receiver);
				const ir::Class &klass = module_.classes[object->klass];
				bool own = false;
				for (size_t i = klass.fields.size(); i-- > 0 && !own;) {
					if (klass.fields[i].name == name->view()) {
						own = true;
						if (klass.fields[i].kind != Kind::Val) {
							error = "'" + std::string(name->view()) + "' is not a signal";
						} else {
							result.p = host_.copy(object->fields()[i].p);
							ok = true;
						}
					}
				}
				if (own) {
					break;
				}
				receiver = klass.native_base.empty() ? nullptr : object->fields()[0].p;
				if (!receiver) {
					error = "'" + std::string(name->view()) + "' is not a signal";
					break;
				}
			}
			ok = host_.member(receiver, name->view(), wanted(target), &result, &error);
			break;
		}
		case Op::HostIndexGet: {
			void *receiver = r[p_in.b].p;
			if (!receiver) {
				raise(module_.type_error_class, NULL_ERROR);
				return NULL_ERROR;
			}
			Host::Argument key;
			key.kind = p_function.registers[p_in.c];
			key.slot = r[p_in.c];
			target = p_in.a;
			kind = p_function.registers[target];
			ok = host_.index_get(receiver, key, wanted(target), &result, &error);
			break;
		}
		case Op::HostIndexSet: {
			void *receiver = r[p_in.a].p;
			if (!receiver) {
				raise(module_.type_error_class, NULL_ERROR);
				return NULL_ERROR;
			}
			Host::Argument key, value;
			key.kind = p_function.registers[p_in.b];
			key.slot = r[p_in.b];
			value.kind = p_function.registers[p_in.c];
			value.slot = r[p_in.c];
			ok = host_.index_set(receiver, key, value, &error);
			break;
		}
		default: {
			void *receiver = r[p_in.a].p;
			if (!receiver) {
				raise(module_.type_error_class, NULL_ERROR);
				return NULL_ERROR;
			}
			Host::Argument value;
			value.kind = p_function.registers[p_in.c];
			value.slot = r[p_in.c];
			ok = host_.set(receiver, module_.strings[p_in.b], value, &error);
			break;
		}
	}
	if (!ok) {
		raise(module_.error_class, error.empty() ? "the engine refused" : error);
		return "refused";
	}
	if (target != NONE && frame_index < frames_.size()) {
		put(kind, stack_[frames_[frame_index].base + target], result);
	}
	return nullptr;
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

bool Interpreter::get_field(Object *p_object, std::string_view p_name, Slot *r_value, Kind *r_kind) const {
	if (!p_object) {
		return false;
	}
	const ir::Class &klass = module_.classes[p_object->klass];
	// The class's own field hides a base's of the same name: the last one wins.
	for (size_t i = klass.fields.size(); i-- > 0;) {
		if (klass.fields[i].name == p_name) {
			*r_value = p_object->fields()[i];
			if (r_kind) {
				*r_kind = klass.fields[i].kind;
			}
			return true;
		}
	}
	return false;
}

Object *Interpreter::construct(uint32_t p_class, void *p_owner) {
	if (p_class >= module_.classes.size()) {
		return nullptr;
	}
	const ir::Class &klass = module_.classes[p_class];
	if (!use_class_now(p_class)) {
		if (p_owner) {
			host_.drop(p_owner);
		}
		return nullptr;
	}
	Object *object = new_object(p_class, klass.fields.size());
	if (!object) {
		if (p_owner) {
			host_.drop(p_owner);
		}
		return nullptr;
	}
	for (size_t i = 0; i < klass.fields.size(); i++) {
		if (klass.fields[i].kind == Kind::F64) {
			object->fields()[i].d = std::numeric_limits<double>::quiet_NaN();
		}
	}
	if (!klass.native_base.empty() && !klass.fields.empty() && klass.fields[0].kind == Kind::Ref && p_owner) {
		object->fields()[0].p = p_owner; // The object's own from here on.
	} else if (p_owner) {
		host_.drop(p_owner);
	}
	Slot self;
	self.bits = 0;
	self.p = object;
	if (klass.constructor != NONE && !call(klass.constructor, &self, 1)) {
		release_object(object);
		return nullptr;
	}
	return object;
}

uint32_t Interpreter::find_method(uint32_t p_class, std::string_view p_name) const {
	if (p_class >= methods_.size()) {
		return NONE;
	}
	auto found = methods_[p_class].find(std::string(p_name));
	return found == methods_[p_class].end() ? NONE : found->second;
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

void Interpreter::release_object(Object *p_object) {
	if (release(p_object)) {
		drop_object(p_object);
	}
}

// Runs until the frame above p_depth returns. False if an error left it instead.
bool Interpreter::run(size_t p_depth, Slot *r_result) {
	while (frames_.size() > p_depth) {
		if (pending_ || stopped_) {
			unwind(p_depth);
			return false;
		}
		Frame &frame = frames_.back();
		const Function &function = module_.functions[frame.function];
		Slot *r = stack_.data() + frame.base;
		const Instruction &in = function.code[frame.pc++];
		switch (in.op) {
			case Op::Stmt:
				if (hook_) {
					frame.pc--; // The hook sees the statement about to run.
					bool carry_on = hook_(*this, hook_data_);
					frames_.back().pc++;
					if (!carry_on) {
						stopped_ = true;
						halted_ = true;
					}
				}
				break;
			case Op::ConstI32:
				r[in.a].i = (int32_t)in.b;
				break;
			case Op::ConstU32:
				r[in.a].u = in.b;
				break;
			case Op::ConstF64:
				r[in.a].d = module_.numbers[in.b];
				break;
			case Op::ConstBool:
				r[in.a].bits = 0;
				r[in.a].b = in.b != 0;
				break;
			case Op::ConstStr:
				retain(strings_[in.b]);
				assign(Kind::Str, r[in.a], strings_[in.b]);
				break;
			case Op::ConstNull:
				drop(function.registers[in.a], r[in.a]);
				break;
			case Op::Move: {
				Kind kind = function.registers[in.a];
				put(kind, r[in.a], share(kind, r[in.b]));
				break;
			}
			case Op::I32ToF64:
				r[in.a].d = r[in.b].i;
				break;
			case Op::U32ToF64:
				r[in.a].d = r[in.b].u;
				break;
			case Op::BoolToF64:
				r[in.a].d = r[in.b].b ? 1 : 0;
				break;
			case Op::StrToF64: {
				Str *string = static_cast<Str *>(r[in.b].p);
				r[in.a].d = string ? string_to_number(string->view()) : 0;
				break;
			}
			case Op::F64ToI32:
				r[in.a].i = to_int32(r[in.b].d);
				break;
			case Op::ConstI64:
				r[in.a].l = module_.wides[in.b];
				break;
			case Op::AddI64:
				r[in.a].l = (int64_t)((uint64_t)r[in.b].l + (uint64_t)r[in.c].l);
				break;
			case Op::SubI64:
				r[in.a].l = (int64_t)((uint64_t)r[in.b].l - (uint64_t)r[in.c].l);
				break;
			case Op::MulI64:
				r[in.a].l = (int64_t)((uint64_t)r[in.b].l * (uint64_t)r[in.c].l);
				break;
			case Op::ModI64:
				if (r[in.c].l == 0) {
					raise(module_.error_class, "the remainder of an Int64 by zero is not a number.");
				} else {
					r[in.a].l = r[in.c].l == -1 ? 0 : r[in.b].l % r[in.c].l; // The least Int64 by -1 would trap.
				}
				break;
			case Op::NegI64:
				r[in.a].l = (int64_t)(0 - (uint64_t)r[in.b].l);
				break;
			case Op::BitAndI64:
				r[in.a].l = r[in.b].l & r[in.c].l;
				break;
			case Op::BitOrI64:
				r[in.a].l = r[in.b].l | r[in.c].l;
				break;
			case Op::BitXorI64:
				r[in.a].l = r[in.b].l ^ r[in.c].l;
				break;
			case Op::BitNotI64:
				r[in.a].l = ~r[in.b].l;
				break;
			case Op::ShlI64:
				r[in.a].l = (int64_t)((uint64_t)r[in.b].l << ((uint64_t)r[in.c].l & 63));
				break;
			case Op::ShrI64: {
				// Written out, so that the sign is carried whatever the compiler does with >> on a negative.
				uint64_t count = (uint64_t)r[in.c].l & 63;
				uint64_t bits = (uint64_t)r[in.b].l >> count;
				if (r[in.b].l < 0 && count > 0) {
					bits |= ~(uint64_t)0 << (64 - count);
				}
				r[in.a].l = (int64_t)bits;
				break;
			}
			case Op::UShrI64:
				r[in.a].l = (int64_t)((uint64_t)r[in.b].l >> ((uint64_t)r[in.c].l & 63));
				break;
			case Op::LtI64: {
				bool value = r[in.b].l < r[in.c].l;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::LeI64: {
				bool value = r[in.b].l <= r[in.c].l;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::EqI64: {
				bool value = r[in.b].l == r[in.c].l;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::CmpI64F64:
				r[in.a].d = compare_wide(r[in.b].l, r[in.c].d);
				break;
			case Op::I32ToI64:
				r[in.a].l = r[in.b].i;
				break;
			case Op::U32ToI64:
				r[in.a].l = r[in.b].u;
				break;
			case Op::I64ToBool: {
				bool value = r[in.b].l != 0;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::I64ToStr:
				assign(Kind::Str, r[in.a], text(std::to_string(r[in.b].l)));
				break;
			case Op::I64ToI32: {
				int64_t value = r[in.b].l;
				if (value < INT32_MIN || value > INT32_MAX) {
					raise(module_.range_error_class, std::to_string(value) + " does not fit an int.");
				} else {
					r[in.a].bits = 0;
					r[in.a].i = (int32_t)value;
				}
				break;
			}
			case Op::I64ToU32: {
				int64_t value = r[in.b].l;
				if (value < 0 || value > (int64_t)UINT32_MAX) {
					raise(module_.range_error_class, std::to_string(value) + " does not fit a uint.");
				} else {
					r[in.a].bits = 0;
					r[in.a].u = (uint32_t)value;
				}
				break;
			}
			case Op::I64ToF64: {
				int64_t value = r[in.b].l;
				double number = (double)value;
				// The Number must hold the very same value. 2^63 is checked first: converting it back is undefined.
				if (number >= 9223372036854775808.0 || (int64_t)number != value) {
					raise(module_.range_error_class, std::to_string(value) + " does not fit a Number exactly.");
				} else {
					r[in.a].d = number;
				}
				break;
			}
			case Op::F64ToI64: {
				double number = r[in.b].d;
				if (!(number >= -9223372036854775808.0 && number < 9223372036854775808.0)) {
					raise(module_.range_error_class, number_to_string(number) + " does not fit an Int64.");
				} else {
					r[in.a].l = (int64_t)number; // The fraction is dropped, as for an int.
				}
				break;
			}
			case Op::I64WrapI32: {
				int64_t value = r[in.b].l;
				r[in.a].bits = 0;
				r[in.a].i = (int32_t)(uint32_t)(uint64_t)value;
				break;
			}
			case Op::I64WrapU32: {
				int64_t value = r[in.b].l;
				r[in.a].bits = 0;
				r[in.a].u = (uint32_t)(uint64_t)value;
				break;
			}
			case Op::I64RoundF64:
				r[in.a].d = (double)r[in.b].l;
				break;
			case Op::F64ClampI64:
				r[in.a].l = clamp_wide(r[in.b].d);
				break;
			case Op::StrToI64: {
				Str *string = static_cast<Str *>(r[in.b].p);
				r[in.a].l = string ? string_to_wide(string->view()) : 0;
				break;
			}
			case Op::F64ToU32:
				r[in.a].u = to_uint32(r[in.b].d);
				break;
			case Op::I32ToU32:
				r[in.a].u = (uint32_t)r[in.b].i;
				break;
			case Op::U32ToI32:
				r[in.a].i = (int32_t)r[in.b].u;
				break;
			case Op::I32ToBool: {
				bool value = r[in.b].i != 0;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::U32ToBool: {
				bool value = r[in.b].u != 0;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::F64ToBool: {
				double number = r[in.b].d;
				r[in.a].bits = 0;
				r[in.a].b = !(number == 0 || std::isnan(number));
				break;
			}
			case Op::StrToBool: {
				Str *string = static_cast<Str *>(r[in.b].p);
				bool value = string && string->size > 0;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::ObjToBool: {
				bool value = r[in.b].p != nullptr;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::I32ToStr:
				assign(Kind::Str, r[in.a], text(std::to_string(r[in.b].i)));
				break;
			case Op::U32ToStr:
				assign(Kind::Str, r[in.a], text(std::to_string(r[in.b].u)));
				break;
			case Op::F64ToStr:
				assign(Kind::Str, r[in.a], text(number_to_string(r[in.b].d)));
				break;
			case Op::BoolToStr:
				assign(Kind::Str, r[in.a], text(r[in.b].b ? "true" : "false"));
				break;
			case Op::ObjToStr: {
				Object *object = static_cast<Object *>(r[in.b].p);
				uint32_t own = object ? module_.classes[object->klass].to_string : NONE;
				if (own != NONE) {
					// The class says how it reads: call its toString(), whose result lands in the register.
					Slot self = r[in.b];
					push(own, &self, 1, in.a); // `frame` and `r` are stale after this.
				} else {
					assign(Kind::Str, r[in.a], text(object_text(object)));
				}
				break;
			}
			case Op::StrToStr: {
				Str *string = r[in.b].p ? static_cast<Str *>(r[in.b].p) : null_text_;
				retain(string);
				assign(Kind::Str, r[in.a], string);
				break;
			}
			case Op::AddF64:
				r[in.a].d = r[in.b].d + r[in.c].d;
				break;
			case Op::SubF64:
				r[in.a].d = r[in.b].d - r[in.c].d;
				break;
			case Op::MulF64:
				r[in.a].d = r[in.b].d * r[in.c].d;
				break;
			case Op::DivF64:
				r[in.a].d = r[in.b].d / r[in.c].d;
				break;
			case Op::ModF64:
				r[in.a].d = std::fmod(r[in.b].d, r[in.c].d);
				break;
			case Op::NegF64:
				r[in.a].d = -r[in.b].d;
				break;
			case Op::BitAnd:
				r[in.a].i = r[in.b].i & r[in.c].i;
				break;
			case Op::BitOr:
				r[in.a].i = r[in.b].i | r[in.c].i;
				break;
			case Op::BitXor:
				r[in.a].i = r[in.b].i ^ r[in.c].i;
				break;
			case Op::Shl:
				r[in.a].i = (int32_t)(r[in.b].u << (r[in.c].u & 31));
				break;
			case Op::Shr:
				r[in.a].i = r[in.b].i >> (r[in.c].u & 31);
				break;
			case Op::UShr:
				r[in.a].u = r[in.b].u >> (r[in.c].u & 31);
				break;
			case Op::BitNot:
				r[in.a].i = ~r[in.b].i;
				break;
			case Op::LtF64: {
				bool value = r[in.b].d < r[in.c].d;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::LeF64: {
				bool value = r[in.b].d <= r[in.c].d;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::EqF64: {
				bool value = r[in.b].d == r[in.c].d;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::LtStr:
			case Op::LeStr:
			case Op::EqStr: {
				Str *left = static_cast<Str *>(r[in.b].p);
				Str *right = static_cast<Str *>(r[in.c].p);
				bool value;
				if (in.op == Op::EqStr) {
					value = left == right || (left && right && left->view() == right->view());
				} else if (left && right) {
					value = in.op == Op::LtStr ? left->view() < right->view() : left->view() <= right->view();
				} else {
					// With a missing string the two are compared as numbers: null is 0.
					double a = left ? string_to_number(left->view()) : 0;
					double b = right ? string_to_number(right->view()) : 0;
					value = in.op == Op::LtStr ? a < b : a <= b;
				}
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::EqBool: {
				bool value = r[in.b].b == r[in.c].b;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::EqRef: {
				bool value = r[in.b].p == r[in.c].p;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::Not: {
				bool value = !r[in.b].b;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::Concat: {
				Str *left = static_cast<Str *>(r[in.b].p);
				Str *right = static_cast<Str *>(r[in.c].p);
				std::string joined;
				joined.reserve((left ? left->size : 4) + (right ? right->size : 4));
				joined += left ? left->view() : std::string_view("null");
				joined += right ? right->view() : std::string_view("null");
				assign(Kind::Str, r[in.a], text(joined));
				break;
			}
			case Op::StrLength: {
				Str *string = static_cast<Str *>(r[in.b].p);
				r[in.a].i = string ? character_count(string->view()) : 0;
				break;
			}
			case Op::Jump:
				frame.pc = in.a;
				break;
			case Op::JumpIf:
				if (r[in.a].b) {
					frame.pc = in.b;
				}
				break;
			case Op::JumpIfNot:
				if (!r[in.a].b) {
					frame.pc = in.b;
				}
				break;
			case Op::GetField: {
				Object *object = static_cast<Object *>(r[in.b].p);
				if (!object) {
					raise(module_.type_error_class, "Error #1009: Cannot access a property or method of a null object reference.");
					break;
				}
				Kind kind = function.registers[in.a];
				put(kind, r[in.a], share(kind, object->fields()[in.c]));
				break;
			}
			case Op::SetField: {
				Object *object = static_cast<Object *>(r[in.a].p);
				if (!object) {
					raise(module_.type_error_class, "Error #1009: Cannot access a property or method of a null object reference.");
					break;
				}
				Kind kind = module_.classes[object->klass].fields[in.b].kind;
				put(kind, object->fields()[in.b], share(kind, r[in.c]));
				break;
			}
			case Op::GetGlobal: {
				Kind kind = module_.globals[in.b].kind;
				put(kind, r[in.a], share(kind, globals_[in.b]));
				break;
			}
			case Op::SetGlobal: {
				Kind kind = module_.globals[in.a].kind;
				put(kind, globals_[in.a], share(kind, r[in.b]));
				break;
			}
			case Op::ArgMissing: {
				bool value = in.b >= frame.arguments;
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::InitClass:
				if (!class_used_[in.a]) {
					use_class(in.a); // What it pushes runs before the next instruction of this frame.
				}
				break;
			case Op::NullCheck:
				if (!r[in.a].p) {
					raise(module_.type_error_class, "Error #1009: Cannot access a property or method of a null object reference.");
				}
				break;
			case Op::New: {
				const ir::Class &klass = module_.classes[in.b];
				Object *object = new_object(in.b, klass.fields.size());
				if (!object) {
					raise(module_.error_class, "Error #1000: The system is out of memory.");
					break;
				}
				for (size_t i = 0; i < klass.fields.size(); i++) {
					if (klass.fields[i].kind == Kind::F64) {
						object->fields()[i].d = std::numeric_limits<double>::quiet_NaN();
					}
				}
				assign(Kind::Obj, r[in.a], object);
				break;
			}
			case Op::Call:
			case Op::CallVirtual: {
				const std::vector<uint32_t> &list = function.argument_lists[in.c];
				uint32_t target = in.b;
				if (in.op == Op::CallVirtual) {
					Object *object = list.empty() ? nullptr : static_cast<Object *>(r[list[0]].p);
					if (!object) {
						raise(module_.type_error_class, "Error #1009: Cannot access a property or method of a null object reference.");
						break;
					}
					target = module_.classes[object->klass].methods[in.b];
				}
				Slot arguments[16];
				std::vector<Slot> many;
				Slot *values = arguments;
				if (list.size() > 16) {
					many.resize(list.size());
					values = many.data();
				}
				for (size_t i = 0; i < list.size(); i++) {
					values[i] = r[list[i]];
				}
				push(target, values, list.size(), in.a); // `frame` and `r` are stale after this.
				break;
			}
			case Op::IsClass: {
				bool value = is_class(static_cast<Object *>(r[in.b].p), in.c);
				r[in.a].bits = 0;
				r[in.a].b = value;
				break;
			}
			case Op::AsClass: {
				Object *object = static_cast<Object *>(r[in.b].p);
				if (!is_class(object, in.c)) {
					object = nullptr;
				}
				retain(object);
				assign(Kind::Obj, r[in.a], object);
				break;
			}
			case Op::Return:
			case Op::ReturnVoid: {
				Slot value;
				value.bits = 0;
				Kind kind = function.result;
				if (in.op == Op::Return) {
					value = share(kind, r[in.a]);
				}
				uint32_t result = frame.result;
				pop();
				if (in.op == Op::Return && result != NONE && frames_.size() > p_depth) {
					const Frame &caller = frames_.back();
					put(kind, stack_[caller.base + result], value);
				} else if (in.op == Op::Return && frames_.size() == p_depth && r_result) {
					*r_result = value; // The caller from outside takes it, with its reference.
				} else if (in.op == Op::Return && held(kind)) {
					drop(kind, value); // Nobody wants it.
				}
				break;
			}
			case Op::Throw: {
				Object *thrown = static_cast<Object *>(r[in.a].p);
				if (!thrown) {
					raise(module_.type_error_class, "Error #1009: Cannot access a property or method of a null object reference.");
					break;
				}
				retain(thrown);
				pending_ = thrown;
				break;
			}
			case Op::HostNew:
			case Op::HostCall:
			case Op::HostGet:
			case Op::HostOperate:
			case Op::HostToStr:
			case Op::HostSingleton:
			case Op::HostConstant:
			case Op::HostCallStatic:
			case Op::HostBind:
			case Op::HostMember:
			case Op::HostIndexGet:
			case Op::HostIndexSet:
			case Op::HostSet: {
				// The engine may call back into AS3 before it answers, which can move the registers: nothing
				// read before the call is used after it.
				const char *failure = host_op(function, in);
				(void)failure;
				break;
			}
			case Op::Trace: {
				const std::vector<uint32_t> &list = function.argument_lists[in.a];
				std::string line;
				for (size_t i = 0; i < list.size(); i++) {
					Str *string = static_cast<Str *>(r[list[i]].p);
					line += i ? " " : "";
					line += string ? string->view() : std::string_view("null");
				}
				host_.print(line);
				break;
			}
		}
		if (pending_ || stopped_) {
			// The instruction that failed is the one before pc: point the frame back at it for the trace.
			if (!frames_.empty() && frames_.back().pc > 0 && frames_.size() > p_depth) {
				frames_.back().pc--;
			}
			unwind(p_depth);
			return false;
		}
	}
	return true;
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
