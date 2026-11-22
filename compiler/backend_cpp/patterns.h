// The operations that are one C++ statement, as a table: the kinds their registers must hold, and the
// statement. In the text, `$a`, `$b` and `$c` are the registers, `$A` the C++ type of the first, `#c`
// the third operand as a number.
#pragma once

#include "ir.h"

#include <string_view>

namespace kas3 {
namespace backend_cpp {

struct Pattern {
	ir::Op op;
	// For each operand that is a register, what it must hold: n an int or a uint, f a Number, b a Boolean,
	// s a String, o an object, w an Int64; `-` for an operand that is not a register.
	std::string_view kinds;
	std::string_view text;
};

inline constexpr Pattern PATTERNS[] = {
	{ ir::Op::I32ToF64, "fn-", "$a = static_cast<double>($b);" },
	{ ir::Op::U32ToF64, "fn-", "$a = static_cast<double>($b);" },
	{ ir::Op::BoolToF64, "fb-", "$a = $b ? 1.0 : 0.0;" },
	{ ir::Op::F64ToI32, "nf-", "$a = static_cast<$A>(to_i32(c, $b));" },
	{ ir::Op::F64ToU32, "nf-", "$a = static_cast<$A>(to_u32(c, $b));" },
	{ ir::Op::I32ToU32, "nn-", "$a = static_cast<$A>($b);" },
	{ ir::Op::U32ToI32, "nn-", "$a = static_cast<$A>($b);" },
	{ ir::Op::I32ToBool, "bn-", "$a = $b != 0;" },
	{ ir::Op::U32ToBool, "bn-", "$a = $b != 0;" },
	{ ir::Op::F64ToBool, "bf-", "$a = to_bool($b);" },
	{ ir::Op::StrToBool, "bs-", "$a = $b && $b->size > 0;" },
	{ ir::Op::StrToF64, "fs-", "$a = text_to_number(c, $b);" },
	{ ir::Op::StrLength, "ns-", "$a = static_cast<$A>(text_length(c, $b));" },
	{ ir::Op::EqStr, "bss", "$a = same_text($b, $c);" },
	{ ir::Op::ObjToBool, "bo-", "$a = $b != nullptr;" },
	{ ir::Op::I64ToF32, "gw-", "$a = static_cast<float>($b);" },
	{ ir::Op::F64ToF32, "gf-", "$a = static_cast<float>($b);" },
	{ ir::Op::F32ToF64, "fg-", "$a = $b;" },
	{ ir::Op::AddF32, "ggg", "$a = $b + $c;" },
	{ ir::Op::SubF32, "ggg", "$a = $b - $c;" },
	{ ir::Op::MulF32, "ggg", "$a = $b * $c;" },
	{ ir::Op::DivF32, "ggg", "$a = $b / $c;" },
	{ ir::Op::ModF32, "ggg", "$a = std::fmod($b, $c);" },
	{ ir::Op::AddF64, "fff", "$a = $b + $c;" },
	{ ir::Op::SubF64, "fff", "$a = $b - $c;" },
	{ ir::Op::MulF64, "fff", "$a = $b * $c;" },
	{ ir::Op::DivF64, "fff", "$a = $b / $c;" },
	{ ir::Op::ModF64, "fff", "$a = std::fmod($b, $c);" },
	{ ir::Op::NegF64, "ff-", "$a = -$b;" },
	{ ir::Op::MathUnary, "ff-", "$a = math_unary(c, #c, $b);" },
	{ ir::Op::Atan2, "fff", "$a = std::atan2($b, $c);" },
	{ ir::Op::Pow, "fff", "$a = math_pow(c, $b, $c);" },
	{ ir::Op::MinF64, "fff", "$a = math_min(c, $b, $c);" },
	{ ir::Op::MaxF64, "fff", "$a = math_max(c, $b, $c);" },
	{ ir::Op::Random, "f--", "$a = math_random(c);" },
	{ ir::Op::BitAnd, "nnn", "$a = static_cast<$A>(i32($b) & i32($c));" },
	{ ir::Op::BitOr, "nnn", "$a = static_cast<$A>(i32($b) | i32($c));" },
	{ ir::Op::BitXor, "nnn", "$a = static_cast<$A>(i32($b) ^ i32($c));" },
	{ ir::Op::Shl, "nnn", "$a = static_cast<$A>(shl(i32($b), i32($c)));" },
	{ ir::Op::Shr, "nnn", "$a = static_cast<$A>(shr(i32($b), i32($c)));" },
	{ ir::Op::UShr, "nnn", "$a = static_cast<$A>(ushr(i32($b), i32($c)));" },
	{ ir::Op::BitNot, "nn-", "$a = static_cast<$A>(~i32($b));" },
	{ ir::Op::LtF64, "bff", "$a = less($b, $c);" },
	{ ir::Op::LeF64, "bff", "$a = less_or_equal($b, $c);" },
	{ ir::Op::EqF64, "bff", "$a = equal($b, $c);" },
	{ ir::Op::EqBool, "bbb", "$a = equal($b, $c);" },
	{ ir::Op::Not, "bb-", "$a = !$b;" },
	{ ir::Op::AddI64, "www", "$a = add_wide($b, $c);" },
	{ ir::Op::SubI64, "www", "$a = sub_wide($b, $c);" },
	{ ir::Op::MulI64, "www", "$a = mul_wide($b, $c);" },
	{ ir::Op::NegI64, "ww-", "$a = neg_wide($b);" },
	{ ir::Op::BitAndI64, "www", "$a = $b & $c;" },
	{ ir::Op::BitOrI64, "www", "$a = $b | $c;" },
	{ ir::Op::BitXorI64, "www", "$a = $b ^ $c;" },
	{ ir::Op::BitNotI64, "ww-", "$a = ~$b;" },
	{ ir::Op::ShlI64, "www", "$a = shl_wide($b, $c);" },
	{ ir::Op::ShrI64, "www", "$a = kas3::vm::shift_right_wide($b, $c);" },
	{ ir::Op::UShrI64, "www", "$a = ushr_wide($b, $c);" },
	{ ir::Op::LtI64, "bww", "$a = less($b, $c);" },
	{ ir::Op::LeI64, "bww", "$a = less_or_equal($b, $c);" },
	{ ir::Op::EqI64, "bww", "$a = equal($b, $c);" },
	{ ir::Op::CmpI64F64, "fwf", "$a = compare_wide(c, $b, $c);" },
	{ ir::Op::I32ToI64, "wn-", "$a = static_cast<int64_t>($b);" },
	{ ir::Op::U32ToI64, "wn-", "$a = static_cast<int64_t>($b);" },
	{ ir::Op::I64ToBool, "bw-", "$a = $b != 0;" },
	{ ir::Op::I64WrapI32, "nw-", "$a = static_cast<$A>(static_cast<uint32_t>(static_cast<uint64_t>($b)));" },
	{ ir::Op::I64WrapU32, "nw-", "$a = static_cast<$A>(static_cast<uint32_t>(static_cast<uint64_t>($b)));" },
	{ ir::Op::I64RoundF64, "fw-", "$a = static_cast<double>($b);" },
	{ ir::Op::F64ClampI64, "wf-", "$a = clamp_wide(c, $b);" },
};

inline const Pattern *pattern_of(ir::Op p_op) {
	for (const Pattern &pattern : PATTERNS) {
		if (pattern.op == p_op) {
			return &pattern;
		}
	}
	return nullptr;
}

} // namespace backend_cpp
} // namespace kas3
