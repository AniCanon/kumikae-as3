// The standard library's lowering: one call of it at a time (lower_library.cpp).
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

// One call of the standard library, lowered. The arguments are worked out as the declarations in
// builtins/toplevel.as say, left to right after what the method is called on, and a missing string is
// reported after them, as in Flash. Each is converted before the instruction runs it, so the instruction
// itself runs the program only where what an argument is can't be known before (an untyped pattern's
// text, a replacement function).
class Library {
	Lowerer &l_;
	Call *call_;
	Symbol *function_;
	uint32_t position_;
	FunctionDecl *decl_;

public:
	Library(Lowerer &p_lowerer, Call *p_call, Symbol *p_function);

	Expr *object() const;
	uint32_t refused(std::string_view p_why, Kind p_kind);
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

	// RegExp's methods, and String's that take one (lower_regexp.cpp).
	uint32_t regexp_method();
	uint32_t pattern_call(RegExpFunction p_which);
	// Date's methods and its static ones (lower_date.cpp).
	uint32_t date_method();
	uint32_t date_static();
	// JSON's two functions, and a Dictionary's toJSON() (lower_json.cpp).
	static bool is_json(const Symbol *p_owner);
	uint32_t json_call();

	// RegExp and Date where no call is: their class, a literal, `new`, a property (lower_regexp.cpp).
	// Whether a class is one of the two: the interpreter holds its objects.
	static bool holds(const Symbol *p_class);
	// Its class in the module, made the first time it is asked for. Nothing for any other class.
	static std::optional<uint32_t> class_of(Lowerer &p_lowerer, const Symbol *p_class);
	static uint32_t regex_literal(Lowerer &p_lowerer, RegexLit *p_literal);
	static std::optional<uint32_t> construct(Lowerer &p_lowerer, New *p_new);
	static std::optional<Place> place(Lowerer &p_lowerer, Expr *p_target, Expr *p_later);
	static uint32_t load(Lowerer &p_lowerer, const Place &p_place, uint32_t p_position);
	static void store(Lowerer &p_lowerer, const Place &p_place, uint32_t p_value, uint32_t p_position);
	// Whether the value's class, one of the program's, declares valueOf(): beside `+` it is then that.
	static bool own_value_of(const Expr *p_value);
	static std::optional<Place> date_place(Lowerer &p_lowerer, Member *p_member, Expr *p_later);
	static uint32_t date_new(Lowerer &p_lowerer, New *p_new);
};

} // namespace ir
} // namespace kas3
