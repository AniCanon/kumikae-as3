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

// Runs until the frame above p_depth returns. False if an error left it instead.
// Two strings in order, or equal, as AS3 compares them. With a missing string the two are compared as
// numbers: null is 0.
static bool compare_strings(Op p_op, const Str *p_left, const Str *p_right) {
	if (p_op == Op::EqStr) {
		return p_left == p_right || (p_left && p_right && p_left->view() == p_right->view());
	}
	if (p_left && p_right) {
		const int order = compare_utf16(p_left->view(), p_right->view());
		return p_op == Op::LtStr ? order < 0 : order <= 0;
	}
	const double left = p_left ? string_to_number(p_left->view()) : 0;
	const double right = p_right ? string_to_number(p_right->view()) : 0;
	return p_op == Op::LtStr ? left < right : left <= right;
}


// An Int64 where an int, a uint or a Number is expected, or a Number where an Int64 is, when the
// program did not write the conversion: the value, or a RangeError that names it.
void Interpreter::convert_checked(const Instruction &p_in, Slot *p_registers) {
	Slot &out = p_registers[p_in.a];
	const Slot &from = p_registers[p_in.b];
	switch (p_in.op) {
		case Op::I64ToI32:
			if (const std::optional<int32_t> fitted = wide_to_int32(from.l)) {
				set_int32(out, *fitted);
			} else {
				raise(module_.range_error_class, std::to_string(from.l) + " does not fit an int.");
			}
			break;
		case Op::I64ToU32:
			if (const std::optional<uint32_t> fitted = wide_to_uint32(from.l)) {
				set_uint32(out, *fitted);
			} else {
				raise(module_.range_error_class, std::to_string(from.l) + " does not fit a uint.");
			}
			break;
		case Op::I64ToF64:
			if (const std::optional<double> fitted = wide_to_number(from.l)) {
				out.d = *fitted;
			} else {
				raise(module_.range_error_class, std::to_string(from.l) + " does not fit a Number exactly.");
			}
			break;
		default:
			if (const std::optional<int64_t> fitted = number_to_wide(from.d)) {
				out.l = *fitted;
			} else {
				raise(module_.range_error_class, number_to_string(from.d) + " does not fit an Int64.");
			}
			break;
	}
}

// A statement is about to run: once in a while the clock is looked at, and the debugger's hook, if there is
// one, sees it and may stop the program.
void Interpreter::at_statement() {
	if (--countdown_ == 0) {
		look_at_clock();
	}
	if (!hook_) {
		return;
	}
	frames_.back().pc--; // The hook sees the statement about to run.
	const bool carry_on = hook_(*this, hook_data_);
	frames_.back().pc++;
	if (time_limit_) {
		entered_ = std::chrono::steady_clock::now(); // A debugger's stop is not the program's time.
	}
	if (!carry_on) {
		stopped_ = true;
		halted_ = true;
	}
}

// A run that has gone on too long ends with Flash's error, which nothing catches.
void Interpreter::look_at_clock() {
	countdown_ = STATEMENTS_PER_LOOK;
	if (!time_limit_ || failing_ || stopped_ || std::chrono::steady_clock::now() - entered_ <= std::chrono::milliseconds(time_limit_)) {
		return;
	}
	raise(module_.error_class, timeout_message(time_limit_));
	stopped_ = true;
}

void Interpreter::entering() {
	if (entries_ == 0 && time_limit_) {
		entered_ = std::chrono::steady_clock::now();
		countdown_ = STATEMENTS_PER_LOOK;
	}
}

std::string timeout_message(uint32_t p_milliseconds) {
	return std::string("Error #1502: A script has executed for longer than the ") +
			(p_milliseconds == Interpreter::FLASH_TIME_LIMIT ? "default " : "") + "timeout period of " +
			number_to_string(p_milliseconds / 1000.0) + " seconds.";
}

// A new object of a class, its Number fields not yet a number.
void Interpreter::make(uint32_t p_class, Slot &r_out) {
	const ir::Class &klass = module_.classes[p_class];
	Object *object = new_object(p_class, klass.fields.size(), collector_.list_for(p_class));
	if (!object) {
		out_of_memory();
		return;
	}
	for (size_t i = 0; i < klass.fields.size(); i++) {
		object->fields()[i] = empty_value(klass.fields[i].kind == Kind::F64, klass.fields[i].starts_null);
	}
	if (!klass.native_base.empty() && !klass.fields.empty() && klass.fields[0].kind == Kind::Ref) {
		// A script class that extends one of Godot's: the Godot object is made, with the script attached.
		std::string error;
		void *owner = host_.source.attach_new(p_class, object, &error);
		if (!owner) {
			release_object(object);
			raise(module_.error_class, error.empty() ? "the engine could not make a '" + klass.name + "'" : error);
			return;
		}
		object->fields()[0].p = owner;
	}
	assign(Kind::Obj, r_out, object);
}

// A call: the function named, or for a virtual call the one the object's own class has in that slot.
// The arguments are copied out of their registers first, since the new frame may move the stack.
void Interpreter::enter(const Function &p_function, const Instruction &p_in, const Slot *p_registers) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	uint32_t target = p_in.b;
	if (p_in.op == Op::CallVirtual) {
		const Object *object = list.empty() ? nullptr : static_cast<const Object *>(p_registers[list[0]].p);
		if (!object) {
			raise_null();
			return;
		}
		target = module_.classes[object->klass].methods[p_in.b];
	} else if (p_in.op == Op::CallInterface) {
		target = interface_function(list.empty() ? nullptr : static_cast<const Object *>(p_registers[list[0]].p), p_in.b);
		if (target == NONE) {
			return;
		}
	}
	Slot few[16];
	std::vector<Slot> many;
	Slot *values = few;
	if (list.size() > 16) {
		many.resize(list.size());
		values = many.data();
	}
	for (size_t i = 0; i < list.size(); i++) {
		values[i] = p_registers[list[i]];
	}
	push(target, values, list.size(), p_in.a);
}

// A function is done. Its value, if it gives one, goes to the caller's register, or to whoever called in
// from outside, or nowhere.
void Interpreter::leave(const Function &p_function, const Instruction &p_in, const Slot *p_registers, size_t p_depth, Slot *r_result) {
	Slot value;
	value.bits = 0;
	const Kind kind = p_function.result;
	const bool gives = p_in.op == Op::Return;
	if (gives) {
		value = share(kind, p_registers[p_in.a]);
	}
	const uint32_t result = frames_.back().result;
	pop();
	if (gives && result != NONE && frames_.size() > p_depth) {
		const Frame &caller = frames_.back();
		const Kind wanted = module_.functions[caller.function].registers[result];
		if (wanted == Kind::Any && kind != Kind::Any) {
			// Called through a function value: what it gives is untyped.
			Slot typed = value;
			value = to_any(kind, typed);
			drop(kind, typed);
		}
		put(wanted, stack_[caller.base + result], value);
	} else if (gives && frames_.size() == p_depth && r_result) {
		*r_result = value; // The caller from outside takes it, with its reference.
	} else if (gives && held(kind)) {
		drop(kind, value); // Nobody wants it.
	}
}

// An object as text. A class that says how it reads has its toString() called, whose result lands in
// the register.
void Interpreter::object_to_text(const Instruction &p_in, Slot *p_registers) {
	Object *object = static_cast<Object *>(p_registers[p_in.b].p);
	const uint32_t own = object ? module_.classes[object->klass].to_string : NONE;
	if (object && module_.classes[object->klass].element != Kind::Void) {
		// A vector's elements, each in its own words: that may run the program and move the registers.
		retain(object);
		std::string all;
		const bool done = vector_text(static_cast<Vector *>(object), ",", &all);
		if (done) {
			assign(Kind::Str, stack_[frames_.back().base + p_in.a], text(all));
		}
		release_object(object);
	} else if (own != NONE) {
		Slot self = p_registers[p_in.b];
		push(own, &self, 1, p_in.a);
	} else {
		assign(Kind::Str, p_registers[p_in.a], text(object_text(object)));
	}
}

// An object as a Number: what an untyped value holding it converts to (a Date's time; null is 0).
void Interpreter::object_to_number(const Instruction &p_in, Slot *p_registers) {
	const Slot value = to_any(Kind::Obj, p_registers[p_in.b]);
	Slot number;
	const bool done = from_any(value, Kind::F64, NONE, &number); // An object's text may run the program.
	drop_any(value);
	if (done) {
		stack_[frames_.back().base + p_in.a].d = number.d;
	}
}

void Interpreter::trace(const std::vector<uint32_t> &p_list, const Slot *p_registers) {
	std::string line;
	for (size_t i = 0; i < p_list.size(); i++) {
		const Str *string = static_cast<const Str *>(p_registers[p_list[i]].p);
		line += i ? " " : "";
		line += string ? string->view() : std::string_view("null");
	}
	host_.output.print(line);
}

bool Interpreter::run(size_t p_depth, Slot *r_result) {
	while (frames_.size() > p_depth) {
		if (failing_ || stopped_) {
			if (catch_error(p_depth)) {
				continue;
			}
			return false;
		}
		Frame &frame = frames_.back();
		const Function &function = module_.functions[frame.function];
		Slot *r = stack_.data() + frame.base;
		const Instruction &in = function.code[frame.pc++];
		// A Boolean result: the whole register is cleared, then the flag set. (Written here, not as a
		// function in a header: measured, that form makes the loop about 3% slower.)
		const auto flag = [&](bool p_value) {
			r[in.a].bits = 0;
			r[in.a].b = p_value;
		};
		switch (in.op) {
			case Op::Stmt:
				at_statement();
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
				flag(in.b != 0);
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
			case Op::ShrI64:
				r[in.a].l = shift_right_wide(r[in.b].l, r[in.c].l);
				break;
			case Op::UShrI64:
				r[in.a].l = (int64_t)((uint64_t)r[in.b].l >> ((uint64_t)r[in.c].l & 63));
				break;
			case Op::LtI64:
				flag(r[in.b].l < r[in.c].l);
				break;
			case Op::LeI64:
				flag(r[in.b].l <= r[in.c].l);
				break;
			case Op::EqI64:
				flag(r[in.b].l == r[in.c].l);
				break;
			case Op::CmpI64F64:
				r[in.a].d = compare_wide(r[in.b].l, r[in.c].d);
				break;
			case Op::CmpI64Str: {
				// Text that is a whole number an Int64 holds is compared exactly; any other as a Number.
				const Str *text = static_cast<const Str *>(r[in.c].p);
				const std::string_view view = text ? text->view() : std::string_view("0");
				const std::optional<int64_t> exact = string_to_wide_exact(view);
				r[in.a].d = exact ? (r[in.b].l < *exact ? -1 : r[in.b].l > *exact ? 1 : 0) : compare_wide(r[in.b].l, string_to_number(view));
				break;
			}
			case Op::I32ToI64:
				r[in.a].l = r[in.b].i;
				break;
			case Op::U32ToI64:
				r[in.a].l = r[in.b].u;
				break;
			case Op::I64ToBool:
				flag(r[in.b].l != 0);
				break;
			case Op::I64ToStr:
				assign(Kind::Str, r[in.a], text(std::to_string(r[in.b].l)));
				break;
			case Op::I64ToI32:
			case Op::I64ToU32:
			case Op::I64ToF64:
			case Op::F64ToI64:
				convert_checked(in, r);
				break;
			case Op::I64WrapI32:
				set_int32(r[in.a], (int32_t)(uint32_t)(uint64_t)r[in.b].l);
				break;
			case Op::I64WrapU32:
				set_uint32(r[in.a], (uint32_t)(uint64_t)r[in.b].l);
				break;
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
			case Op::StrToI64Checked: {
				Str *string = static_cast<Str *>(r[in.b].p);
				if (const std::optional<int64_t> exact = string ? string_to_wide_exact(string->view()) : std::optional<int64_t>(0)) {
					r[in.a].l = *exact;
				} else {
					raise(module_.range_error_class, "\"" + std::string(string->view()) + "\" does not fit an Int64.");
				}
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
			case Op::I32ToBool:
				flag(r[in.b].i != 0);
				break;
			case Op::U32ToBool:
				flag(r[in.b].u != 0);
				break;
			case Op::F64ToBool:
				flag(!(r[in.b].d == 0 || std::isnan(r[in.b].d)));
				break;
			case Op::StrToBool: {
				Str *string = static_cast<Str *>(r[in.b].p);
				flag(string && string->size > 0);
				break;
			}
			case Op::ObjToBool:
				flag(r[in.b].p != nullptr);
				break;
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
			case Op::ObjToStr:
				object_to_text(in, r); // `frame` and `r` may be stale after this.
				break;
			case Op::ObjToF64:
				object_to_number(in, r); // `frame` and `r` may be stale after this.
				break;
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
			case Op::MathUnary:
				r[in.a].d = math_unary(static_cast<ir::MathFunction>(in.c), r[in.b].d);
				break;
			case Op::Atan2:
				r[in.a].d = std::atan2(r[in.b].d, r[in.c].d);
				break;
			case Op::Pow:
				r[in.a].d = math_pow(r[in.b].d, r[in.c].d);
				break;
			case Op::MinF64:
				r[in.a].d = math_min(r[in.b].d, r[in.c].d);
				break;
			case Op::MaxF64:
				r[in.a].d = math_max(r[in.b].d, r[in.c].d);
				break;
			case Op::Random:
				r[in.a].d = math_random();
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
			case Op::LtF64:
				flag(r[in.b].d < r[in.c].d);
				break;
			case Op::LeF64:
				flag(r[in.b].d <= r[in.c].d);
				break;
			case Op::EqF64:
				flag(r[in.b].d == r[in.c].d);
				break;
			case Op::LtStr:
			case Op::LeStr:
			case Op::EqStr:
				flag(compare_strings(in.op, static_cast<Str *>(r[in.b].p), static_cast<Str *>(r[in.c].p)));
				break;
			case Op::EqBool:
				flag(r[in.b].b == r[in.c].b);
				break;
			case Op::EqRef:
				flag(r[in.b].p == r[in.c].p || same_function(static_cast<const Object *>(r[in.b].p), static_cast<const Object *>(r[in.c].p)));
				break;
			case Op::Not:
				flag(!r[in.b].b);
				break;
			case Op::Concat:
				// One string after the other; a missing one reads "null".
				if (Str *joined = join_strings(r[in.b].p ? static_cast<Str *>(r[in.b].p)->view() : std::string_view("null"),
							r[in.c].p ? static_cast<Str *>(r[in.c].p)->view() : std::string_view("null"))) {
					assign(Kind::Str, r[in.a], joined);
				} else {
					out_of_memory();
				}
				break;
			case Op::StrLength: {
				Str *string = static_cast<Str *>(r[in.b].p);
				r[in.a].i = string ? utf16_length(string->view()) : 0;
				break;
			}
			case Op::StrCall:
				string_call(function, in, r);
				break;
			case Op::NumText:
				number_text(function, in, r);
				break;
			case Op::RegExpCall:
				regexp_call(function, in); // `frame` and `r` may be stale after this.
				break;
			case Op::DateCall:
				date_call(function, in); // `frame` and `r` may be stale after this.
				break;
			case Op::JsonCall:
				json_call(function, in); // `frame` and `r` may be stale after this.
				break;
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
					raise_null();
					break;
				}
				Kind kind = function.registers[in.a];
				put(kind, r[in.a], share(kind, object->fields()[in.c]));
				break;
			}
			case Op::SetField: {
				Object *object = static_cast<Object *>(r[in.a].p);
				if (!object) {
					raise_null();
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
			case Op::ArgMissing:
				flag(in.b >= frame.arguments);
				break;
			case Op::InitClass:
				if (!class_used_[in.a]) {
					use_class(in.a); // What it pushes runs before the next instruction of this frame.
				}
				break;
			case Op::NullCheck:
				if (!r[in.a].p) {
					raise_null();
				}
				break;
			case Op::AnyNullCheck:
				any_null_check(r[in.a]);
				break;
			case Op::New:
				make(in.b, r[in.a]);
				break;
			case Op::VecNew:
			case Op::ArrayNew:
			case Op::VecGet:
			case Op::VecSet:
			case Op::VecLength:
			case Op::VecSetLength:
			case Op::VecFixed:
			case Op::VecSetFixed:
				vector_op(function, in, r);
				break;
			case Op::AnyConst:
			case Op::ToAny:
			case Op::FromAny:
			case Op::AnyText:
			case Op::AnyTypeof:
			case Op::AnyEq:
			case Op::AnyStrictEq:
			case Op::AnyAdd:
			case Op::AnyLt:
			case Op::AnyLe:
			case Op::AnyGt:
			case Op::AnyGe:
			case Op::AnyIs:
			case Op::AnyIsGodot:
				any_op(function, in, r); // `frame` and `r` may be stale after this.
				break;
			case Op::VecCall:
				vector_call(function, in); // `frame` and `r` may be stale after this.
				break;
			case Op::TableNew:
			case Op::DynGet:
			case Op::DynSet:
			case Op::DynDelete:
			case Op::DynIn:
			case Op::DynCall:
			case Op::DynKeys:
			case Op::DynNext:
			case Op::DynKey:
				dynamic_op(function, in); // `frame` and `r` are stale after this.
				break;
			case Op::Call:
			case Op::CallVirtual:
			case Op::CallInterface:
				enter(function, in, r); // `frame` and `r` are stale after this.
				break;
			case Op::IsClass:
				flag(is_class(static_cast<Object *>(r[in.b].p), in.c));
				break;
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
			case Op::ReturnVoid:
				leave(function, in, r, p_depth, r_result); // `frame` and `r` are stale after this.
				break;
			case Op::ObjectText:
				assign(Kind::Str, r[in.a], text(object_text(static_cast<Object *>(r[in.b].p))));
				break;
			case Op::ErrorStack: {
				// In an Error's constructor: the frames below it, and below any constructor of an error
				// class that called it with `super`.
				Object *error = static_cast<Object *>(r[in.b].p);
				size_t skip = 0;
				const auto error_constructor = [&](uint32_t p_function) {
					const uint32_t owner = module_.functions[p_function].owner;
					if (owner == NONE || module_.classes[owner].constructor != p_function) {
						return false;
					}
					for (uint32_t at = owner; at != NONE; at = module_.classes[at].base) {
						if (at == module_.error_class) {
							return true;
						}
					}
					return false;
				};
				while (skip < frames_.size() && error_constructor(frames_[frames_.size() - 1 - skip].function)) {
					skip++;
				}
				assign(Kind::Str, r[in.a], stack_text(error, skip));
				break;
			}
			case Op::MakeFunction:
			case Op::MakeMethod:
			case Op::MakeInterfaceMethod:
			case Op::CallFunction:
			case Op::FunctionToCallable:
			case Op::CallbackThis:
			case Op::ApplyFunction:
			case Op::FunctionLength:
				function_op(function, in); // `frame` and `r` may be stale after this.
				break;
			case Op::OwnerOf: {
				Object *object = static_cast<Object *>(r[in.b].p);
				const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
				if (klass && (klass->native_base.empty() || klass->fields.empty())) {
					raise(module_.type_error_class, "Error #1034: Type Coercion failed: cannot convert a " + klass->name +
									" to one of Godot's objects: its class extends none of Godot's classes.");
					break;
				}
				Slot none;
				none.bits = 0; // All of it: `{}` on a union clears its first member only.
				put(Kind::Ref, r[in.a], object ? share(Kind::Ref, object->fields()[0]) : none);
				break;
			}
			case Op::Throw:
				pending_ = share(Kind::Any, r[in.a]); // Any value, null too.
				failing_ = true;
				break;
			case Op::Rethrow:
				pending_ = share(Kind::Any, r[in.a]);
				failing_ = true;
				take_trace(in.a);
				break;
			case Op::HostNew:
			case Op::HostCall:
			case Op::HostGet:
			case Op::HostOperate:
			case Op::HostToStr:
			case Op::HostPresent:
			case Op::HostIsClass:
			case Op::HostAsClass:
			case Op::HostSingleton:
			case Op::HostDefault:
			case Op::HostConstant:
			case Op::HostCallStatic:
			case Op::HostCollection:
			case Op::HostBind:
			case Op::HostMember:
			case Op::HostIndexGet:
			case Op::HostIndexSet:
			case Op::HostSet: {
				// The engine may call back into AS3 before it answers, which can move the registers: nothing
				// read before the call is used after it.
				host_op(function, in);
				break;
			}
			case Op::Trace:
				trace(function.argument_lists[in.a], r);
				break;
		}
		if (failing_ || stopped_) {
			// The instruction that failed is the one before pc: point the frame back at it for the trace.
			if (!frames_.empty() && frames_.back().pc > 0 && frames_.size() > p_depth) {
				frames_.back().pc--;
			}
			if (catch_error(p_depth)) {
				continue;
			}
			return false;
		}
	}
	return true;
}

} // namespace vm
} // namespace kas3
