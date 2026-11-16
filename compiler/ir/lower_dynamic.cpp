#include "lower_dynamic.h"
#include "lower_functions.h"
#include "lower_statements.h"

namespace kas3 {
namespace ir {

bool is_dictionary(const Symbol *p_symbol) {
	return p_symbol && p_symbol->kind == SymbolKind::Class && p_symbol->unit && p_symbol->unit->is_builtin && !p_symbol->owner &&
			p_symbol->name == "Dictionary";
}

uint32_t Dynamic::table_class(bool p_dictionary) {
	uint32_t &index = p_dictionary ? l_.module_.dictionary_class : l_.module_.object_class;
	if (index == NONE) {
		Class klass;
		klass.name = p_dictionary ? "flash.utils.Dictionary" : "Object";
		klass.table = true;
		klass.dictionary = p_dictionary;
		klass.built_in = true;
		l_.module_.classes.push_back(std::move(klass));
		index = static_cast<uint32_t>(l_.module_.classes.size()) - 1;
	}
	return index;
}

// What a property reached by name may need as the program runs: ReferenceError for a sealed object's
// missing one, function values for a method read by name, and Arrays for the keys a loop goes through.
void Dynamic::use_classes() {
	if (l_.module_.reference_error_class == NONE) {
		if (Symbol *reference_error = l_.compilation_.builtin("ReferenceError")) {
			l_.class_index(reference_error, &l_.module_.reference_error_class);
		}
	}
	uint32_t ignored = NONE;
	l_.class_index(l_.function_, &ignored);
	if (l_.array_) {
		l_.vector_class(l_.compilation_.instance_type(l_.array_));
	}
	Functions(l_).use_argument_error();
}

bool Dynamic::by_name(const Expr *p_holder) const {
	Kind kind = Kind::Void;
	if (!p_holder || Lowerer::is_null(p_holder) || !l_.kind_of(p_holder->type, &kind)) {
		return false;
	}
	if (kind == Kind::Any) {
		return true;
	}
	const Type *type = p_holder->type;
	const Symbol *symbol = type && type->kind == Type::Instance ? type->symbol : nullptr;
	return kind == Kind::Obj && type->kind != Type::Vector && symbol && symbol != l_.function_;
}

// The value that holds the property, as an untyped or an object register.
uint32_t Dynamic::holder(Expr *p_holder, Expr *p_later, uint32_t p_position) {
	uint32_t value = p_holder->kind == NodeKind::ThisExpr && !l_.fn().is_static && !l_.received_env_ ? 0 : l_.expression(p_holder, Kind::Void);
	if (l_.kind_at(value) != Kind::Obj && l_.kind_at(value) != Kind::Any) {
		value = l_.convert(value, Kind::Any, p_position);
	}
	return l_.settled(value, p_later, p_position);
}

// A key as a String when it is one, and otherwise untyped: the interpreter makes it the holder's kind of key.
uint32_t Dynamic::key(Expr *p_key, Expr *p_later, uint32_t p_position) {
	const uint32_t value = l_.is_string_type(p_key->type) ? l_.expression(p_key, Kind::Str) : l_.any_value(p_key);
	return l_.settled(value, p_later, p_position);
}

uint32_t Dynamic::name(std::string_view p_name, uint32_t p_position) {
	const uint32_t out = l_.reg(Kind::Str);
	l_.emit(Op::ConstStr, p_position, out, l_.string(std::string(p_name)));
	return out;
}

auto Dynamic::place(Expr *p_target, Expr *p_later) -> std::optional<Place> {
	Place place;
	place.where = Place::Property;
	place.kind = Kind::Any;
	if (p_target->kind == NodeKind::Member) {
		Member *member = static_cast<Member *>(p_target);
		// One of Object's methods may be replaced on an Object (`o.toString = f`), as AS3 keeps them on
		// its prototype.
		const Symbol *symbol = member->resolved();
		Kind kind = Kind::Void;
		const bool replaced = symbol && symbol->owner == l_.object_ && l_.kind_of(member->object->type, &kind) && kind == Kind::Any;
		if ((symbol && !replaced) || member->object->kind == NodeKind::SuperExpr || !by_name(member->object)) {
			return std::nullopt;
		}
		use_classes();
		place.object = holder(member->object, p_later, member->name_offset);
		place.key = name(member->name, member->name_offset);
		return place;
	}
	if (p_target->kind != NodeKind::Index) {
		return std::nullopt;
	}
	Index *index = static_cast<Index *>(p_target);
	Kind key_kind = Kind::Void;
	const bool numeric = l_.kind_of(index->index->type, &key_kind) && Lowerer::is_numeric(key_kind);
	if (!by_name(index->object) || (l_.is_vector(index->object->type) && numeric) || index->object->kind == NodeKind::SuperExpr) {
		return std::nullopt;
	}
	use_classes();
	place.object = holder(index->object, Lowerer::writes_locals(index->index) ? index->index : p_later, p_target->offset);
	place.key = key(index->index, p_later, p_target->offset);
	return place;
}

// {a: 1, "b": 2, 3: x}: made empty, then each property set in order, its value worked out after the ones
// before it are set. A number written as a key is the number's text, as the interpreter writes it.
uint32_t Dynamic::literal(ObjectLit *p_literal) {
	const uint32_t position = p_literal->offset;
	use_classes();
	const uint32_t made = l_.reg(Kind::Obj);
	l_.emit(Op::TableNew, position, made, table_class(false), NONE);
	for (const ObjectField &field : p_literal->fields) {
		uint32_t key = NONE;
		if (!field.key_is_string && !field.key.empty() && ((field.key[0] >= '0' && field.key[0] <= '9') || field.key[0] == '.')) {
			NumberLit written;
			written.text = field.key;
			key = l_.reg(Kind::F64);
			l_.emit(Op::ConstF64, field.offset, key, l_.number(Lowerer::literal_value(&written)));
			key = l_.convert(key, Kind::Any, field.offset);
		} else {
			key = name(field.key_is_string ? Lowerer::unescape("\"" + std::string(field.key) + "\"") : std::string(field.key), field.offset);
		}
		l_.emit(Op::DynSet, field.offset, made, key, l_.any_value(field.value));
	}
	return l_.convert(made, Kind::Any, position);
}

auto Dynamic::made(New *p_new, const Symbol *p_class) -> std::optional<uint32_t> {
	const bool dictionary = is_dictionary(p_class);
	if (!p_class || (p_class != l_.object_ && !dictionary)) {
		return std::nullopt;
	}
	const uint32_t position = p_new->offset;
	if (p_new->arguments.count > (dictionary ? 1u : 0u)) {
		l_.refuse(p_new->callee->offset, dictionary ? "a Dictionary made with more than weakKeys" : "an Object made from a value");
		return l_.default_value(Kind::Obj, position);
	}
	use_classes();
	const uint32_t weak = p_new->arguments.count ? l_.expression(p_new->arguments[0], Kind::Bool) : NONE;
	const uint32_t out = l_.reg(Kind::Obj);
	l_.emit(Op::TableNew, position, out, table_class(dictionary), weak);
	return dictionary ? out : l_.convert(out, Kind::Any, position);
}

// A call of a property by its name: worked out after the arguments, as Flash looks the property up then.
// Object's own methods (hasOwnProperty and the rest) are called this way on anything: the interpreter
// answers them when the value has no property of that name. toString() is left to the library.
auto Dynamic::call(Call *p_call) -> std::optional<uint32_t> {
	if (p_call->callee->kind != NodeKind::Member) {
		return std::nullopt;
	}
	Member *callee = static_cast<Member *>(p_call->callee);
	const Symbol *symbol = callee->resolved();
	const bool object_method = symbol && symbol->owner == l_.object_ && symbol->name != "toString";
	if (callee->object->kind == NodeKind::SuperExpr || (symbol && !object_method) || (!symbol && callee->name == "toString" && p_call->arguments.count == 0)) {
		return std::nullopt;
	}
	Kind kind = Kind::Void;
	if (!object_method && !by_name(callee->object)) {
		return std::nullopt;
	}
	if (object_method && (!l_.kind_of(callee->object->type, &kind) || is_engine(kind) || kind == Kind::Void)) {
		return std::nullopt; // On one of Godot's values: refused where the call is lowered.
	}
	use_classes();
	const uint32_t position = p_call->offset;
	std::vector<uint32_t> list;
	const uint32_t object = holder(callee->object, Lowerer::writer(p_call->arguments), position);
	list.push_back(name(callee->name, callee->name_offset));
	for (uint32_t i = 0; i < p_call->arguments.count; i++) {
		Expr *later = Lowerer::writer(p_call->arguments, i + 1);
		list.push_back(l_.settled(l_.any_value(p_call->arguments[i]), later, position));
	}
	const uint32_t out = l_.reg(Kind::Any);
	l_.emit(Op::DynCall, position, out, object, l_.arguments(std::move(list)));
	return out;
}

// delete o.a, delete o[k]: false for a property that can't go. Anything else written after `delete` is
// worked out, and is true, as in AS3.
uint32_t Dynamic::remove(Unary *p_delete) {
	const uint32_t position = p_delete->offset;
	Expr *target = p_delete->operand;
	Expr *holder_written = target->kind == NodeKind::Member ? static_cast<Member *>(target)->object
			: target->kind == NodeKind::Index					? static_cast<Index *>(target)->object
																: nullptr;
	const uint32_t out = l_.reg(Kind::Bool);
	const Symbol *declared = target->kind == NodeKind::Member ? static_cast<Member *>(target)->resolved() : nullptr;
	if (declared && declared->is_instance_member() && holder_written->kind != NodeKind::SuperExpr) {
		// A member the class declares can't be deleted: false, once what holds it is worked out.
		l_.expression(holder_written, Kind::Void);
		l_.emit(Op::ConstBool, position, out, 0);
		return out;
	}
	if (!holder_written || !by_name(holder_written) || declared) {
		l_.refuse(target->offset, "'delete' of this");
		return out;
	}
	use_classes();
	uint32_t object = NONE, key = NONE;
	if (target->kind == NodeKind::Member) {
		object = holder(holder_written, nullptr, position);
		key = name(static_cast<Member *>(target)->name, position);
	} else {
		Expr *index = static_cast<Index *>(target)->index;
		object = holder(holder_written, index, position);
		key = this->key(index, nullptr, position);
	}
	l_.emit(Op::DynDelete, position, out, object, key);
	return out;
}

// key in holder: the key worked out first.
uint32_t Dynamic::in(Binary *p_in) {
	const uint32_t position = p_in->offset;
	const uint32_t out = l_.reg(Kind::Bool);
	Kind kind = Kind::Void;
	if (!l_.kind_of(p_in->right->type, &kind) || (kind != Kind::Any && kind != Kind::Obj)) {
		l_.refuse(p_in->right->offset, "'in' on a value of type '" + l_.compilation_.type_name(p_in->right->type) + "'");
		return out;
	}
	use_classes();
	const uint32_t key = this->key(p_in->left, p_in->right, position);
	const uint32_t object = holder(p_in->right, nullptr, position);
	l_.emit(Op::DynIn, position, out, object, key);
	return out;
}

auto Dynamic::to_engine(Expr *p_value, const Type *p_wanted) -> std::optional<uint32_t> {
	const Type *type = p_value->type;
	const Symbol *symbol = type && type->kind == Type::Instance ? type->symbol : nullptr;
	const Symbol *wanted = p_wanted && p_wanted->kind == Type::Instance ? p_wanted->symbol : nullptr;
	const bool dictionary = wanted && Lowerer::is_godot(wanted) && wanted->name == "GodotDictionary";
	// A `*` where Godot takes a Dictionary may hold an Object as well as one of Godot's: Godot's call
	// says when it holds neither.
	const bool untyped = l_.untyped_type(type) && dictionary;
	if (!untyped && (!symbol || (symbol != l_.object_ && !is_dictionary(symbol)))) {
		return std::nullopt;
	}
	if (p_wanted && p_wanted->kind != Type::Any && !dictionary) {
		return std::nullopt;
	}
	return l_.any_value(p_value);
}

// for (k in o) and for each (v in o): an Array's or a Vector's elements first, by index, then the
// properties there were when the loop began (vm/interpreter_property_calls.cpp). Nothing for null.
void Dynamic::loop(ForIn *p_loop) {
	const uint32_t position = p_loop->offset;
	Kind kind = Kind::Void;
	if (!l_.kind_of(p_loop->collection->type, &kind) || (kind != Kind::Any && kind != Kind::Obj)) {
		l_.refuse(position, p_loop->each ? "'for each'" : "'for in'");
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
	use_classes();
	const uint32_t holder = l_.reg(kind);
	l_.emit(Op::Move, position, holder, l_.expression(p_loop->collection, kind));
	const uint32_t keys = l_.reg(Kind::Obj);
	l_.emit(Op::DynKeys, position, keys, holder);
	const uint32_t cursor = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, position, cursor, l_.number(0));
	const uint32_t both = l_.arguments({ holder, keys });
	const uint32_t top = l_.here();
	l_.emit(Op::Stmt, position);
	const uint32_t more = l_.reg(Kind::Bool);
	l_.emit(Op::DynNext, position, more, cursor, both);
	const uint32_t out = l_.emit(Op::JumpIfNot, position, more, 0);
	uint32_t value = l_.reg(Kind::Any);
	l_.emit(Op::DynKey, position, value, cursor, both);
	if (p_loop->each) {
		const uint32_t element = l_.reg(Kind::Any);
		l_.emit(Op::DynGet, position, element, holder, value);
		value = element;
	}
	l_.store(target, l_.convert(value, target.kind, position), position);
	Statements(l_).open_loop();
	Statements(l_).statement(p_loop->body);
	const uint32_t next_at = l_.here();
	l_.emit(Op::Jump, position, top);
	l_.patch(out, l_.here());
	Statements(l_).finish_loop(next_at);
}

} // namespace ir
} // namespace kas3
