// The standard library's lowering: one call of it at a time (lower_library.cpp).
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

// One call of the standard library, lowered. The arguments are worked out as the declarations in
// builtins/toplevel.as say, left to right after what the method is called on, and a missing string is
// reported after them, as in Flash. Each is converted before the instruction runs it, so the instruction
// itself never runs the program.
class Library {
	Lowerer &l_;
	Call *call_;
	Symbol *function_;
	uint32_t position_;
	FunctionDecl *decl_;

public:
	Library(Lowerer &p_lowerer, Call *p_call, Symbol *p_function);

	Expr *object() const;
	uint32_t refused(const std::string &p_why, Kind p_kind);
	Expr *later(uint32_t p_from) const;
	// The value a declaration gives a parameter left out: a number, a string, or `undefined` (NaN).
	uint32_t left_out(const Parameter &p_parameter, Kind p_kind);
	// The arguments for the declared parameters, each converted as the parameter says, then any number
	// more converted to p_rest; first p_receiver, if it is not NONE.
	std::vector<uint32_t> arguments(uint32_t p_receiver, Kind p_rest = Kind::Void);
	uint32_t emit(Op p_op, uint32_t p_which, std::vector<uint32_t> p_list, Kind p_result);
	uint32_t string_method();
	// split(delimiter, limit): the delimiter untyped (null, left out, a String or what becomes one), the limit
	// a Number. A regular expression waits for RegExp.
	uint32_t split();
	uint32_t number_method(Kind p_kind);
	uint32_t top_level();
	// toString() of Object: a class's own, the one of the class the object is of; super.toString() and an
	// Error's are Object's and Error's own.
	uint32_t object_text();
};

} // namespace ir
} // namespace kas3
