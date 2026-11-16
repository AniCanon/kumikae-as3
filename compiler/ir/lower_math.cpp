#include "lowerer.h"

#include <array>
#include <limits>

namespace kas3 {
namespace ir {

std::optional<double> Lowerer::math_constant(const Symbol *p_symbol) const {
	struct Constant {
		std::string_view name;
		double value;
	};
	static constexpr std::array<Constant, 8> constants = { {
			{ "E", 2.718281828459045 },
			{ "LN10", 2.302585092994046 },
			{ "LN2", 0.6931471805599453 },
			{ "LOG10E", 0.4342944819032518 },
			{ "LOG2E", 1.4426950408889634 },
			{ "PI", 3.141592653589793 },
			{ "SQRT1_2", 0.7071067811865476 },
			{ "SQRT2", 1.4142135623730951 },
	} };
	if (!math_ || p_symbol->owner != math_) {
		return std::nullopt;
	}
	for (const Constant &constant : constants) {
		if (constant.name == p_symbol->name) {
			return constant.value;
		}
	}
	return std::nullopt;
}

std::optional<uint32_t> Lowerer::math_call(Call *p_call, const Symbol *p_function) {
	struct Unary {
		std::string_view name;
		MathFunction function;
	};
	static constexpr std::array<Unary, 13> unaries = { {
			{ "abs", MathFunction::Abs },
			{ "acos", MathFunction::Acos },
			{ "asin", MathFunction::Asin },
			{ "atan", MathFunction::Atan },
			{ "ceil", MathFunction::Ceil },
			{ "cos", MathFunction::Cos },
			{ "exp", MathFunction::Exp },
			{ "floor", MathFunction::Floor },
			{ "log", MathFunction::Log },
			{ "round", MathFunction::Round },
			{ "sin", MathFunction::Sin },
			{ "sqrt", MathFunction::Sqrt },
			{ "tan", MathFunction::Tan },
	} };
	if (!math_ || p_function->owner != math_ || p_function->kind != SymbolKind::Method) {
		return std::nullopt;
	}
	const uint32_t position = p_call->offset;
	const std::string_view name = p_function->name;
	// The arguments, left to right, each a Number kept from what a later one may do.
	std::vector<uint32_t> values;
	for (uint32_t i = 0; i < p_call->arguments.count; i++) {
		Expr *later = writer(p_call->arguments, i + 1);
		values.push_back(settled(expression(p_call->arguments[i], Kind::F64), later, position));
	}
	const uint32_t out = reg(Kind::F64);
	if (name == "random" && values.empty()) {
		emit(Op::Random, position, out);
		return out;
	}
	if (name == "max" || name == "min") {
		// With nothing to compare, the least and the greatest there is; otherwise one after another.
		const double start = name == "max" ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
		if (values.empty()) {
			emit(Op::ConstF64, position, out, number(start));
			return out;
		}
		uint32_t best = values[0];
		for (size_t i = 1; i < values.size(); i++) {
			const uint32_t next = reg(Kind::F64);
			emit(name == "max" ? Op::MaxF64 : Op::MinF64, position, next, best, values[i]);
			best = next;
		}
		return best;
	}
	if ((name == "atan2" || name == "pow") && values.size() == 2) {
		emit(name == "pow" ? Op::Pow : Op::Atan2, position, out, values[0], values[1]);
		return out;
	}
	for (const Unary &unary : unaries) {
		if (unary.name == name && values.size() == 1) {
			emit(Op::MathUnary, position, out, values[0], static_cast<uint32_t>(unary.function));
			return out;
		}
	}
	return std::nullopt;
}

} // namespace ir
} // namespace kas3
