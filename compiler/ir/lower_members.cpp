// Getters and setters. Each half is a function of its class, "Class/get x" and "Class/set x", and an
// instance one has a slot in the method table under that name, so that an override takes its place. A
// property is a place (Place::Accessor): reading it calls the getter, writing it the setter, so `o.x += 1`
// and `o.x++` work out `o` once and call each half once, in AS3's order.
#include "lower_members.h"

namespace kas3 {
namespace ir {

void Members::declare_accessor(uint32_t p_class, FunctionDecl *p_function) {
	const bool is_static = (p_function->modifiers.bits & Modifiers::Static) != 0;
	const uint32_t made = l_.add_function(p_class, l_.module_.classes[p_class].name + "/" + member_key(p_function), p_function->name_offset, is_static);
	l_.functions_[p_function] = made;
	if (!is_static) {
		l_.give_method_slot(p_class, p_function, made);
	}
}

uint32_t Members::half(const FunctionDecl *p_function, Place::Dispatch p_dispatch) {
	if (!p_function) {
		return NONE;
	}
	const auto &table = p_dispatch == Place::Direct ? l_.functions_ : l_.slots_;
	const auto found = table.find(p_function);
	return found == table.end() ? NONE : found->second;
}

std::optional<Place> Members::property(Expr *p_target, Expr *p_object, Expr *p_later) {
	const bool member = p_target->kind == NodeKind::Member;
	Symbol *symbol = member ? static_cast<Member *>(p_target)->resolved()
			: p_target->kind == NodeKind::Name ? static_cast<Name *>(p_target)->symbol
											   : nullptr;
	if (!symbol || symbol->kind != SymbolKind::Property || Lowerer::is_godot(symbol) || l_.fields_.count(symbol)) {
		return std::nullopt;
	}
	const uint32_t position = member ? static_cast<Member *>(p_target)->name_offset : p_target->offset;
	const Type *object_type = p_object ? p_object->type : nullptr;
	Symbol *through = object_type && object_type->kind == Type::Instance ? object_type->symbol : nullptr;
	FunctionDecl *getter = l_.compilation_.property_half(through, symbol, FunctionDecl::Getter);
	FunctionDecl *setter = l_.compilation_.property_half(through, symbol, FunctionDecl::Setter);
	if (!l_.functions_.count(getter ? getter : setter)) {
		return std::nullopt; // Not one of the program's: refused where it is used.
	}
	Place place;
	place.where = Place::Accessor;
	place.type = p_target->type;
	if (!l_.kind_of(p_target->type, &place.kind) || place.kind == Kind::Void) {
		l_.refuse(position, "a property of type '" + l_.compilation_.type_name(p_target->type) + "'");
		return Place();
	}
	if (symbol->is_static()) {
		l_.touch(symbol->owner, position); // Its class is set up first, as for a static method.
		place.object = NONE;
	} else {
		const bool own = !p_object || p_object->kind == NodeKind::ThisExpr || p_object->kind == NodeKind::SuperExpr;
		place.object = own ? 0 : l_.settled(l_.receiver(p_object), p_later, position);
		// `super.x` is the base's own; a private property is never overridden.
		const bool exact = (symbol->modifiers & Modifiers::Private) || (p_object && p_object->kind == NodeKind::SuperExpr);
		place.dispatch = exact ? Place::Direct : Place::Virtual;
	}
	place.getter = half(getter, place.dispatch);
	place.setter = half(setter, place.dispatch);
	return place;
}

// One half called: with the object first unless the property is static, then the value for a setter. A
// missing object is reported here, after whatever the place's value needed was worked out.
void Members::call(const Place &p_place, uint32_t p_function, uint32_t p_out, uint32_t p_value, uint32_t p_position) {
	std::vector<uint32_t> list;
	if (p_place.object != NONE) {
		list.push_back(p_place.object);
	}
	if (p_value != NONE) {
		list.push_back(p_value);
	}
	if (p_place.dispatch == Place::Direct && p_place.object != NONE && p_place.object != 0) {
		l_.emit(Op::NullCheck, p_position, p_place.object);
	}
	const Op op = p_place.dispatch == Place::Direct ? Op::Call : Op::CallVirtual;
	l_.emit(op, p_position, p_out, p_function, l_.arguments(std::move(list)));
}

uint32_t Members::get(const Place &p_place, uint32_t p_position) {
	if (p_place.getter == NONE) {
		l_.refuse(p_position, "reading a property that has no getter");
		return l_.default_value(p_place.kind, p_position);
	}
	const uint32_t out = l_.reg(p_place.kind);
	call(p_place, p_place.getter, out, NONE, p_position);
	return out;
}

void Members::set(const Place &p_place, uint32_t p_value, uint32_t p_position) {
	if (p_place.setter == NONE) {
		l_.refuse(p_position, "writing a property that has no setter");
		return;
	}
	call(p_place, p_place.setter, NONE, p_value, p_position);
}

} // namespace ir
} // namespace kas3
