// A vector's methods, for Vectors: what each takes and gives, and the call (forEach, map, filter, some and
// every are in lower_functions.cpp).
#include "lower_vectors.h"

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

std::optional<VectorMethod> vector_method(std::string_view p_name) {
	struct Method {
		std::string_view name;
		VectorMethod method;
	};
	static constexpr std::array<Method, 16> methods = { {
			{ "push", VectorMethod::Push },
			{ "unshift", VectorMethod::Unshift },
			{ "pop", VectorMethod::Pop },
			{ "shift", VectorMethod::Shift },
			{ "indexOf", VectorMethod::IndexOf },
			{ "lastIndexOf", VectorMethod::LastIndexOf },
			{ "reverse", VectorMethod::Reverse },
			{ "slice", VectorMethod::Slice },
			{ "concat", VectorMethod::Concat },
			{ "join", VectorMethod::Join },
			{ "toString", VectorMethod::Join },
			{ "insertAt", VectorMethod::InsertAt },
			{ "removeAt", VectorMethod::RemoveAt },
			{ "splice", VectorMethod::Splice },
			{ "sort", VectorMethod::Sort },
			{ "sortOn", VectorMethod::SortOn },
	} };
	for (const Method &each : methods) {
		if (each.name == p_name) {
			return each.method;
		}
	}
	return std::nullopt;
}

// Concat, SortOn and Sort, which an Array takes otherwise than a Vector.
VectorShape array_or_vector_shape(VectorMethod p_method, bool p_array) {
	const Kind own = Kind::Obj;
	std::vector<Kind> fixed;
	std::vector<std::optional<int64_t>> left_out;
	std::optional<Kind> rest;
	Kind result = Kind::Void;
	switch (p_method) {
		case VectorMethod::Concat:
			rest = p_array ? Kind::Any : own; // An Array's takes anything: an Array is spread, the rest added.
			result = own;
			break;
		case VectorMethod::SortOn: // An Array's: the names and the options, each untyped.
			rest = Kind::Any;
			result = Kind::Any;
			break;
		case VectorMethod::Sort:
			// A Vector's takes one; an Array's none to two, and gives the array, 0 or an Array of indices.
			if (p_array) {
				rest = Kind::Any;
				result = Kind::Any;
			} else {
				fixed = { Kind::Any };
				left_out = { std::nullopt };
				result = own;
			}
			break;
		default:
			break;
	}
	return { fixed, left_out, rest, result };
}

VectorShape vector_shape(VectorMethod p_method, Kind p_element, bool p_array) {
	const Kind element = p_element;
	const Kind own = Kind::Obj;
	std::vector<Kind> fixed;
	std::vector<std::optional<int64_t>> left_out;
	std::optional<Kind> rest;
	Kind result = Kind::Void;
	switch (p_method) {
		case VectorMethod::Push:
		case VectorMethod::Unshift:
			rest = element;
			result = Kind::U32;
			break;
		case VectorMethod::Pop:
		case VectorMethod::Shift:
			result = element;
			break;
		case VectorMethod::IndexOf:
		case VectorMethod::LastIndexOf:
			fixed = { element, Kind::I32 };
			left_out = { std::nullopt, p_method == VectorMethod::IndexOf ? 0 : INT32_MAX };
			result = Kind::I32;
			break;
		case VectorMethod::Reverse:
			result = own;
			break;
		case VectorMethod::Slice:
			fixed = { Kind::I32, Kind::I32 };
			left_out = { 0, 16777215 };
			result = own;
			break;
		case VectorMethod::Concat:
		case VectorMethod::SortOn:
		case VectorMethod::Sort:
			return array_or_vector_shape(p_method, p_array);
		case VectorMethod::Join:
			fixed = { Kind::Str };
			left_out = { 0 }; // A comma, made below.
			result = Kind::Str;
			break;
		case VectorMethod::InsertAt:
			fixed = { Kind::I32, element };
			left_out = { std::nullopt, std::nullopt };
			break;
		case VectorMethod::RemoveAt:
			fixed = { Kind::I32 };
			left_out = { std::nullopt };
			result = element;
			break;
		case VectorMethod::Splice:
			fixed = { Kind::I32, Kind::U32 };
			left_out = { std::nullopt, UINT32_MAX };
			rest = element;
			result = own;
			break;
	}
	return { fixed, left_out, rest, result };
}

} // namespace

std::optional<uint32_t> Vectors::never_found(Call *p_call, Kind p_element) {
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
	const std::optional<VectorMethod> method = vector_method(callee->name);
	const uint32_t count = p_call->arguments.count;
	for (std::string_view loop : { "forEach", "map", "filter", "some", "every" }) {
		if (klass && callee->name == loop) {
			return vector_each(p_call);
		}
	}
	if (!klass || !method || (callee->name == "toString" && count) || (*method == VectorMethod::SortOn && !l_.module_.classes[*klass].array)) {
		l_.refuse(callee->name_offset, "calling '" + std::string(callee->name) + "' on a Vector");
		return l_.default_value(Kind::Obj, position);
	}
	const Kind element = l_.module_.classes[*klass].element;
	const bool array = l_.module_.classes[*klass].array;
	const auto [fixed, left_out, rest, result] = vector_shape(*method, element, array);
	bool fits = rest || count <= fixed.size();
	for (size_t i = count; i < fixed.size(); i++) {
		fits = fits && left_out[i].has_value();
	}
	for (uint32_t i = 0; *method == VectorMethod::Concat && !array && i < count; i++) {
		fits = fits && p_call->arguments[i]->type == callee->object->type; // concat: vectors of this very type.
	}
	if (!fits) {
		l_.refuse(callee->name_offset, "this call of '" + std::string(callee->name) + "' on a Vector");
		return l_.default_value(Kind::Obj, position);
	}
	if ((*method == VectorMethod::IndexOf || *method == VectorMethod::LastIndexOf) && count > 0) {
		if (const std::optional<uint32_t> none = never_found(p_call, element)) {
			return *none;
		}
	}
	std::vector<uint32_t> list;
	list.push_back(l_.settled(l_.expression(callee->object, Kind::Obj), Lowerer::writer(p_call->arguments), position));
	for (uint32_t i = 0; i < count; i++) {
		Expr *later = Lowerer::writer(p_call->arguments, i + 1, count);
		list.push_back(l_.settled(l_.expression(p_call->arguments[i], i < fixed.size() ? fixed[i] : *rest), later, position));
	}
	for (size_t i = count; i < fixed.size(); i++) {
		const uint32_t filled = l_.reg(fixed[i]);
		if (fixed[i] == Kind::Str) {
			l_.emit(Op::ConstStr, position, filled, l_.string(","));
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
