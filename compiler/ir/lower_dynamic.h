// Properties by name: Object literals and `new Object()`, flash.utils.Dictionary, and every member reached
// by a name known only as the program runs (`o.a`, `o["a"]`, `delete`, `in`, `for in`). Lowered by one
// helper class, so that the lowerer's own header does not grow.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

// flash.utils.Dictionary, of the built-in library.
bool is_dictionary(const Symbol *p_symbol);

class Dynamic {
	Lowerer &l_;

	uint32_t holder(Expr *p_holder, Expr *p_later, uint32_t p_position);
	uint32_t key(Expr *p_key, Expr *p_later, uint32_t p_position);
	uint32_t name(std::string_view p_name, uint32_t p_position);
	void use_classes();

public:
	explicit Dynamic(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	// The class of an Object or of a Dictionary, made the first time it is wanted.
	uint32_t table_class(bool p_dictionary);
	// Whether a value of this expression's type has its properties reached by name: an untyped value, an
	// Object, a Dictionary, or (for a name it does not declare) an Array or an object of a class.
	bool by_name(const Expr *p_holder) const;
	// `o.a` or `o[k]` as a place, if it is one reached by name.
	std::optional<Place> place(Expr *p_target, Expr *p_later);
	uint32_t literal(ObjectLit *p_literal);
	// `new Object()` and `new Dictionary(weakKeys)`. Nothing if the class is neither.
	std::optional<uint32_t> made(New *p_new, const Symbol *p_class);
	uint32_t object_from(Expr *p_value, uint32_t p_position);
	// `o.f(...)` where `f` is reached by name, or is one of Object's own methods. Nothing if it is not.
	std::optional<uint32_t> call(Call *p_call);
	uint32_t remove(Unary *p_delete);
	uint32_t in(Binary *p_in);
	void loop(ForIn *p_loop);
	// An Object or a Dictionary where Godot takes one of its Dictionaries or anything: the value goes
	// untyped, and becomes a Dictionary of Godot's as it goes. Nothing for any other value.
	std::optional<uint32_t> to_engine(Expr *p_value, const Type *p_wanted);
};

} // namespace ir
} // namespace kas3
