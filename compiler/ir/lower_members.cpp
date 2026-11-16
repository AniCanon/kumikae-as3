// Getters, setters and interfaces.
//
// Getters and setters. Each half is a function of its class, "Class/get x" and "Class/set x", and an
// instance one has a slot in the method table under that name, so that an override takes its place. A
// property is a place (Place::Accessor): reading it calls the getter, writing it the setter, so `o.x += 1`
// and `o.x++` work out `o` once and call each half once, in AS3's order.
//
// Interfaces. Each is a class of the module with no objects (Class::interface), so that a value typed as
// one is an object and `is`, `as` and every check of a class work as they do for a class. A call through
// one goes by the method's name, as in AVM2: each name an interface declares has a number, and each class
// a table from those numbers to the functions it runs (CallInterface).
#include "lower_classes.h"
#include "lower_members.h"

namespace kas3 {
namespace ir {

void Members::declare_accessor(uint32_t p_class, FunctionDecl *p_function) {
	const bool is_static = (p_function->modifiers.bits & Modifiers::Static) != 0;
	const uint32_t made = Classes(l_).add_function(p_class, l_.module_.classes[p_class].name + "/" + member_key(p_function), p_function->name_offset, is_static);
	l_.functions_[p_function] = made;
	l_.module_.functions[made].is_public = (p_function->modifiers.bits & Modifiers::Public) != 0; // Godot reaches it.
	if (!is_static) {
		Classes(l_).give_method_slot(p_class, p_function, made);
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
	const bool by_interface = symbol->owner && symbol->owner->kind == SymbolKind::Interface;
	if (!(by_interface ? l_.slots_ : l_.functions_).count(getter ? getter : setter)) {
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
		if (p_object && own && l_.received_env_) {
			// `this` and `super` written in a function expression are not the method's object (AS3).
			l_.refuse(position, "'this' or 'super' inside a function expression");
			return Place();
		}
		place.object = !p_object ? l_.self(position) : own ? 0 : l_.settled(l_.receiver(p_object), p_later, position);
		// `super.x` is the base's own; a private property is never overridden.
		const bool exact = (symbol->modifiers & Modifiers::Private) || (p_object && p_object->kind == NodeKind::SuperExpr);
		place.dispatch = by_interface ? Place::Interface : exact ? Place::Direct : Place::Virtual;
		if (!l_.class_index(symbol->owner, &place.declared)) {
			place.declared = NONE;
		}
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
	const Op op = p_place.dispatch == Place::Direct ? Op::Call : p_place.dispatch == Place::Virtual ? Op::CallVirtual : Op::CallInterface;
	const uint32_t at = l_.emit(op, p_position, p_out, p_function, l_.arguments(std::move(list)));
	if (op == Op::CallVirtual && p_place.object != 0 && p_place.declared != NONE) {
		l_.fn().virtual_receivers.emplace_back(at, p_place.declared);
	}
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

uint32_t Members::method_number(const std::string &p_name) {
	std::vector<std::string> &names = l_.module_.interface_methods;
	const auto found = std::find(names.begin(), names.end(), p_name);
	if (found != names.end()) {
		return static_cast<uint32_t>(found - names.begin());
	}
	names.push_back(p_name);
	return static_cast<uint32_t>(names.size()) - 1;
}

void Members::add_interface(const Symbol *p_interface, std::vector<const Symbol *> &r_list) const {
	if (!p_interface || !p_interface->klass || !l_.classes_.count(p_interface) ||
			std::find(r_list.begin(), r_list.end(), p_interface) != r_list.end()) {
		return;
	}
	r_list.push_back(p_interface);
	for (const Symbol *parent : p_interface->klass->interfaces) {
		add_interface(parent, r_list);
	}
}

void Members::reserve_interfaces(const std::vector<Unit *> &p_units) {
	std::vector<InterfaceDecl *> found;
	for (Unit *unit : p_units) {
		for (const List<Node *> *definitions : { unit->tree->package ? &unit->tree->package->definitions : nullptr, &unit->tree->definitions }) {
			for (Node *definition : definitions ? *definitions : List<Node *>()) {
				InterfaceDecl *decl = definition->kind == NodeKind::InterfaceDecl ? static_cast<InterfaceDecl *>(definition) : nullptr;
				if (decl && decl->symbol && decl->symbol->klass && !l_.classes_.count(decl->symbol)) {
					found.push_back(decl);
				}
			}
		}
	}
	for (InterfaceDecl *decl : found) {
		Class klass;
		klass.name = l_.class_name(decl->symbol);
		klass.is_interface = true;
		l_.classes_[decl->symbol] = static_cast<uint32_t>(l_.module_.classes.size());
		l_.module_.classes.push_back(std::move(klass));
		for (FunctionDecl *method : decl->methods) {
			l_.slots_[method] = method_number(member_key(method));
		}
	}
	// What each extends, at any depth, once every one has its place.
	for (InterfaceDecl *decl : found) {
		std::vector<const Symbol *> extended;
		for (const Symbol *parent : decl->symbol->klass->interfaces) {
			add_interface(parent, extended);
		}
		for (const Symbol *parent : extended) {
			l_.module_.classes[l_.classes_[decl->symbol]].interfaces.push_back(l_.classes_[parent]);
		}
	}
}

void Members::implement(uint32_t p_class, const Symbol *p_symbol) {
	// What the class and its bases name, and what those extend.
	std::vector<const Symbol *> promised;
	for (const Symbol *at = p_symbol; at && at->klass; at = at->klass->base) {
		for (const Symbol *named : at->klass->interfaces) {
			add_interface(named, promised);
		}
	}
	Class &klass = l_.module_.classes[p_class];
	const std::unordered_map<std::string, uint32_t> &slots = l_.slot_names_[p_class];
	for (const Symbol *promise : promised) {
		klass.interfaces.push_back(l_.classes_[promise]);
		for (const FunctionDecl *method : static_cast<const InterfaceDecl *>(promise->node)->methods) {
			const uint32_t number = l_.slots_[method];
			const auto slot = slots.find(member_key(method));
			if (slot == slots.end()) {
				// The checker found it, so the class has it from Godot's API or the built-in library, which
				// have no function in the module to put in the table.
				l_.refuse(p_symbol->offset, "implementing '" + member_key(method) + "' of " + std::string(promise->name) +
						" with a member of Godot's or of the built-in library");
				continue;
			}
			if (klass.interface_methods.size() <= number) {
				klass.interface_methods.resize(number + 1, NONE);
			}
			klass.interface_methods[number] = klass.methods[slot->second];
		}
	}
}

// The number of an interface's method, or nothing if the symbol is not the method of one of the program's.
std::optional<uint32_t> Members::interface_method(Symbol *p_method, Expr *p_callee) {
	if (!p_method || p_method->kind != SymbolKind::Method || !p_method->owner || p_method->owner->kind != SymbolKind::Interface) {
		return std::nullopt;
	}
	const auto found = l_.slots_.find(declaration(p_method));
	if (found == l_.slots_.end() || p_callee->kind != NodeKind::Member) {
		l_.refuse(p_callee->offset, "calling '" + std::string(p_method->name) + "'");
		return NONE;
	}
	return found->second;
}

std::optional<uint32_t> Members::interface_call(Call *p_call, Symbol *p_method) {
	const std::optional<uint32_t> number = interface_method(p_method, p_call->callee);
	if (!number || *number == NONE) {
		return number ? std::optional<uint32_t>(0) : std::nullopt;
	}
	const uint32_t position = p_call->offset;
	std::vector<uint32_t> list = { l_.settled(l_.receiver(static_cast<Member *>(p_call->callee)->object), Lowerer::writer(p_call->arguments), position) };
	// What is left out is left to the function that runs, which has its own defaults.
	l_.call_arguments(declaration(p_method), p_call->arguments, position, list);
	Kind kind = Kind::Void;
	const uint32_t out = l_.call_result(p_method, position, &kind);
	l_.emit(Op::CallInterface, position, out, *number, l_.arguments(std::move(list)));
	return out == NONE ? 0 : out;
}

std::optional<uint32_t> Members::interface_value(Expr *p_expression, Symbol *p_method) {
	const std::optional<uint32_t> number = interface_method(p_method, p_expression);
	if (!number || *number == NONE) {
		return number ? std::optional<uint32_t>(l_.default_value(Kind::Obj, p_expression->offset)) : std::nullopt;
	}
	const uint32_t object = l_.receiver(static_cast<Member *>(p_expression)->object);
	const uint32_t out = l_.reg(Kind::Obj);
	l_.emit(Op::MakeInterfaceMethod, p_expression->offset, out, *number, object);
	return out;
}

} // namespace ir
} // namespace kas3
