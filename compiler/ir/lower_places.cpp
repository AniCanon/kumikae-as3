#include "lower_dynamic.h"
#include "lower_engine.h"
#include "lower_library.h"
#include "lower_members.h"

namespace kas3 {
namespace ir {

void Lowerer::touch(const Symbol *p_class, uint32_t p_position) {
	auto found = classes_.find(p_class);
	if (found != classes_.end() && p_class != class_) {
		emit(Op::InitClass, p_position, found->second);
	}
}

uint32_t Lowerer::receiver(Expr *p_object) {
	if (p_object->kind == NodeKind::ThisExpr || p_object->kind == NodeKind::SuperExpr) {
		if (received_env_) {
			refuse(p_object->offset, "an instance member named inside a function expression"); // r0 is the environment there.
		}
		return 0;
	}
	uint32_t object = expression(p_object, Kind::Void);
	if (kind_at(object) != Kind::Obj) {
		refuse(p_object->offset, "a member of a value of type '" + compilation_.type_name(p_object->type) + "'");
		return default_value(Kind::Obj, p_object->offset);
	}
	// No check here: a missing object is reported where it is used, after whatever else the expression
	// evaluates first (a call's arguments, an assignment's value), as in Flash.
	return object;
}

uint32_t Lowerer::whole_of(const Place &p_place, uint32_t p_position) {
	if (p_place.part->value == NONE) {
		p_place.part->value = load(p_place.part->whole, p_position);
	}
	return p_place.part->value;
}

auto Lowerer::place_of_symbol(Symbol *p_symbol, uint32_t p_object, uint32_t p_position) -> Lowerer::Place {
	if (p_symbol && (p_symbol->kind == SymbolKind::Local || p_symbol->kind == SymbolKind::Parameter)) {
		if (std::optional<Place> shared = captured_place(p_symbol, p_position)) {
			return *shared; // A variable a function written inside shares, through its environment.
		}
	}
	Place place;
	if (!p_symbol) {
		refuse(p_position, "this name");
		return place;
	}
	auto local = locals_.find(p_symbol);
	if (local != locals_.end()) {
		place.where = Place::Register;
		place.index = local->second;
		place.kind = kind_at(local->second);
		return place;
	}
	auto global = globals_.find(p_symbol);
	if (global != globals_.end()) {
		if (p_symbol->owner) {
			touch(p_symbol->owner, p_position);
		}
		place.where = Place::GlobalSlot;
		place.index = global->second;
		place.kind = module_.globals[global->second].kind;
		return place;
	}
	auto field = fields_.find(p_symbol);
	if (field != fields_.end() && p_symbol->owner) {
		uint32_t owner = 0;
		if (class_index(p_symbol->owner, &owner)) {
			place.where = Place::FieldOf;
			place.index = field->second;
			place.object = p_object;
			place.kind = module_.classes[owner].fields[field->second].kind;
			return place;
		}
	}
	refuse(p_position, p_symbol->kind == SymbolKind::Property ? "a getter or setter"
															  : "'" + std::string(p_symbol->name) + "' used as a value");
	return place;
}

auto Lowerer::place_of(Expr *p_target, Expr *p_later) -> Lowerer::Place {
	if (std::optional<Place> by_name = Dynamic(*this).place(p_target, p_later)) {
		return *by_name;
	}
	if (std::optional<Place> of_vector = vector_place(p_target, p_later)) {
		return *of_vector;
	}
	if (std::optional<Place> of_library = Library::place(*this, p_target, p_later)) {
		return *of_library;
	}
	if (p_target->kind == NodeKind::Name) {
		Name *name = static_cast<Name *>(p_target);
		if (name->symbol && is_godot(name->symbol) && (name->symbol->kind == SymbolKind::Property || name->symbol->kind == SymbolKind::Field)) {
			return Engine(*this).engine_place(name->symbol, nullptr, name->offset);
		}
		const bool member = name->symbol && name->symbol->is_instance_member();
		if (member && fn().is_static) {
			refuse(name->offset, "this name");
			return Place();
		}
		if (std::optional<Place> property = Members(*this).property(name, nullptr, p_later)) {
			return *property;
		}
		// In a function expression, the object of the method it was written in.
		return place_of_symbol(name->symbol, name->symbol && name->symbol->is_instance_member() ? self(name->offset) : 0, name->offset);
	}
	if (p_target->kind == NodeKind::Member) {
		Member *member = static_cast<Member *>(p_target);
		Symbol *symbol = member->resolved();
		if (symbol && is_godot(symbol) && (symbol->kind == SymbolKind::Property || symbol->kind == SymbolKind::Field)) {
			return Engine(*this).engine_place(symbol, member->object, member->name_offset, p_later);
		}
		if (std::optional<Place> property = Members(*this).property(member, member->object, p_later)) {
			return *property;
		}
		if (symbol && (globals_.count(symbol) || !symbol->is_instance_member())) {
			return place_of_symbol(symbol, 0, member->name_offset);
		}
		// Is the class known before the object is evaluated? A String's `length` is not a field.
		if (symbol && !fields_.count(symbol)) {
			refuse(member->name_offset, symbol->kind == SymbolKind::Property ? "a getter or setter"
																		  : "'" + std::string(symbol->name) + "' used as a value");
			return Place();
		}
		uint32_t object = settled(receiver(member->object), p_later, member->name_offset);
		return place_of_symbol(symbol, object, member->name_offset);
	}
	if (p_target->kind == NodeKind::Index) {
		// An element of one of Godot's values: `vector[0]`.
		Index *index = static_cast<Index *>(p_target);
		Kind whole = Kind::Void;
		if (kind_of(index->object->type, &whole) && whole == Kind::Val) {
			Place place;
			if (!kind_of(p_target->type, &place.kind) || place.kind == Kind::Void) {
				refuse(p_target->offset, "an element of type '" + compilation_.type_name(p_target->type) + "'");
				return Place();
			}
			place.where = Place::Engine;
			place.type = p_target->type;
			place.of_value = true;
			if (index->object->kind == NodeKind::Name || index->object->kind == NodeKind::Member || Engine(*this).stored_element(index->object)) {
				place.part = &parts_.emplace_back();
				place.part->whole = place_of(index->object);
				whole_of(place, p_target->offset);
			} else {
				place.object = settled(expression(index->object, Kind::Void), index->index, p_target->offset);
			}
			place.key = settled(expression(index->index, Kind::Void), p_later, p_target->offset);
			if (kind_at(place.key) == Kind::Obj) {
				refuse(index->index->offset, "a script object as the key of a Godot value");
			}
			return place;
		}
		refuse(p_target->offset, "indexing with '[]'");
		return Place();
	}
	refuse(p_target->offset, "this kind of assignment target");
	return Place();
}

uint32_t Lowerer::load(const Place &p_place, uint32_t p_position) {
	switch (p_place.where) {
		case Place::Register:
			return p_place.index;
		case Place::FieldOf: {
			uint32_t out = reg(p_place.kind);
			emit(Op::GetField, p_position, out, p_place.object, p_place.index);
			return out;
		}
		case Place::GlobalSlot: {
			uint32_t out = reg(p_place.kind);
			emit(Op::GetGlobal, p_position, out, p_place.index);
			return out;
		}
		case Place::Engine: {
			if (p_place.script_object != NONE) {
				Engine(*this).owner_of(p_place.script_object, p_place.object, p_position);
			}
			uint32_t object = p_place.part ? whole_of(p_place, p_position) : p_place.object;
			uint32_t out = engine_reg(p_place.kind, p_place.type);
			if (p_place.key != NONE) {
				emit(Op::HostIndexGet, p_position, out, object, p_place.key);
			} else {
				emit(Op::HostGet, p_position, out, object, p_place.index);
			}
			return out;
		}
		case Place::Element:
		case Place::Length:
		case Place::Fixed: {
			uint32_t out = reg(p_place.kind);
			if (p_place.where == Place::Element) {
				emit(Op::VecGet, p_position, out, p_place.object, p_place.key);
			} else {
				emit(p_place.where == Place::Length ? Op::VecLength : Op::VecFixed, p_position, out, p_place.object);
			}
			return out;
		}
		case Place::Accessor:
			return Members(*this).get(p_place, p_position);
		case Place::Property: {
			uint32_t out = reg(Kind::Any);
			emit(Op::DynGet, p_position, out, p_place.object, p_place.key);
			return out;
		}
		case Place::Library:
			return Library::load(*this, p_place, p_position);
		default:
			return default_value(Kind::F64, p_position);
	}
}

void Lowerer::store(const Place &p_place, uint32_t p_value, uint32_t p_position) {
	switch (p_place.where) {
		case Place::Register:
			if (p_place.constant) {
				refuse(p_position, "changing a constant of Godot's");
				break;
			}
			if (p_place.index != p_value) {
				emit(Op::Move, p_position, p_place.index, p_value);
			}
			break;
		case Place::FieldOf:
			emit(Op::SetField, p_position, p_place.object, p_place.index, p_value);
			break;
		case Place::GlobalSlot:
			emit(Op::SetGlobal, p_position, p_place.index, p_value);
			break;
		case Place::Engine:
			if (p_place.part) {
				uint32_t whole = whole_of(p_place, p_position);
				if (p_place.part->whole.constant) {
					refuse(p_position, "changing one part of a constant of Godot's");
					break;
				}
				if (p_place.key != NONE) {
					emit(Op::HostIndexSet, p_position, whole, p_place.key, p_value);
				} else {
					emit(Op::HostSet, p_position, whole, p_place.index, p_value);
				}
				store(p_place.part->whole, whole, p_position);
			} else if (p_place.of_value) {
				// `a.normalized().x = 5` would change a copy that nobody keeps.
				refuse(p_position, "changing one part of a Godot value that is not stored anywhere");
			} else {
				if (p_place.script_object != NONE) {
					Engine(*this).owner_of(p_place.script_object, p_place.object, p_position);
				}
				emit(Op::HostSet, p_position, p_place.object, p_place.index, p_value);
			}
			break;
		case Place::Element:
			emit(Op::VecSet, p_position, p_place.object, p_place.key, p_value);
			break;
		case Place::Length:
			emit(Op::VecSetLength, p_position, p_place.object, p_value);
			break;
		case Place::Fixed:
			emit(Op::VecSetFixed, p_position, p_place.object, p_value);
			break;
		case Place::Accessor:
			Members(*this).set(p_place, p_value, p_position);
			break;
		case Place::Property:
			emit(Op::DynSet, p_position, p_place.object, p_place.key, convert(p_value, Kind::Any, p_position));
			break;
		case Place::Library:
			Library::store(*this, p_place, p_value, p_position);
			break;
		default:
			break;
	}
}

} // namespace ir
} // namespace kas3
