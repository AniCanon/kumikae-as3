#include "lower_statements.h"
#include "lower_vectors.h"

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
	klass.element_check = check_for(p_type->element, element).value_or(NONE); // Before the class is added: it may add others.
	if (element == Kind::Val && of && is_godot(of)) {
		klass.element_type = godot_name(of->name, std::string_view()).value_or(std::string());
		klass.element_zero = of->klass && value_has_parts(of);
	}
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
			// Into a Vector.<Object>, undefined is null; an Array's element is untyped and keeps it.
			const ir::Class &made = l_.module_.classes[*klass];
			value = made.array ? l_.expression(element, made.element) : l_.typed(element, p_literal->type->element, made.element);
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
		place.type = p_target->type;
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
	const uint32_t collection = l_.engine_reg(Kind::Val, whole);
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

// Vector.<T>(x): the conversion written as a call.
uint32_t Vectors::vector_from(Call *p_call) {
	const uint32_t position = p_call->offset;
	const Type *written = p_call->callee->type && p_call->callee->type->kind == Type::ClassObject ? p_call->callee->type->element : nullptr;
	const std::optional<uint32_t> klass = l_.is_vector(written) ? l_.vector_class(written) : std::nullopt;
	if (!klass) {
		l_.refuse(p_call->callee->offset, "this call");
		return l_.default_value(Kind::Obj, position);
	}
	if (p_call->arguments.count != 1) {
		l_.error(p_call->callee->offset, "a conversion to a Vector takes 1 argument but " + std::to_string(p_call->arguments.count) + " were given");
		return l_.default_value(Kind::Obj, position);
	}
	const uint32_t value = l_.any_value(p_call->arguments[0]);
	const uint32_t out = l_.reg(Kind::Obj);
	l_.emit(Op::VecFrom, position, out, value, *klass);
	return out;
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
	const std::optional<uint32_t> godot = p_wanted && p_wanted->kind == Type::Instance && Lowerer::is_godot(p_wanted->symbol)
			? l_.engine_entry(p_wanted->symbol->name)
			: std::nullopt;
	const bool named = godot.has_value();
	const std::optional<uint32_t> wanted = named ? godot : l_.engine_entry("GodotArray");
	if ((p_wanted && p_wanted->kind != Type::Any && !named) || !wanted) {
		l_.refuse(position, "passing a Vector where '" + l_.compilation_.type_name(p_wanted) + "' is expected");
		return l_.default_value(Kind::Val, position);
	}
	const uint32_t out = named ? l_.engine_reg(Kind::Val, p_wanted) : l_.engine_reg(Kind::Val, "Array");
	l_.emit(Op::HostCollection, position, out, l_.expression(p_value, Kind::Obj), *wanted);
	return out;
}

} // namespace ir
} // namespace kas3
