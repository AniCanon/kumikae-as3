// A vector's methods, for Vectors: what each takes and gives, and the call (forEach, map, filter, some and
// every are in lower_functions.cpp).
#include "lower_vectors.h"

#include "library_methods.h"

#include <array>

namespace kas3 {
namespace ir {

namespace {

// What a method of a vector takes and gives. The arguments with a place of their own come first: what
// each is converted to, and what it is when left out, if it may be. Then, for some methods, any number
// more of one kind.
struct VectorShape {
	std::vector<Kind> fixed;
	std::vector<std::optional<int64_t>> left_out;
	std::optional<Kind> rest;
	Kind result = Kind::Void;
};

// The shape of a vector's method, from its row in the library's table, for a vector of such elements.
VectorShape vector_shape(const LibraryMethod &p_method, Kind p_element) {
	VectorShape shape;
	for (const Taken &taken : p_method.fixed) {
		if (taken.kind == Takes::Nothing) {
			break;
		}
		shape.fixed.push_back(kind_taken(taken.kind, p_element));
		shape.left_out.push_back(taken.required ? std::nullopt : std::optional<int64_t>(static_cast<int64_t>(taken.left_out)));
	}
	if (p_method.rest != Takes::Nothing) {
		shape.rest = kind_taken(p_method.rest, p_element);
	}
	shape.result = kind_taken(p_method.result, p_element);
	return shape;
}

} // namespace

std::optional<uint32_t> Vectors::never_found(Call *p_call, Kind p_element) {
	if (holds_numbers(p_element)) {
		return std::nullopt; // Whatever it is given becomes a number of its kind, which may be an element.
	}
	const uint32_t position = p_call->offset;
	Member *callee = static_cast<Member *>(p_call->callee);
	const Kind searched = Lowerer::is_null(p_call->arguments[0]) ? Kind::Void : l_.natural(p_call->arguments[0]);
	const bool engine_and_null = is_engine(p_element) && searched == Kind::Void;
	if (!engine_and_null && !(is_engine(p_element) && searched == p_element) && !Lowerer::can_be_equal(p_element, searched)) {
		// A value of another type is never one of its elements (the search is by ===): -1, once the
		// vector and the arguments are worked out, as the call would.
		const uint32_t vector = l_.settled(l_.expression(callee->object, Kind::Obj), Lowerer::writer(p_call->arguments), position);
		for (Expr *argument : p_call->arguments) {
			l_.expression(argument, Kind::Void);
		}
		l_.emit(Op::NullCheck, position, vector);
		uint32_t none = l_.reg(Kind::I32);
		l_.emit(Op::ConstI32, position, none, static_cast<uint32_t>(-1));
		return none;
	}
	return std::nullopt;
}

uint32_t Vectors::vector_call(Call *p_call) {
	const uint32_t position = p_call->offset;
	Member *callee = static_cast<Member *>(p_call->callee);
	const std::optional<uint32_t> klass = l_.vector_class(callee->object->type);
	const bool array = klass && l_.module_.classes[*klass].array;
	const LibraryMethod *row = library_method(callee->name, array ? ON_ARRAY : ON_VECTOR);
	std::optional<VectorMethod> method = row && row->does == Does::VecCall ? std::optional<VectorMethod>(static_cast<VectorMethod>(row->which)) : std::nullopt;
	const uint32_t count = p_call->arguments.count;
	for (std::string_view loop : { "forEach", "map", "filter", "some", "every" }) {
		if (klass && callee->name == loop) {
			return vector_each(p_call);
		}
	}
	if (!klass || !method || (callee->name == "toString" && count)) {
		l_.refuse(callee->name_offset, "calling '" + std::string(callee->name) + "' on a Vector");
		return l_.default_value(Kind::Obj, position);
	}
	const Kind element = l_.module_.classes[*klass].element;
	auto [fixed, left_out, rest, result] = vector_shape(*row, element);
	bool fits = rest || count <= fixed.size();
	for (size_t i = count; i < fixed.size(); i++) {
		fits = fits && left_out[i].has_value();
	}
	if (!fits) {
		l_.refuse(callee->name_offset, "this call of '" + std::string(callee->name) + "' on a Vector");
		return l_.default_value(Kind::Obj, position);
	}
	if ((*method == VectorMethod::IndexOf || *method == VectorMethod::LastIndexOf) && count > 0) {
		if (const std::optional<uint32_t> none = never_found(p_call, element)) {
			return *none;
		}
		// A vector of numbers converts what it is given (avmplus's Vector.as: `indexOf(value:int)`). Any
		// other looks for the value as it is: one whose type is not known until it runs is handed over
		// untyped, and one of the elements' own kind as that.
		if (l_.natural(p_call->arguments[0]) == Kind::Any) {
			method = search_untyped(*method, element);
		} else {
			fixed[0] = element;
		}
	}
	std::vector<uint32_t> list;
	list.push_back(l_.settled(l_.expression(callee->object, Kind::Obj), Lowerer::writer(p_call->arguments), position));
	for (uint32_t i = 0; i < count; i++) {
		Expr *later = Lowerer::writer(p_call->arguments, i + 1, count);
		// concat takes vectors of this very type: one of another, or an Array, is found out as it runs
		// (AS3's TypeError #1034, as avmplus's castToThisType gives).
		const bool checked = *method == VectorMethod::Concat && !array && p_call->arguments[i]->type != callee->object->type;
		// What is stored into a Vector.<Object>, or looked for in one, is an Object's value: undefined is null.
		const Takes takes = i < fixed.size() ? row->fixed[i].kind : row->rest;
		const Kind kind = i < fixed.size() ? fixed[i] : *rest;
		const bool as_object = !array && (takes == Takes::Element || takes == Takes::Sought) && l_.starts_null_type(kind, callee->object->type->element);
		const uint32_t value = checked ? l_.from_untyped(l_.any_value(p_call->arguments[i]), callee->object->type, Kind::Obj, position)
							 : as_object ? l_.typed(p_call->arguments[i], callee->object->type->element, kind)
										 : l_.expression(p_call->arguments[i], kind);
		list.push_back(l_.settled(value, later, position));
	}
	for (size_t i = count; i < fixed.size(); i++) {
		const uint32_t filled = l_.reg(fixed[i]);
		if (fixed[i] == Kind::Str) {
			l_.emit(Op::ConstStr, position, filled, l_.string(std::string(row->text_left_out)));
		} else {
			l_.emit(fixed[i] == Kind::U32 ? Op::ConstU32 : Op::ConstI32, position, filled, static_cast<uint32_t>(*left_out[i]));
		}
		list.push_back(filled);
	}
	const uint32_t out = result == Kind::Void ? NONE : l_.reg(result);
	l_.emit(Op::VecCall, position, out, static_cast<uint32_t>(*method), l_.arguments(std::move(list)));
	return out == NONE ? 0 : out;
}

} // namespace ir
} // namespace kas3
