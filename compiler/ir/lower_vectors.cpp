#include "lowerer.h"

#include <array>

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

uint32_t Lowerer::declared_class(TypeRef *p_type) {
	const Type *type = p_type ? compilation_.type_of(p_type) : nullptr;
	if (is_vector(type)) {
		return vector_class(type).value_or(NONE);
	}
	if (type && type->kind == Type::Instance) {
		const auto found = classes_.find(type->symbol);
		return found == classes_.end() ? NONE : found->second;
	}
	return NONE;
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
		// An int or a uint index is read in its own kind as it runs; anything else as a Number.
		Kind kind = Kind::F64;
		kind_of(index->type, &kind);
		place.key = settled(expression(index, kind == Kind::I32 || kind == Kind::U32 ? kind : Kind::F64), p_later, p_target->offset);
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
	static constexpr std::array<Method, 14> methods = { {
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
	} };
	for (const Method &each : methods) {
		if (each.name == p_name) {
			return each.method;
		}
	}
	return std::nullopt; // forEach, map, filter, some, every and sort take a function: they wait for closures.
}

VectorShape vector_shape(VectorMethod p_method, Kind p_element) {
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
			rest = own;
			result = own;
			break;
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

uint32_t Lowerer::vector_call(Call *p_call) {
	const uint32_t position = p_call->offset;
	Member *callee = static_cast<Member *>(p_call->callee);
	const std::optional<uint32_t> klass = vector_class(callee->object->type);
	const std::optional<VectorMethod> method = vector_method(callee->name);
	const uint32_t count = p_call->arguments.count;
	if (!klass || !method || (callee->name == "toString" && count)) {
		refuse(callee->name_offset, "calling '" + std::string(callee->name) + "' on a Vector");
		return default_value(Kind::Obj, position);
	}
	const Kind element = module_.classes[*klass].element;
	const auto [fixed, left_out, rest, result] = vector_shape(*method, element);
	bool fits = rest || count <= fixed.size();
	for (size_t i = count; i < fixed.size(); i++) {
		fits = fits && left_out[i].has_value();
	}
	for (uint32_t i = 0; *method == VectorMethod::Concat && i < count; i++) {
		fits = fits && p_call->arguments[i]->type == callee->object->type; // concat: vectors of this very type.
	}
	if (!fits) {
		refuse(callee->name_offset, "this call of '" + std::string(callee->name) + "' on a Vector");
		return default_value(Kind::Obj, position);
	}
	if ((*method == VectorMethod::IndexOf || *method == VectorMethod::LastIndexOf) && count > 0) {
		const Kind searched = is_null(p_call->arguments[0]) ? Kind::Void : natural(p_call->arguments[0]);
		if (is_engine(element) || !can_be_equal(element, searched)) {
			refuse(p_call->arguments[0]->offset, is_engine(element) ? "searching a Vector of Godot's values or objects" : "searching a Vector for a value of another type");
			return default_value(Kind::I32, position);
		}
	}
	std::vector<uint32_t> list;
	list.push_back(settled(expression(callee->object, Kind::Obj), writer(p_call->arguments), position));
	for (uint32_t i = 0; i < count; i++) {
		Expr *later = nullptr;
		for (uint32_t next = i + 1; next < count && !later; next++) {
			later = writes_locals(p_call->arguments[next]) ? p_call->arguments[next] : nullptr;
		}
		list.push_back(settled(expression(p_call->arguments[i], i < fixed.size() ? fixed[i] : *rest), later, position));
	}
	for (size_t i = count; i < fixed.size(); i++) {
		const uint32_t filled = reg(fixed[i]);
		if (fixed[i] == Kind::Str) {
			emit(Op::ConstStr, position, filled, string(","));
		} else {
			emit(fixed[i] == Kind::U32 ? Op::ConstU32 : Op::ConstI32, position, filled, static_cast<uint32_t>(*left_out[i]));
		}
		list.push_back(filled);
	}
	const uint32_t out = result == Kind::Void ? NONE : reg(result);
	emit(Op::VecCall, position, out, static_cast<uint32_t>(*method), arguments(std::move(list)));
	return out == NONE ? 0 : out;
}

// for each (x in vector) and for (i in vector): the elements, or their indices, first to last. The length
// is read again each time round, as in Flash: an element added inside the loop is reached.
void Lowerer::vector_loop(ForIn *p_loop) {
	const uint32_t position = p_loop->offset;
	if (!vector_class(p_loop->collection->type)) {
		refuse(p_loop->collection->offset, "the type '" + compilation_.type_name(p_loop->collection->type) + "'");
		return;
	}
	Place target;
	if (p_loop->declaration && p_loop->declaration->declarators.count == 1) {
		target = place_of_symbol(p_loop->declaration->declarators[0].symbol, 0, p_loop->declaration->declarators[0].offset);
	} else if (p_loop->target) {
		target = place_of(p_loop->target);
	} else {
		refuse(position, "this loop variable");
		return;
	}
	const uint32_t vector = reg(Kind::Obj);
	emit(Op::Move, position, vector, expression(p_loop->collection, Kind::Obj));
	// Nothing to go through if there is no vector.
	const uint32_t to_end = emit(Op::JumpIfNot, position, convert(vector, Kind::Bool, position), 0);
	const uint32_t at = reg(Kind::F64);
	emit(Op::ConstF64, position, at, number(0));
	const uint32_t top = here();
	emit(Op::Stmt, position);
	const uint32_t length = reg(Kind::U32);
	emit(Op::VecLength, position, length, vector);
	const uint32_t more = reg(Kind::Bool);
	emit(Op::LtF64, position, more, at, convert(length, Kind::F64, position));
	const uint32_t out = emit(Op::JumpIfNot, position, more, 0);
	uint32_t value = at;
	if (p_loop->each) {
		value = reg(module_.classes[*vector_class(p_loop->collection->type)].element);
		emit(Op::VecGet, position, value, vector, at);
	}
	store(target, convert(value, target.kind, position), position);
	open_loop();
	statement(p_loop->body);
	const uint32_t next_at = here();
	const uint32_t one = reg(Kind::F64);
	emit(Op::ConstF64, position, one, number(1));
	emit(Op::AddF64, position, at, at, one);
	emit(Op::Jump, position, top);
	patch(out, here());
	patch(to_end, here());
	finish_loop(next_at);
}

auto Lowerer::vector_to_engine(Expr *p_value, const Type *p_wanted) -> std::optional<uint32_t> {
	if (!is_vector(p_value->type)) {
		return std::nullopt;
	}
	const uint32_t position = p_value->offset;
	const std::optional<uint32_t> klass = vector_class(p_value->type);
	if (!klass || module_.classes[*klass].element == Kind::Obj) {
		// Elements that are script objects, or vectors themselves, have no form Godot can hold yet.
		refuse(position, "passing a Vector of script objects or of Vectors to Godot");
		return default_value(Kind::Val, position);
	}
	// Godot's name for what it takes here: a packed array, or an Array where it takes that or anything.
	std::string wanted = "Array";
	const bool named = p_wanted && p_wanted->kind == Type::Instance && is_godot(p_wanted->symbol) &&
			godot_name(p_wanted->symbol->name, std::string_view(), &wanted);
	if (p_wanted && p_wanted->kind != Type::Any && !named) {
		refuse(position, "passing a Vector where '" + compilation_.type_name(p_wanted) + "' is expected");
		return default_value(Kind::Val, position);
	}
	const uint32_t out = named ? engine_reg(Kind::Val, p_wanted) : reg(Kind::Val);
	emit(Op::HostCollection, position, out, expression(p_value, Kind::Obj), string(wanted));
	return out;
}

} // namespace ir
} // namespace kas3
