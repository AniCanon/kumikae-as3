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
		host_.drop(p_slot.p);
	}
	p_slot.bits = 0;
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
				host_.drop(field.p);
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
	Frame &frame = frames_.back();
	const Function &function = module_.functions[frame.function];
	for (size_t i = 0; i < function.registers.size(); i++) {
		if (held(function.registers[i])) {
			drop(function.registers[i], stack_[frame.base + i]);
		}
	}
	stack_.resize(frame.base);
	frames_.pop_back();
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
			ok = host_.call(receiver, module_.strings[p_in.b], values, count, kind, target == NONE ? nullptr : &result, &error);
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
			ok = host_.get(receiver, module_.strings[p_in.c], kind, &result, &error);
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
	Object *object = new_object(p_class, klass.fields.size());
	if (!object) {
		return nullptr;
	}
	for (size_t i = 0; i < klass.fields.size(); i++) {
		if (klass.fields[i].kind == Kind::F64) {
			object->fields()[i].d = std::numeric_limits<double>::quiet_NaN();
		}
	}
	if (!klass.native_base.empty() && !klass.fields.empty() && klass.fields[0].kind == Kind::Ref && p_owner) {
		object->fields()[0].p = host_.copy(p_owner);
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
