// Whole-number arithmetic. The IR adds two ints as Numbers and converts the sum back (`i++` is
// I32ToF64, AddF64, F64ToI32): right, and slow, since no C++ compiler may turn that round trip into an
// integer add. Here a Number register that is known to hold a sum of whole numbers is remembered with the
// same sum in 32-bit arithmetic, and the conversion back reads that instead.
//
// It is exact: ints and uints are whole numbers below 2^32 in size, a Number holds any sum of a few of
// them exactly, and ToInt32 and ToUint32 of a whole number are that number modulo 2^32, which is what
// unsigned 32-bit arithmetic gives. (A product is not remembered: it can pass 2^53, where a Number rounds.)
//
// Measured, whole processes turn and turn about: tests/bench/loop.as 19.6 ms without and 6.6 ms with,
// vectors.as 34.9 ms and 9.0 ms.
//
// Two such Numbers compared are compared as the integers they are (`i < n` is I32ToF64 twice and LtF64):
// the same answer, and a loop's test that the C++ compiler knows the range of.
#include "function_writer.h"

#include "names.h"

#include <algorithm>
#include <cmath>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// A sum of this many terms is still far below 2^53.
constexpr uint32_t MOST_TERMS = 64;

} // namespace

void FunctionWriter::forget_whole(uint32_t p_register) {
	for (Whole &each : whole_) {
		if (std::find(each.sources.begin(), each.sources.end(), p_register) != each.sources.end()) {
			each = Whole();
		}
	}
	whole_[p_register] = Whole();
	text_[p_register] = NONE;
	text_exact_[p_register] = false;
}

bool FunctionWriter::whole_conversion(const Instruction &p_in) {
	if (p_in.op == Op::I64ToU32) {
		// Fixed array lengths check the range without entering the interpreter on every allocation.
		line("if (const auto converted = kas3::vm::wide_to_uint32(" + reg(p_in.b) + ")) {");
		line("\t" + reg(p_in.a) + " = *converted;");
		line("} else");
		by_thunk(p_in);
		return true;
	}
	if (p_in.op == Op::ModF64) {
		const Whole &left = whole_[p_in.b], &right = whole_[p_in.c];
		const auto widened = [&](const Whole &p_value) {
			if (p_value.sources.size() != 1 || p_value.terms != 1) {
				return false;
			}
			const Kind source = kind(p_value.sources.front());
			return source == Kind::I32 || source == Kind::U32;
		};
		if (widened(left) && widened(right)) {
			// Each operand was widened from a 32-bit integer. Divide in 64 bits, including INT_MIN % -1.
			line("if (" + right.exact + " != 0) {");
			line("const int64_t rem = " + left.exact + " % " + right.exact + ";");
			line(reg(p_in.a) + " = rem == 0 && " + left.exact + " < 0 ? -0.0 : static_cast<double>(rem);");
			line("} else { " + reg(p_in.a) + " = NAN; }");
			return true;
		}
	}
	// F64ToI32 or F64ToU32 of a known whole-number sum uses the integer's own type.
	if ((p_in.op != Op::F64ToI32 && p_in.op != Op::F64ToU32) || whole_[p_in.b].expression.empty()) {
		return false;
	}
	line(reg(p_in.a) + " = static_cast<" + std::string(type_of(kind(p_in.a))) + ">(" + whole_[p_in.b].expression + ");");
	return true;
}

// LtF64, LeF64 or EqF64 of two registers whose whole-number sums are known: the sums themselves compared.
bool FunctionWriter::whole_comparison(const Instruction &p_in) {
	if (p_in.op != Op::LtF64 && p_in.op != Op::LeF64 && p_in.op != Op::EqF64) {
		return false;
	}
	const Whole &left = whole_[p_in.b];
	const Whole &right = whole_[p_in.c];
	if (left.exact.empty() || right.exact.empty()) {
		return false;
	}
	const char *const test = p_in.op == Op::LtF64 ? " < " : p_in.op == Op::LeF64 ? " <= " : " == ";
	line(reg(p_in.a) + " = " + left.exact + test + right.exact + ";");
	return true;
}

// What the instruction just written makes known of the register it wrote.
void FunctionWriter::remember_whole(const Instruction &p_in) {
	switch (p_in.op) {
		case Op::I32ToF64:
		case Op::U32ToF64:
			whole_[p_in.a] = Whole{ "static_cast<uint32_t>(" + reg(p_in.b) + ")", "static_cast<int64_t>(" + reg(p_in.b) + ")", { p_in.b }, 1 };
			break;
		case Op::ConstF64: {
			const double value = module_.numbers[p_in.b];
			if (value == std::trunc(value) && std::fabs(value) <= 4294967295.0) {
				const std::string whole = "INT64_C(" + std::to_string(static_cast<int64_t>(value)) + ")";
				whole_[p_in.a] = Whole{ "static_cast<uint32_t>(" + whole + ")", whole, {}, 1 };
			}
			break;
		}
		case Op::AddF64:
		case Op::SubF64: {
			const Whole left = whole_[p_in.b];
			const Whole right = whole_[p_in.c];
			if (left.expression.empty() || right.expression.empty() || left.terms + right.terms > MOST_TERMS) {
				break;
			}
			const char *const sign = p_in.op == Op::AddF64 ? " + " : " - ";
			Whole sum{ "(" + left.expression + sign + right.expression + ")", "(" + left.exact + sign + right.exact + ")", left.sources,
				left.terms + right.terms };
			sum.sources.insert(sum.sources.end(), right.sources.begin(), right.sources.end());
			sum.sources.push_back(p_in.b);
			sum.sources.push_back(p_in.c);
			whole_[p_in.a] = std::move(sum);
			break;
		}
		case Op::Move:
			if (kind(p_in.a) == Kind::F64 && p_in.a != p_in.b && !whole_[p_in.b].expression.empty()) {
				whole_[p_in.a] = whole_[p_in.b];
				whole_[p_in.a].sources.push_back(p_in.b);
			}
			break;
		default:
			break;
	}
}

} // namespace backend_cpp
} // namespace kas3
