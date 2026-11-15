// Getters, setters and interfaces (lower_members.cpp): a helper the lowerer makes where it needs one, with
// the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Members {
	Lowerer &l_;

	// How a half of the property is reached from the place: a function, a table slot or an interface method.
	uint32_t half(const FunctionDecl *p_function, Place::Dispatch p_dispatch);
	void call(const Place &p_place, uint32_t p_function, uint32_t p_out, uint32_t p_value, uint32_t p_position);

public:
	explicit Members(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	// A getter or a setter of a class: its function, "Class/get x", and for an instance one the slot of
	// that name in the method table.
	void declare_accessor(uint32_t p_class, FunctionDecl *p_function);

	// A property of the program's own, named bare (p_object null) or through an object, as a place. The
	// object is worked out here, once. Nothing if the name is not such a property.
	std::optional<Place> property(Expr *p_target, Expr *p_object, Expr *p_later);

	uint32_t get(const Place &p_place, uint32_t p_position);
	void set(const Place &p_place, uint32_t p_value, uint32_t p_position);
};

} // namespace ir
} // namespace kas3
