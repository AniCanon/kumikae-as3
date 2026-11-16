// Function expressions, what they capture, and function values: a method or a function taken as a value,
// and a call through one (lower_closures.cpp, lower_functions.cpp). A helper the lowerer makes where it
// needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Functions {
	Lowerer &l_;

public:
	explicit Functions(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	// Function expressions and what they capture (lower_closures.cpp).
	void make_environment(FunctionDecl *p_decl);
	std::optional<Place> captured_place(const Symbol *p_symbol, uint32_t p_position);
	uint32_t closure_value(FunctionExpr *p_expression);
	void lower_pending_functions();
	// Function values (lower_functions.cpp).
	uint32_t function_value(Expr *p_expression);
	static bool called_value(const Symbol *p_symbol);
	uint32_t value_call(Call *p_call, Kind p_want);
	std::optional<uint32_t> function_method(Call *p_call);
	void use_argument_error();
};

} // namespace ir
} // namespace kas3
