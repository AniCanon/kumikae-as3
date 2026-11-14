#include "interpreter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

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
				host_.output.print(line);
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

} // namespace vm
} // namespace kas3
