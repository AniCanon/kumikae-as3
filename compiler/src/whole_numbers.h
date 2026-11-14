// Rules about whole numbers that the checker and the lowerer must agree on, written once.
#pragma once

#include "names.h"

#include <cstdint>
#include <string_view>

namespace kas3 {

// A whole number written out, with any signs before it: `12`, `0xFF`, `-5`, `- -5`.
struct WideLiteral {
	enum State {
		NotWhole, // Not a literal, or one with a fraction or an exponent.
		Fits, // An Int64 holds it: `value` is it, exactly.
		TooLarge, // A whole number no Int64 holds.
	};
	State state = NotWhole;
	int64_t value = 0;
	bool negative = false;
	std::string_view digits; // As written, without the signs.
};
WideLiteral read_wide_literal(const Expr *p_expression);

// One of AS3's own functions whose result is a whole number though it comes back in a Number:
// Math.floor, Math.round, Math.ceil and parseInt always; Math.abs, Math.max and Math.min when every
// argument is whole (r_of_arguments is then set, and the caller looks at them).
bool gives_whole_number(const Call *p_call, bool *r_of_arguments);

// Whether an expression's value is a whole number: one of a whole-number type (p_integral says which
// expressions have one), or a sum, difference, product or remainder of such, a choice between two such,
// or a call that gives one. AS3 computes `i + 1` as a Number, but Godot gives Vector2i * int a Vector2i
// and Vector2i * float a Vector2, and `cell * (i + 1)` must stay on the grid.
template <typename IsIntegral>
bool is_whole_number(const Expr *p_expression, const IsIntegral &p_integral) {
	if (!p_expression || !p_expression->type) {
		return false;
	}
	switch (p_expression->kind) {
		case NodeKind::Binary: {
			const Binary *binary = static_cast<const Binary *>(p_expression);
			if (binary->op == TokenKind::Plus || binary->op == TokenKind::Minus || binary->op == TokenKind::Star || binary->op == TokenKind::Percent) {
				return is_whole_number(binary->left, p_integral) && is_whole_number(binary->right, p_integral);
			}
			break;
		}
		case NodeKind::Unary: {
			const Unary *unary = static_cast<const Unary *>(p_expression);
			if (unary->op == TokenKind::Minus || unary->op == TokenKind::Plus) {
				return is_whole_number(unary->operand, p_integral);
			}
			break;
		}
		case NodeKind::Conditional: {
			const Conditional *choice = static_cast<const Conditional *>(p_expression);
			return is_whole_number(choice->then_value, p_integral) && is_whole_number(choice->else_value, p_integral);
		}
		case NodeKind::Call: {
			const Call *call = static_cast<const Call *>(p_expression);
			bool of_arguments = false;
			if (gives_whole_number(call, &of_arguments)) {
				if (!of_arguments) {
					return true;
				}
				for (const Expr *argument : call->arguments) {
					if (!is_whole_number(argument, p_integral)) {
						return false;
					}
				}
				return true;
			}
			break;
		}
		default:
			break;
	}
	return p_integral(p_expression);
}

} // namespace kas3
