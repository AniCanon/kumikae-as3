#include "lower_statements.h"
#include "lower_vectors.h"

#include <array>

namespace kas3 {
namespace ir {

std::optional<uint32_t> Lowerer::vector_class(const Type *p_type) {
	if (p_type && array_ && p_type->kind == Type::Instance && p_type->symbol == array_) {
		if (array_class_ == NONE) {
			Class klass;
			klass.name = "Array";
			klass.element = Kind::Any;
			klass.array = true;
			module_.classes.push_back(std::move(klass));
			array_class_ = static_cast<uint32_t>(module_.classes.size()) - 1;
			module_.array_class = array_class_;
		}
		return array_class_;
	}
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

uint32_t Lowerer::declared_class(TypeRef *p_type) {
	const Type *type = p_type ? compilation_.type_of(p_type) : nullptr;
	if (is_vector(type)) {
		return vector_class(type).value_or(NONE);
	}
	uint32_t index = NONE;
	if (type && type->kind == Type::Instance && class_index(type->symbol, &index)) {
		return index; // A class of the program, or Function or an Error class, made when first named.
	}
	return NONE;
}

uint32_t Vectors::vector_new(const Type *p_type, const List<Expr *> &p_arguments, uint32_t p_position) {
	const uint32_t position = p_position;
	const std::optional<uint32_t> klass = l_.vector_class(p_type);
	if (klass && l_.module_.classes[*klass].array) {
		// new Array(...): each argument untyped; one number is the length, as it runs.
		std::vector<uint32_t> list;
		for (uint32_t i = 0; i < p_arguments.count; i++) {
			Expr *later = Lowerer::writer(p_arguments, i + 1);
			list.push_back(l_.settled(l_.any_value(p_arguments[i]), later, position));
		}
		const uint32_t out = l_.reg(Kind::Obj);
		l_.emit(Op::ArrayNew, position, out, *klass, l_.arguments(std::move(list)));
		return out;
	}
	if (!klass || p_arguments.count > 2) {
		l_.refuse(position, "the type '" + l_.compilation_.type_name(p_type) + "'");
		return l_.default_value(Kind::Obj, position);
	}
	// new Vector.<T>(length = 0, fixed = false)
	Expr *length_written = p_arguments.count > 0 ? p_arguments[0] : nullptr;
	Expr *fixed_written = p_arguments.count > 1 ? p_arguments[1] : nullptr;
	const uint32_t length = length_written
			? l_.settled(l_.expression(length_written, Kind::U32), fixed_written, position)
			: l_.default_value(Kind::U32, position);
	const uint32_t fixed = fixed_written ? l_.expression(fixed_written, Kind::Bool) : l_.default_value(Kind::Bool, position);
	const uint32_t out = l_.reg(Kind::Obj);
	l_.emit(Op::VecNew, position, out, *klass, l_.arguments({ length, fixed }));
	return out;
}

uint32_t Vectors::vector_literal(Expr *p_literal, const List<Expr *> &p_elements) {
	const uint32_t position = p_literal->offset;
	const std::optional<uint32_t> klass = l_.vector_class(p_literal->type);
	if (!klass) {
		l_.refuse(position, "the type '" + l_.compilation_.type_name(p_literal->type) + "'");
		return l_.default_value(Kind::Obj, position);
	}
	// Made empty, then filled in order: each element is worked out after the ones before it are stored.
	const uint32_t out = l_.reg(Kind::Obj);
	l_.emit(Op::VecNew, position, out, *klass, l_.arguments({ l_.default_value(Kind::U32, position), l_.default_value(Kind::Bool, position) }));
	uint32_t at = 0;
	for (Expr *element : p_elements) {
		// A hole in an Array's, `[1,,2]`, is undefined.
		const uint32_t where = element ? element->offset : position;
		uint32_t value = NONE;
		if (element) {
			value = l_.expression(element, l_.module_.classes[*klass].element);
		} else {
			value = l_.reg(Kind::Any);
			l_.emit(Op::AnyConst, where, value, 0);
		}
		const uint32_t index = l_.reg(Kind::U32);
		l_.emit(Op::ConstU32, where, index, at++);
		l_.emit(Op::VecSet, where, out, index, value);
	}
	return out;
}

auto Vectors::vector_place(Expr *p_target, Expr *p_later) -> std::optional<Place> {
	Expr *object = nullptr;
	Place place;
	if (p_target->kind == NodeKind::Index && l_.is_vector(static_cast<Index *>(p_target)->object->type)) {
		object = static_cast<Index *>(p_target)->object;
		place.where = Place::Element;
		if (!l_.kind_of(p_target->type, &place.kind) || place.kind == Kind::Void) {
			l_.refuse(p_target->offset, "an element of type '" + l_.compilation_.type_name(p_target->type) + "'");
			return Place();
		}
	} else if (p_target->kind == NodeKind::Member && l_.is_vector(static_cast<Member *>(p_target)->object->type)) {
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
	if (!l_.vector_class(object->type)) {
		l_.refuse(object->offset, "the type '" + l_.compilation_.type_name(object->type) + "'");
		return Place();
	}
	// The vector, then the index, then whatever comes after: each kept from what the next may change.
	Expr *index = place.where == Place::Element ? static_cast<Index *>(p_target)->index : nullptr;
	place.object = l_.settled(l_.expression(object, Kind::Obj), index && Lowerer::writes_locals(index) ? index : p_later, p_target->offset);
	if (index) {
		// An int or a uint index is read in its own kind as it runs; anything else as a Number.
		Kind kind = Kind::F64;
		l_.kind_of(index->type, &kind);
		place.key = l_.settled(l_.expression(index, kind == Kind::I32 || kind == Kind::U32 ? kind : Kind::F64), p_later, p_target->offset);
	}
	return place;
}

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

// for each (x in vector) and for (i in vector): the elements, or their indices, first to last. The length
// is read again each time round, as in Flash: an element added inside the loop is reached. An Array's
// indices are Strings, as its keys are in AS3, and an element that is undefined is passed over: an Array
// here does not tell a hole (`new Array(3)`) from undefined, and a hole is no property to go through.
void Vectors::vector_loop(ForIn *p_loop) {
	const uint32_t position = p_loop->offset;
	if (!l_.vector_class(p_loop->collection->type)) {
		l_.refuse(p_loop->collection->offset, "the type '" + l_.compilation_.type_name(p_loop->collection->type) + "'");
		return;
	}
	Place target;
	if (p_loop->declaration && p_loop->declaration->declarators.count == 1) {
		target = l_.place_of_symbol(p_loop->declaration->declarators[0].symbol, 0, p_loop->declaration->declarators[0].offset);
	} else if (p_loop->target) {
		target = l_.place_of(p_loop->target);
	} else {
		l_.refuse(position, "this loop variable");
		return;
	}
	const uint32_t vector = l_.reg(Kind::Obj);
	l_.emit(Op::Move, position, vector, l_.expression(p_loop->collection, Kind::Obj));
	// Nothing to go through if there is no vector.
	const uint32_t to_end = l_.emit(Op::JumpIfNot, position, l_.convert(vector, Kind::Bool, position), 0);
	const uint32_t at = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, position, at, l_.number(0));
	const uint32_t top = l_.here();
	l_.emit(Op::Stmt, position);
	const uint32_t length = l_.reg(Kind::U32);
	l_.emit(Op::VecLength, position, length, vector);
	const uint32_t more = l_.reg(Kind::Bool);
	l_.emit(Op::LtF64, position, more, at, l_.convert(length, Kind::F64, position));
	const uint32_t out = l_.emit(Op::JumpIfNot, position, more, 0);
	const bool array = l_.module_.classes[*l_.vector_class(p_loop->collection->type)].array;
	uint32_t value = array ? l_.convert(at, Kind::Str, position) : at;
	uint32_t hole = NONE;
	if (p_loop->each || array) {
		const uint32_t element = l_.reg(l_.module_.classes[*l_.vector_class(p_loop->collection->type)].element);
		l_.emit(Op::VecGet, position, element, vector, at);
		value = p_loop->each ? element : value;
		if (array) {
			const uint32_t missing = l_.reg(Kind::Any), is_missing = l_.reg(Kind::Bool);
			l_.emit(Op::AnyConst, position, missing, 0);
			l_.emit(Op::AnyStrictEq, position, is_missing, element, missing);
			hole = l_.emit(Op::JumpIf, position, is_missing, 0);
		}
	}
	l_.store(target, l_.convert(value, target.kind, position), position);
	Statements(l_).open_loop();
	Statements(l_).statement(p_loop->body);
	const uint32_t next_at = l_.here();
	if (hole != NONE) {
		l_.patch(hole, next_at);
	}
	const uint32_t one = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, position, one, l_.number(1));
	l_.emit(Op::AddF64, position, at, at, one);
	l_.emit(Op::Jump, position, top);
	l_.patch(out, l_.here());
	l_.patch(to_end, l_.here());
	Statements(l_).finish_loop(next_at);
}

// for each (x in packed): one of Godot's collections that says what it holds, by index, first to last. Its
// size is asked again each time round, as a vector's length is read.
void Vectors::engine_loop(ForIn *p_loop) {
	const uint32_t position = p_loop->offset;
	Kind element = Kind::Void;
	uint32_t size_name = 0;
	const Type *whole = p_loop->collection->type;
	const Symbol *klass = whole && whole->kind == Type::Instance ? whole->symbol : nullptr;
	if (!l_.kind_of(p_loop->element, &element) || element == Kind::Void || !l_.engine_name(klass, "size", &size_name)) {
		l_.refuse(position, "'for each' over a collection of '" + l_.compilation_.type_name(p_loop->element) + "'");
		return;
	}
	Place target;
	if (p_loop->declaration && p_loop->declaration->declarators.count == 1) {
		target = l_.place_of_symbol(p_loop->declaration->declarators[0].symbol, 0, p_loop->declaration->declarators[0].offset);
	} else if (p_loop->target) {
		target = l_.place_of(p_loop->target);
	} else {
		l_.refuse(position, "this loop variable");
		return;
	}
	const uint32_t collection = l_.reg(Kind::Val);
	l_.emit(Op::Move, position, collection, l_.expression(p_loop->collection, Kind::Val));
	const uint32_t at = l_.reg(Kind::I64);
	l_.emit(Op::ConstI64, position, at, l_.wide(0));
	const uint32_t top = l_.here();
	l_.emit(Op::Stmt, position);
	const uint32_t size = l_.reg(Kind::I64);
	l_.emit(Op::HostCall, position, size, size_name, l_.arguments({ collection }));
	const uint32_t more = l_.reg(Kind::Bool);
	l_.emit(Op::LtI64, position, more, at, size);
	const uint32_t out = l_.emit(Op::JumpIfNot, position, more, 0);
	const uint32_t value = l_.engine_reg(element, p_loop->element);
	l_.emit(Op::HostIndexGet, position, value, collection, at);
	l_.store(target, l_.convert(value, target.kind, position), position);
	Statements(l_).open_loop();
	Statements(l_).statement(p_loop->body);
	const uint32_t next_at = l_.here();
	const uint32_t one = l_.reg(Kind::I64);
	l_.emit(Op::ConstI64, position, one, l_.wide(1));
	l_.emit(Op::AddI64, position, at, at, one);
	l_.emit(Op::Jump, position, top);
	l_.patch(out, l_.here());
	Statements(l_).finish_loop(next_at);
}

auto Vectors::vector_to_engine(Expr *p_value, const Type *p_wanted) -> std::optional<uint32_t> {
	if (!l_.is_vector(p_value->type)) {
		return std::nullopt;
	}
	const uint32_t position = p_value->offset;
	const std::optional<uint32_t> klass = l_.vector_class(p_value->type);
	if (!klass) {
		l_.refuse(position, "passing this Vector to Godot");
		return l_.default_value(Kind::Val, position);
	}
	// Godot's name for what it takes here: a packed array, or an Array where it takes that or anything.
	std::string wanted = "Array";
	const bool named = p_wanted && p_wanted->kind == Type::Instance && Lowerer::is_godot(p_wanted->symbol) &&
			godot_name(p_wanted->symbol->name, std::string_view(), &wanted);
	if (p_wanted && p_wanted->kind != Type::Any && !named) {
		l_.refuse(position, "passing a Vector where '" + l_.compilation_.type_name(p_wanted) + "' is expected");
		return l_.default_value(Kind::Val, position);
	}
	const uint32_t out = named ? l_.engine_reg(Kind::Val, p_wanted) : l_.reg(Kind::Val);
	l_.emit(Op::HostCollection, position, out, l_.expression(p_value, Kind::Obj), l_.string(wanted));
	return out;
}

} // namespace ir
} // namespace kas3
