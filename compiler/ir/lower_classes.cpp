#include "lower_classes.h"
#include "lower_dynamic.h"
#include "lower_library.h"
#include "lower_members.h"

namespace kas3 {
namespace ir {

void Classes::reserve_class(ClassDecl *p_decl) {
	Symbol *symbol = p_decl->symbol;
	if (!symbol || !symbol->klass || l_.classes_.count(symbol)) {
		return;
	}
	Class klass;
	klass.name = l_.class_name(symbol);
	l_.classes_[symbol] = (uint32_t)l_.module_.classes.size();
	l_.module_.classes.push_back(std::move(klass));
}

// What a class starts with: a script's hold on its Godot object, or what its base class has.
Class Classes::inherited_class(ClassDecl *p_decl) {
	const Symbol *symbol = p_decl->symbol;
	Class klass;
	klass.name = l_.class_name(symbol);
	Symbol *base = symbol->klass->base;
	std::string godot_base;
	if (base && Lowerer::is_godot(base) && l_.is_godot_object(base) && godot_name(base->name, std::string_view(), &godot_base)) {
		// A script: it is attached to an object of that Godot class, kept in its first field.
		klass.native_base = godot_base;
		klass.fields.push_back({ "$owner", Kind::Ref });
	} else if (base && base != l_.object_) {
		// A Dictionary's objects are tables, with no fields to add to; a RegExp's or a Date's are the library's.
		if (is_dictionary(base) || Library::holds(base) || !l_.class_index(base, &klass.base)) {
			l_.refuse(p_decl->extends ? p_decl->extends->offset : p_decl->name_offset, "extending '" + std::string(base->name) + "'");
		} else {
			const Class &from = l_.module_.classes[klass.base];
			klass.fields = from.fields;
			klass.methods = from.methods;
			klass.named_methods = from.named_methods;
			klass.to_string = from.to_string;
			klass.native_base = from.native_base;
			klass.virtuals = from.virtuals;
		}
	}
	klass.counted = !klass.native_base.empty() && l_.extends_counted(symbol);
	return klass;
}

uint32_t Classes::add_function(uint32_t p_class, const std::string &p_name, uint32_t p_position, bool p_static) {
	Function function;
	function.name = p_name;
	function.file = l_.file(l_.unit_);
	function.position = p_position;
	function.owner = p_class;
	function.is_static = p_static;
	l_.module_.functions.push_back(std::move(function));
	return (uint32_t)l_.module_.functions.size() - 1;
}

// The first value Godot is told of for an exported field: a constant written with it, or what it holds when
// none is written. Anything else is no constant: Godot is told of none.
// The checker allows only what export_constant() says is one.
static Constant first_value(const Expr *p_written, Kind p_kind, const Type *p_type, double (*p_number)(const NumberLit *),
		std::string (*p_text)(std::string_view)) {
	Constant out;
	bool negative = false;
	const NumberLit *number = written_number(p_written, &negative);
	if (!p_written && p_kind == Kind::Val && p_type && p_type->symbol && p_type->symbol->klass && value_has_parts(p_type->symbol)) {
		// It starts at its type's zero, as the constructor makes it.
		out.form = godot_name(p_type->symbol->name, std::string_view(), &out.text) ? Constant::Form::Value : Constant::Form::None;
	} else if (!p_written) {
		out.form = Constant::Form::Empty;
	} else if (number) {
		out.form = Constant::Form::Number;
		out.number = (negative ? -1 : 1) * p_number(number);
	} else if (p_written->kind == NodeKind::New && p_written->type && p_written->type->symbol && export_constant(p_written)) {
		out.form = godot_name(p_written->type->symbol->name, std::string_view(), &out.text) ? Constant::Form::Value : Constant::Form::None;
		for (const Expr *argument : static_cast<const New *>(p_written)->arguments) {
			const NumberLit *part = written_number(argument, &negative);
			out.parts.push_back((negative ? -1 : 1) * p_number(part));
		}
	} else if (p_written->kind == NodeKind::StringLit) {
		out.form = Constant::Form::Text;
		out.text = p_text(static_cast<const StringLit *>(p_written)->text);
	} else if (p_written->kind == NodeKind::BoolLit) {
		out.form = Constant::Form::Boolean;
		out.number = static_cast<const BoolLit *>(p_written)->value ? 1 : 0;
	} else if (p_written->kind == NodeKind::NullLit) {
		out.form = Constant::Form::Null;
	}
	return out;
}

// A static field is a global named after its class; any other takes the next slot of the object.
void Classes::declare_fields(uint32_t p_class, VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		if (!declarator.symbol) {
			continue;
		}
		Kind kind = l_.written_kind(declarator.type, declarator.offset, Kind::F64);
		const uint32_t holds = kind == Kind::Obj ? l_.declared_class(declarator.type) : NONE;
		// An untyped field starts undefined; one declared `Object` starts null.
		const bool starts_null = l_.starts_null_type(kind, l_.compilation_.type_of(declarator.type));
		if (p_decl->modifiers.bits & Modifiers::Static) {
			l_.globals_[declarator.symbol] = (uint32_t)l_.module_.globals.size();
			l_.module_.globals.push_back({ l_.module_.classes[p_class].name + "." + std::string(declarator.name), kind, holds, starts_null });
		} else {
			l_.fields_[declarator.symbol] = (uint32_t)l_.module_.classes[p_class].fields.size();
			l_.module_.classes[p_class].fields.push_back({ std::string(declarator.name), kind, holds, starts_null });
			Field &field = l_.module_.classes[p_class].fields.back();
			field.is_public = (p_decl->modifiers.bits & Modifiers::Public) != 0;
			field.is_protected = (p_decl->modifiers.bits & Modifiers::Protected) != 0;
			field.constant = p_decl->is_const;
			const Type *type = declarator.type ? l_.compilation_.type_of(declarator.type) : nullptr;
			if (is_engine(kind) && type && type->symbol) {
				godot_name(type->symbol->name, std::string_view(), &field.engine_type);
			}
			field.exported = declares_export(p_decl);
			if (field.exported) {
				field.first = first_value(declarator.initializer, kind, type, &Lowerer::literal_value, &Lowerer::unescape);
			}
			if (declares_signal(p_decl)) {
				l_.module_.classes[p_class].signals.push_back(std::string(declarator.name));
			}
		}
	}
}

// A method that overrides one of Godot's is what the engine calls under Godot's name.
void Classes::record_godot_virtual(uint32_t p_class, const Symbol *p_symbol, FunctionDecl *p_function, uint32_t p_made) {
	for (Symbol *at = p_symbol->klass->base; at && at->klass; at = at->klass->base) {
		auto inherited = at->klass->members.find(p_function->name);
		if (inherited == at->klass->members.end() || !Lowerer::is_godot(inherited->second)) {
			continue; // Not here, or a script class's override of it: Godot's declaration is further up.
		}
		std::string godot;
		if (godot_name(at->name, p_function->name, &godot)) {
			bool replaced = false;
			for (auto &entry : l_.module_.classes[p_class].virtuals) {
				if (entry.first == godot) {
					entry.second = p_made;
					replaced = true;
				}
			}
			if (!replaced) {
				l_.module_.classes[p_class].virtuals.push_back({ godot, p_made });
			}
		}
		return;
	}
}

// A method's place in the class's table. An override takes the slot of what it overrides.
void Classes::give_method_slot(uint32_t p_class, FunctionDecl *p_function, uint32_t p_made) {
	std::unordered_map<std::string, uint32_t> &names = l_.slot_names_[p_class];
	std::string key = member_key(p_function);
	auto slot = names.find(key);
	if (slot != names.end() && (p_function->modifiers.bits & Modifiers::Override)) {
		l_.module_.classes[p_class].methods[slot->second] = p_made;
		l_.slots_[p_function] = slot->second;
		return;
	}
	uint32_t added = (uint32_t)l_.module_.classes[p_class].methods.size();
	l_.module_.classes[p_class].methods.push_back(p_made);
	// A private method is not seen by subclasses, so its name is not recorded for them.
	if (!(p_function->modifiers.bits & Modifiers::Private)) {
		names[key] = added;
	}
	if (p_function->modifiers.bits & Modifiers::Public) {
		l_.module_.classes[p_class].named_methods.push_back({ key, added });
	}
	l_.slots_[p_function] = added;
}

// One function of a class: its constructor, a static function, or a method.
void Classes::declare_method(uint32_t p_class, const Symbol *p_symbol, FunctionDecl *p_function) {
	if (!p_function->symbol) {
		return;
	}
	if (p_function->accessor != FunctionDecl::None) {
		Members(l_).declare_accessor(p_class, p_function);
		return;
	}
	const std::string name = l_.module_.classes[p_class].name;
	if (p_symbol->klass->constructor == p_function->symbol) {
		uint32_t made = add_function(p_class, name, p_function->name_offset, false);
		l_.module_.classes[p_class].constructor = made;
		l_.functions_[p_function] = made;
		return;
	}
	bool is_static = (p_function->modifiers.bits & Modifiers::Static) != 0;
	uint32_t made = add_function(p_class, name + "/" + std::string(p_function->name), p_function->name_offset, is_static);
	l_.functions_[p_function] = made;
	l_.module_.functions[made].is_public = (p_function->modifiers.bits & Modifiers::Public) != 0;
	if (p_function->name == "toString" && !is_static) {
		const Type *result = p_function->return_type ? l_.compilation_.type_of(p_function->return_type) : nullptr;
		if (p_function->parameters.count == 0 && result && result->kind == Type::Instance && result->symbol == l_.string_) {
			l_.module_.classes[p_class].to_string = made;
		} else {
			l_.refuse(p_function->name_offset, "a toString() that takes arguments or does not return a String");
		}
	}
	if (is_static) {
		return;
	}
	if (p_function->modifiers.bits & Modifiers::Override) {
		record_godot_virtual(p_class, p_symbol, p_function, made);
	}
	give_method_slot(p_class, p_function, made);
}

void Classes::declare_class(Unit &p_unit, ClassDecl *p_decl) {
	Symbol *symbol = p_decl->symbol;
	if (!symbol || !symbol->klass) {
		return;
	}
	l_.unit_ = &p_unit;
	Class klass = inherited_class(p_decl);
	uint32_t index = l_.classes_[symbol];
	if (klass.base != NONE) {
		l_.slot_names_[index] = l_.slot_names_[klass.base];
	}
	l_.module_.classes[index] = std::move(klass);
	for (Node *member : p_decl->members) {
		if (member->kind == NodeKind::VarDecl) {
			declare_fields(index, static_cast<VarDecl *>(member));
		} else if (member->kind == NodeKind::FunctionDecl) {
			declare_method(index, symbol, static_cast<FunctionDecl *>(member));
		}
	}
	if (l_.module_.classes[index].constructor == NONE) {
		// No constructor is written: one that only sets the fields is made.
		l_.module_.classes[index].constructor = add_function(index, l_.module_.classes[index].name, p_decl->name_offset, false);
	}
	Members(l_).implement(index, symbol);
}

} // namespace ir
} // namespace kas3
