// Each kind of expression (lower_expressions.cpp), for Lowerer::expression(), which converts what it gives
// to the kind wanted. A helper the lowerer makes where it needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Expressions {
	Lowerer &l_;

	uint32_t name_value(Expr *p_expression);
	uint32_t member_value(Expr *p_expression);
	uint32_t new_value(Expr *p_expression);
	uint32_t postfix(Expr *p_expression);
	uint32_t conditional(Expr *p_expression, Kind p_want);

	uint32_t unary(Unary *p_unary, Kind p_want);

	uint32_t assign(Assign *p_assign);

public:
	explicit Expressions(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	uint32_t evaluate(Expr *p_expression, Kind p_want);
};

} // namespace ir
} // namespace kas3
