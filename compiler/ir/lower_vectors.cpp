#include "lowerer.h"

namespace kas3 {
namespace ir {

std::optional<uint32_t> Lowerer::vector_class(const Type *p_type) {
	if (const auto found = vector_classes_.find(p_type); found != vector_classes_.end()) {
		return found->second;
	}
	Kind element = Kind::Void;
	if (!is_vector(p_type) || !kind_of(p_type->element, &element) || element == Kind::Void) {
		return std::nullopt;
	}
	if (is_vector(p_type->element) && !vector_class(p_type->element)) {
		return std::nullopt;
	}
	// Named as AS3 writes the type, with a class of the program by its full name: two vectors are of one
	// class exactly when their names are the same.
	const Symbol *of = p_type->element->kind == Type::Instance ? p_type->element->symbol : nullptr;
	std::string inner = of && classes_.count(of) ? class_name(of) : compilation_.type_name(p_type->element);
	if (is_vector(p_type->element)) {
		inner = module_.classes[*vector_class(p_type->element)].name;
	}
	Class klass;
	klass.name = "Vector.<" + inner + ">";
	klass.element = element;
	module_.classes.push_back(std::move(klass));
	const uint32_t index = static_cast<uint32_t>(module_.classes.size()) - 1;
	vector_classes_[p_type] = index;
	return index;
}

uint32_t Lowerer::vector_new(New *p_made) {
	const uint32_t position = p_made->offset;
	const std::optional<uint32_t> klass = vector_class(p_made->type);
	if (!klass || p_made->arguments.count > 2) {
		refuse(p_made->callee->offset, "the type '" + compilation_.type_name(p_made->type) + "'");
		return default_value(Kind::Obj, position);
	}
	// new Vector.<T>(length = 0, fixed = false)
	Expr *length_written = p_made->arguments.count > 0 ? p_made->arguments[0] : nullptr;
	Expr *fixed_written = p_made->arguments.count > 1 ? p_made->arguments[1] : nullptr;
	const uint32_t length = length_written
			? settled(expression(length_written, Kind::U32), fixed_written, position)
			: default_value(Kind::U32, position);
	const uint32_t fixed = fixed_written ? expression(fixed_written, Kind::Bool) : default_value(Kind::Bool, position);
	const uint32_t out = reg(Kind::Obj);
	emit(Op::VecNew, position, out, *klass, arguments({ length, fixed }));
	return out;
}

uint32_t Lowerer::vector_literal(VectorLit *p_literal) {
	const uint32_t position = p_literal->offset;
	const std::optional<uint32_t> klass = vector_class(p_literal->type);
	if (!klass) {
		refuse(position, "the type '" + compilation_.type_name(p_literal->type) + "'");
		return default_value(Kind::Obj, position);
	}
	// Made empty, then filled in order: each element is worked out after the ones before it are stored.
	const uint32_t out = reg(Kind::Obj);
	emit(Op::VecNew, position, out, *klass, arguments({ default_value(Kind::U32, position), default_value(Kind::Bool, position) }));
	uint32_t at = 0;
	for (Expr *element : p_literal->elements) {
		const uint32_t value = expression(element, module_.classes[*klass].element);
		const uint32_t index = reg(Kind::F64);
		emit(Op::ConstF64, element->offset, index, number(at++));
		emit(Op::VecSet, element->offset, out, index, value);
	}
	return out;
}

auto Lowerer::vector_place(Expr *p_target, Expr *p_later) -> std::optional<Place> {
	Expr *object = nullptr;
	Place place;
	if (p_target->kind == NodeKind::Index && is_vector(static_cast<Index *>(p_target)->object->type)) {
		object = static_cast<Index *>(p_target)->object;
		place.where = Place::Element;
		if (!kind_of(p_target->type, &place.kind) || place.kind == Kind::Void) {
			refuse(p_target->offset, "an element of type '" + compilation_.type_name(p_target->type) + "'");
			return Place();
		}
	} else if (p_target->kind == NodeKind::Member && is_vector(static_cast<Member *>(p_target)->object->type)) {
		Member *member = static_cast<Member *>(p_target);
		object = member->object;
		if (member->name == "length") {
			place.where = Place::Length;
			place.kind = Kind::U32;
		} else if (member->name == "fixed") {
			place.where = Place::Fixed;
			place.kind = Kind::Bool;
		} else {
			return std::nullopt; // A method: the call lowers it.
		}
	} else {
		return std::nullopt;
	}
	if (!vector_class(object->type)) {
		refuse(object->offset, "the type '" + compilation_.type_name(object->type) + "'");
		return Place();
	}
	// The vector, then the index, then whatever comes after: each kept from what the next may change.
	Expr *index = place.where == Place::Element ? static_cast<Index *>(p_target)->index : nullptr;
	place.object = settled(expression(object, Kind::Obj), index && writes_locals(index) ? index : p_later, p_target->offset);
	if (index) {
		place.key = settled(expression(index, Kind::F64), p_later, p_target->offset);
	}
	return place;
}

} // namespace ir
} // namespace kas3
