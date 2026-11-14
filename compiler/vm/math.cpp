#include "numeric.h"

#include <chrono>
#include <cmath>

namespace kas3 {
namespace vm {

// A half goes up, whatever the sign. Adding a half and cutting would turn 0.49999999999999994 into 1.
static double round_half_up(double p_value) {
	if (!std::isfinite(p_value)) {
		return p_value;
	}
	double whole = std::floor(p_value);
	if (p_value - whole >= 0.5) {
		whole += 1;
	}
	// -0.2 and -0.5 round to -0, as ECMAScript says.
	return whole == 0 && std::signbit(p_value) ? -0.0 : whole;
}

double math_unary(ir::MathFunction p_function, double p_value) {
	switch (p_function) {
		case ir::MathFunction::Abs:
			return std::fabs(p_value);
		case ir::MathFunction::Acos:
			return std::acos(p_value);
		case ir::MathFunction::Asin:
			return std::asin(p_value);
		case ir::MathFunction::Atan:
			return std::atan(p_value);
		case ir::MathFunction::Ceil:
			return std::ceil(p_value);
		case ir::MathFunction::Cos:
			return std::cos(p_value);
		case ir::MathFunction::Exp:
			return std::exp(p_value);
		case ir::MathFunction::Floor:
			return std::floor(p_value);
		case ir::MathFunction::Log:
			return std::log(p_value);
		case ir::MathFunction::Round:
			return round_half_up(p_value);
		case ir::MathFunction::Sin:
			return std::sin(p_value);
		case ir::MathFunction::Sqrt:
			return std::sqrt(p_value);
		case ir::MathFunction::Tan:
			return std::tan(p_value);
	}
	return p_value;
}

double math_min(double p_a, double p_b) {
	if (std::isnan(p_a) || std::isnan(p_b)) {
		return std::nan("");
	}
	if (p_a == p_b) {
		return std::signbit(p_a) ? p_a : p_b; // -0 is below 0.
	}
	return p_a < p_b ? p_a : p_b;
}

double math_max(double p_a, double p_b) {
	if (std::isnan(p_a) || std::isnan(p_b)) {
		return std::nan("");
	}
	if (p_a == p_b) {
		return std::signbit(p_a) ? p_b : p_a;
	}
	return p_a > p_b ? p_a : p_b;
}

double math_pow(double p_base, double p_power) {
	if (std::isnan(p_power) || (std::fabs(p_base) == 1 && std::isinf(p_power))) {
		return std::nan("");
	}
	return std::pow(p_base, p_power);
}

double math_random() {
	// xorshift64*: small, quick, and good enough for a game's dice. Not for secrets.
	static uint64_t state = 0;
	if (state == 0) {
		state = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) | 1;
	}
	state ^= state >> 12;
	state ^= state << 25;
	state ^= state >> 27;
	const uint64_t mixed = state * 0x2545F4914F6CDD1Dull;
	return static_cast<double>(mixed >> 11) * (1.0 / 9007199254740992.0); // 53 bits over 2^53.
}

} // namespace vm
} // namespace kas3
