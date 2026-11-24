#include "interpreter_internal.h"
#include "numeric_dispatch.h"

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

// New, by the library's function.
void Interpreter::make(uint32_t p_class, Slot &r_out) {
	if (Object *object = object_new(library_, p_class)) {
		assign(Kind::Obj, r_out, object);
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

// Runs until the frame above p_depth returns. False if an error left it instead.
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
		// Scalar instructions cannot call back into the VM. Keep the frame and registers across them.
	dispatch:
		const Instruction &in = function.code[frame.pc++];
		// A Boolean result: the whole register is cleared, then the flag set. (Written here, not as a
		// function in a header: measured, that form makes the loop about 3% slower.)
		const auto flag = [&](bool p_value) {
			r[in.a].bits = 0;
			r[in.a].b = p_value;
		};
		switch (in.op.execution) {
			case Op::Stmt:
				if (--countdown_ && !hook_) {
					goto dispatch;
				}
				at_statement();
				break;
			case Op::ConstI32:
				r[in.a].i = (int32_t)in.b;
				goto dispatch;
			case Op::ConstU32:
				r[in.a].u = in.b;
				goto dispatch;
			case Op::ConstF64:
				r[in.a].d = module_.numbers[in.b];
				goto dispatch;
			case Op::ConstBool:
				flag(in.b != 0);
				goto dispatch;
			case Op::ConstStr:
				if (r[in.a].p != strings_[in.b]) {
					retain(strings_[in.b]);
					assign(Kind::Str, r[in.a], strings_[in.b]);
				}
				goto dispatch;
			case Op::Release:
				put(function.registers[in.a], r[in.a], Slot{});
				break;
			case Op::ConstNull:
				drop(function.registers[in.a], r[in.a]);
				break;
			case Op::TakeMove:
				put(function.registers[in.a], r[in.a], std::exchange(r[in.b], Slot{}));
				break;
			case Op::Move: {
				Kind kind = function.registers[in.a];
				if (!held(kind)) {
					r[in.a] = r[in.b];
					goto dispatch;
				}
				if (kind == Kind::Val && r[in.a].p && r[in.b].p && host_.handles.assign(r[in.a].p, r[in.b].p)) {
					break;
				}
				put(kind, r[in.a], share(kind, r[in.b]));
				break;
			}
			case Op::I32ToF64:
				if (!hook_ && countdown_ > 1 && frame.pc < function.code.size() && function.code[frame.pc].op == Op::ConstF64) {
					if (const auto *loop = loops_.at(frame.function, frame.pc - 1)) {
						--countdown_;
						frame.pc = loop->advance(r);
						goto dispatch;
					}
				}
				r[in.a].d = r[in.b].i;
				numeric_tail(module_, function, frame.pc, r);
				goto dispatch;
			case Op::U32ToF64:
				r[in.a].d = r[in.b].u;
				goto dispatch;
			case Op::BoolToF64:
				r[in.a].d = r[in.b].b ? 1 : 0;
				goto dispatch;
			case Op::StrToF64: {
				Str *string = static_cast<Str *>(r[in.b].p);
				r[in.a].d = string ? string_to_number(string->view()) : 0;
				break;
			}
			case Op::F64ToI32:
				r[in.a].i = to_int32(r[in.b].d);
				goto dispatch;
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
				flag(string && string->size() > 0);
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
			case Op::I32ToU8:
				r[in.a].u = static_cast<uint8_t>(r[in.b].i);
				break;
			case Op::U8ToI32:
				r[in.a].i = static_cast<int32_t>(r[in.b].u);
				break;
			case Op::U32ToU8:
				r[in.a].u = static_cast<uint8_t>(r[in.b].u);
				break;
			case Op::U8ToU32:
				r[in.a].u = r[in.b].u;
				break;
			case Op::I64ToF32:
				r[in.a].d = static_cast<float>(r[in.b].l);
				break;
			case Op::F64ToF32:
				r[in.a].d = static_cast<float>(r[in.b].d);
				break;
			case Op::F32ToF64:
				r[in.a].d = r[in.b].d;
				break;
			case Op::AddF32:
				r[in.a].d = static_cast<float>(r[in.b].d) + static_cast<float>(r[in.c].d);
				break;
			case Op::SubF32:
				r[in.a].d = static_cast<float>(r[in.b].d) - static_cast<float>(r[in.c].d);
				break;
			case Op::MulF32:
				r[in.a].d = static_cast<float>(r[in.b].d) * static_cast<float>(r[in.c].d);
				break;
			case Op::DivF32:
				r[in.a].d = static_cast<float>(r[in.b].d) / static_cast<float>(r[in.c].d);
				break;
			case Op::ModF32:
				r[in.a].d = std::fmod(static_cast<float>(r[in.b].d), static_cast<float>(r[in.c].d));
				break;
			case Op::AddF64:
				r[in.a].d = r[in.b].d + r[in.c].d;
				goto dispatch;
			case Op::SubF64:
				r[in.a].d = r[in.b].d - r[in.c].d;
				goto dispatch;
			case Op::MulF64:
				r[in.a].d = r[in.b].d * r[in.c].d;
				goto dispatch;
			case Op::DivF64:
				r[in.a].d = r[in.b].d / r[in.c].d;
				goto dispatch;
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
				goto dispatch;
			case Op::BitOr:
				r[in.a].i = r[in.b].i | r[in.c].i;
				goto dispatch;
			case Op::BitXor:
				r[in.a].i = r[in.b].i ^ r[in.c].i;
				goto dispatch;
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
				goto dispatch;
			case Op::LeF64:
				flag(r[in.b].d <= r[in.c].d);
				goto dispatch;
			case Op::EqF64:
				flag(r[in.b].d == r[in.c].d);
				goto dispatch;
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
				if (Str *joined = join_strings(static_cast<const Str *>(r[in.b].p), static_cast<const Str *>(r[in.c].p))) {
					assign(Kind::Str, r[in.a], joined);
				} else {
					out_of_memory();
				}
				break;
			case Op::StrLength: {
				Str *string = static_cast<Str *>(r[in.b].p);
				r[in.a].i = string ? string->length() : 0;
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
				goto dispatch;
			case Op::JumpIf:
				if (r[in.a].b) {
					frame.pc = in.b;
				}
				goto dispatch;
			case Op::JumpIfNot:
				if (!r[in.a].b) {
					frame.pc = in.b;
				}
				goto dispatch;
			case Op::GetField: {
				Object *object = static_cast<Object *>(r[in.b].p);
				if (!object) {
					raise_null();
					break;
				}
				Kind kind = function.registers[in.a];
				Slot value = field(module_.classes[object->klass], object, in.c);
				if (kind == Kind::Obj && value.p == r[in.a].p) {
					goto dispatch;
				}
				put(kind, r[in.a], share(kind, value));
				break;
			}
			case Op::TakeField: {
				auto *object = static_cast<Object *>(r[in.a].p);
				if (!object) {
					raise_null();
					break;
				}
				const auto &klass = module_.classes[object->klass];
				put(klass.fields[in.b].kind, field(klass, object, in.b), std::exchange(r[in.c], Slot{}));
				break;
			}
			case Op::SetField: {
				Object *object = static_cast<Object *>(r[in.a].p);
				if (!object) {
					raise_null();
					break;
				}
				const ir::Class &klass = module_.classes[object->klass];
				Kind kind = klass.fields[in.b].kind;
				put(kind, field(klass, object, in.b), share(kind, r[in.c]));
				break;
			}
			case Op::GetGlobal: {
				Kind kind = module_.globals[in.b].kind;
				put(kind, r[in.a], share(kind, globals_[in.b]));
				break;
			}
			case Op::TakeGlobal:
				put(module_.globals[in.a].kind, globals_[in.a], std::exchange(r[in.b], Slot{}));
				break;
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
			case Op::VecGet: {
				if (function.registers[in.a] == Kind::Str && indexed_read(function, in, r)) {
					break;
				}
				const uint32_t target = named_sites_.move_target(frame.function, frame.pc - 1);
				if (target == NONE) {
					vector_op(function, in, r);
				} else {
					Instruction read = in;
					read.a = target;
					vector_op(function, read, r);
					if (!failing_ && !stopped_) {
						frames_.back().pc++;
					}
				}
				break;
			}
			case Op::TakeElement:
				vector_store<true>(function, in, r);
				break;
			case Op::VecSet:
				vector_store<false>(function, in, r);
				break;
			case Op::VecNew:
			case Op::ArrayNew:
			case Op::VecLength:
			case Op::VecSetLength:
			case Op::VecFixed:
			case Op::VecSetFixed:
				vector_op(function, in, r);
				break;
			case Op::VecFrom:
				vector_from(in); // `frame` and `r` may be stale after this.
				break;
			case Op::Arguments:
				arguments_array(in);
				break;
			case Op::Native:
				native_op(in); // `frame` and `r` may be stale after this.
				break;
			case Op::ClassValue:
			case Op::ClassIs:
			case Op::ClassInstanceOf:
			case Op::ClassAs:
			case Op::ClassNew:
				class_op(function, in); // `frame` and `r` may be stale after this.
				break;
			case Op::CallLibrary:
				call_library(in); // `frame` and `r` may be stale after this.
				break;
			case Op::ToAny:
				if (named_sites_.borrows_set(frame.function, frame.pc - 1) && any::can_borrow(r[in.b].p)) {
					const Slot borrowed = any::godot_value(r[in.b].p);
					const Instruction &set = function.code[frame.pc++];
					if (cached_property(function, set, &borrowed)) {
						break;
					}
					frame.pc--;
				}
				any_op(function, in, r);
				break;
			case Op::FromAny:
				if (function.registers[in.a] == Kind::I32 && any::tag(r[in.b]) == AnyTag::Int) {
					set_int32(r[in.a], any::to_int_payload(r[in.b]));
					goto dispatch;
				}
				any_op(function, in, r);
				break;
			case Op::AnyConst:
			case Op::AnyText:
			case Op::AnyTypeof:
			case Op::AnyEq:
			case Op::AnyStrictEq:
			case Op::AnyAdd:
			case Op::AnyLt:
			case Op::AnyLe:
			case Op::AnyGt:
			case Op::AnyGe:
			case Op::AnySub:
			case Op::AnyMul:
			case Op::AnyDiv:
			case Op::AnyMod:
			case Op::AnyNeg:
			case Op::AnyIs:
			case Op::AnyIsGodot:
				any_op(function, in, r); // `frame` and `r` may be stale after this.
				break;
			case Op::VecCall:
				vector_call(function, in); // `frame` and `r` may be stale after this.
				break;
			case Op::DynCall:
				if (!cached_property(function, in)) {
					property_call(function, in);
				}
				break;
			case Op::DynGet:
			case Op::DynSet:
				if (!cached_property(function, in)) {
					property_op(function, in);
				}
				break;
			case Op::TableNew:
			case Op::DynDelete:
			case Op::DynIn:
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
			case Op::CallTypedFunction:
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
				put(Kind::Ref, r[in.a], object ? share(Kind::Ref, owner_field(object)) : none);
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
			case Op::HostGet: {
				const auto &steps = prepared_[frame.function];
				const auto *bind = steps.empty() ? nullptr : steps[frame.pc - 1].prepared;
				if (bind && bind->read_number && function.registers[in.a] == Kind::F64 &&
						r[in.b].p && bind->read_number(*bind, r[in.b].p, &r[in.a].d)) {
					goto dispatch;
				}
				host_op(function, in);
				break;
			}
			case Op::HostNew:
			case Op::HostCall:
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
			case Op::HostBindMethod:
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
			if (carried_.held && carried_.frames == frames_.size()) {
				raise_carried(); // From code the engine ran for this instruction (Interpreter::call()).
			}
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
