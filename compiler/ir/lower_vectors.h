// Vectors (lower_vectors.cpp): a Vector.<T>, or an Array, once its class is made (Lowerer::vector_class):
// `new`, a literal, an element as a place, a method (lower_vector_methods.cpp), a loop over one, and handing
// one to Godot. forEach, map, filter, some and every, which call a function, are in lower_functions.cpp. A helper the lowerer
// makes where it needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Vectors {
	Lowerer &l_;

	uint32_t vector_each(Call *p_call);
	uint32_t each_result(std::string_view p_name, uint32_t p_class, uint32_t p_limit, uint32_t p_position);
	uint32_t each_answer(Call *p_call, uint32_t p_class, uint32_t p_out, uint32_t p_at, uint32_t p_item, uint32_t p_answer, uint32_t p_yes);
	// indexOf or lastIndexOf: -1 when what is looked for is of a type no element can be equal to.
	std::optional<uint32_t> never_found(Call *p_call, Kind p_element);

public:
	explicit Vectors(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	uint32_t vector_new(const Type *p_type, const List<Expr *> &p_arguments, uint32_t p_position);
	uint32_t vector_literal(Expr *p_literal, const List<Expr *> &p_elements);
	uint32_t vector_call(Call *p_call);
	// An element of a vector, or its length or `fixed`, as a place. Nothing if the target is none of those.
	std::optional<Place> vector_place(Expr *p_target, Expr *p_later);
	// A vector where Godot takes one of its own collections: converted, element by element, to the type
	// wanted (an Array where Godot takes anything). Nothing if the value is not a vector.
	std::optional<uint32_t> vector_to_engine(Expr *p_value, const Type *p_wanted);
	void vector_loop(ForIn *p_loop);
	void engine_loop(ForIn *p_loop);
};

} // namespace ir
} // namespace kas3
