#include "lowerer.h"

namespace kas3 {
namespace ir {

void Lowerer::reserve_class(ClassDecl *p_decl) {
	Symbol *symbol = p_decl->symbol;
	if (!symbol || !symbol->klass || classes_.count(symbol)) {
		return;
	}
	Class klass;
	klass.name = class_name(symbol);
	classes_[symbol] = (uint32_t)module_.classes.size();
	module_.classes.push_back(std::move(klass));
}

// What a class starts with: a script's hold on its Godot object, or what its base class has.
Class Lowerer::inherited_class(ClassDecl *p_decl) {
	const Symbol *symbol = p_decl->symbol;
	Class klass;
	klass.name = class_name(symbol);
	Symbol *base = symbol->klass->base;
	std::string godot_base;
	if (base && is_godot(base) && is_godot_object(base) && godot_name(base->name, std::string_view(), &godot_base)) {
		// A script: it is attached to an object of that Godot class, kept in its first field.
		klass.native_base = godot_base;
		klass.fields.push_back({ "$owner", Kind::Ref });
	} else if (base && base != object_) {
		if (!class_index(base, &klass.base)) {
			refuse(p_decl->extends ? p_decl->extends->offset : p_decl->name_offset, "extending '" + std::string(base->name) + "'");
		} else {
			const Class &from = module_.classes[klass.base];
			klass.fields = from.fields;
			klass.methods = from.methods;
			klass.to_string = from.to_string;
			klass.native_base = from.native_base;
			klass.virtuals = from.virtuals;
		}
	}
	return klass;
}

uint32_t Lowerer::add_function(uint32_t p_class, const std::string &p_name, uint32_t p_position, bool p_static) {
	Function function;
	function.name = p_name;
	function.file = file(unit_);
	function.position = p_position;
	function.owner = p_class;
	function.is_static = p_static;
	module_.functions.push_back(std::move(function));
	return (uint32_t)module_.functions.size() - 1;
}

// A static field is a global named after its class; any other takes the next slot of the object.
void Lowerer::declare_fields(uint32_t p_class, VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		if (!declarator.symbol) {
			continue;
		}
		Kind kind = written_kind(declarator.type, declarator.offset, Kind::F64);
		if (p_decl->modifiers.bits & Modifiers::Static) {
			globals_[declarator.symbol] = (uint32_t)module_.globals.size();
			module_.globals.push_back({ module_.classes[p_class].name + "." + std::string(declarator.name), kind });
		} else {
			fields_[declarator.symbol] = (uint32_t)module_.classes[p_class].fields.size();
			module_.classes[p_class].fields.push_back({ std::string(declarator.name), kind });
		}
	}
}

// A method that overrides one of Godot's is what the engine calls under Godot's name.
void Lowerer::record_godot_virtual(uint32_t p_class, const Symbol *p_symbol, FunctionDecl *p_function, uint32_t p_made) {
	for (Symbol *at = p_symbol->klass->base; at && at->klass; at = at->klass->base) {
		auto inherited = at->klass->members.find(p_function->name);
		if (inherited == at->klass->members.end() || !is_godot(inherited->second)) {
			continue; // Not here, or a script class's override of it: Godot's declaration is further up.
		}
		std::string godot;
		if (godot_name(at->name, p_function->name, &godot)) {
			bool replaced = false;
			for (auto &entry : module_.classes[p_class].virtuals) {
				if (entry.first == godot) {
					entry.second = p_made;
					replaced = true;
				}
			}
			if (!replaced) {
				module_.classes[p_class].virtuals.push_back({ godot, p_made });
			}
		}
		return;
	}
}

// A method's place in the class's table. An override takes the slot of what it overrides.
void Lowerer::give_method_slot(uint32_t p_class, FunctionDecl *p_function, uint32_t p_made) {
	std::unordered_map<std::string, uint32_t> &names = slot_names_[p_class];
	std::string key(p_function->name);
	auto slot = names.find(key);
	if (slot != names.end() && (p_function->modifiers.bits & Modifiers::Override)) {
		module_.classes[p_class].methods[slot->second] = p_made;
		slots_[p_function->symbol] = slot->second;
		return;
	}
	uint32_t added = (uint32_t)module_.classes[p_class].methods.size();
	module_.classes[p_class].methods.push_back(p_made);
	// A private method is not seen by subclasses, so its name is not recorded for them.
	if (!(p_function->modifiers.bits & Modifiers::Private)) {
		names[key] = added;
	}
	slots_[p_function->symbol] = added;
}

// One function of a class: its constructor, a static function, or a method.
void Lowerer::declare_method(uint32_t p_class, const Symbol *p_symbol, FunctionDecl *p_function) {
	if (!p_function->symbol) {
		return;
	}
	if (p_function->accessor != FunctionDecl::None) {
		refuse(p_function->name_offset, "a getter or setter");
		return;
	}
	const std::string name = module_.classes[p_class].name;
	if (p_symbol->klass->constructor == p_function->symbol) {
		uint32_t made = add_function(p_class, name, p_function->name_offset, false);
		module_.classes[p_class].constructor = made;
		functions_[p_function->symbol] = made;
		return;
	}
	bool is_static = (p_function->modifiers.bits & Modifiers::Static) != 0;
	uint32_t made = add_function(p_class, name + "/" + std::string(p_function->name), p_function->name_offset, is_static);
	functions_[p_function->symbol] = made;
	if (p_function->name == "toString" && !is_static) {
		const Type *result = p_function->return_type ? compilation_.type_of(p_function->return_type) : nullptr;
		if (p_function->parameters.count == 0 && result && result->kind == Type::Instance && result->symbol == string_) {
			module_.classes[p_class].to_string = made;
		} else {
			refuse(p_function->name_offset, "a toString() that takes arguments or does not return a String");
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

void Lowerer::declare_class(Unit &p_unit, ClassDecl *p_decl) {
	Symbol *symbol = p_decl->symbol;
	if (!symbol || !symbol->klass) {
		return;
	}
	unit_ = &p_unit;
	if (p_decl->implements.count > 0) {
		refuse(p_decl->implements[0]->offset, "'implements'");
	}
	Class klass = inherited_class(p_decl);
	uint32_t index = classes_[symbol];
	if (klass.base != NONE) {
		slot_names_[index] = slot_names_[klass.base];
	}
	module_.classes[index] = std::move(klass);
	for (Node *member : p_decl->members) {
		if (member->kind == NodeKind::VarDecl) {
			declare_fields(index, static_cast<VarDecl *>(member));
		} else if (member->kind == NodeKind::FunctionDecl) {
			declare_method(index, symbol, static_cast<FunctionDecl *>(member));
		}
	}
	if (module_.classes[index].constructor == NONE) {
		// No constructor is written: one that only sets the fields is made.
		module_.classes[index].constructor = add_function(index, module_.classes[index].name, p_decl->name_offset, false);
	}
}

void Lowerer::begin(uint32_t p_function, Unit &p_unit, Symbol *p_class) {
	function_index_ = p_function;
	unit_ = &p_unit;
	class_ = p_class;
	constructor_ = false;
	locals_.clear();
	loops_.clear();
}

void Lowerer::parameters(FunctionDecl *p_function) {
	if (!fn().is_static) {
		fn().registers.push_back(Kind::Obj);
		fn().locals.push_back({ "this", 0 });
	}
	if (p_function) {
		for (Parameter &parameter : p_function->parameters) {
			if (parameter.is_rest) {
				refuse(parameter.offset, "a rest parameter");
			}
			Kind kind = written_kind(parameter.type, parameter.offset, Kind::F64);
			uint32_t local = reg(kind);
			if (is_engine(kind) && parameter.type) {
				// What the engine must be handing over for this parameter.
				const Type *type = compilation_.type_of(parameter.type);
				std::string godot;
				if (type && type->symbol && godot_name(type->symbol->name, std::string_view(), &godot)) {
					fn().engine_types.resize(local + 1);
					fn().engine_types[local] = godot;
				}
			}
			if (parameter.symbol) {
				locals_[parameter.symbol] = local;
			}
			fn().locals.push_back({ std::string(parameter.name), local });
		}
	}
	fn().parameters = (uint32_t)fn().registers.size();
	fn().required = fn().parameters;
	for (uint32_t i = 0; p_function && i < p_function->parameters.count; i++) {
		if (p_function->parameters[i].default_value) {
			fn().required = fn().parameters - (p_function->parameters.count - i);
			break;
		}
	}
	// A parameter the caller left out takes its default here, in the function that runs.
	for (uint32_t i = 0; p_function && i < p_function->parameters.count; i++) {
		Parameter &parameter = p_function->parameters[i];
		if (!parameter.default_value || !parameter.symbol) {
			continue;
		}
		uint32_t local = locals_[parameter.symbol];
		uint32_t missing = reg(Kind::Bool);
		emit(Op::ArgMissing, parameter.offset, missing, local);
		uint32_t skip = emit(Op::JumpIfNot, parameter.offset, missing, 0);
		uint32_t value = expression(parameter.default_value, kind_at(local));
		if (value != local) {
			emit(Op::Move, parameter.offset, local, value);
		}
		patch(skip, here());
	}
}

void Lowerer::end(uint32_t p_position) {
	if (fn().result == Kind::Void) {
		emit(Op::ReturnVoid, p_position);
	} else {
		// Falling off the end gives the type's default value.
		emit(Op::Return, p_position, default_value(fn().result, p_position));
	}
}

void Lowerer::lower_function(Unit &p_unit, Symbol *p_class, FunctionDecl *p_decl) {
	auto found = functions_.find(p_decl->symbol);
	if (found == functions_.end()) {
		return;
	}
	begin(found->second, p_unit, p_class);
	parameters(p_decl);
	if (p_decl->return_type) {
		fn().result = written_kind(p_decl->return_type, p_decl->return_type->offset, Kind::Void);
	} else {
		unit_ = &p_unit;
		refuse(p_decl->name_offset, "a function without a return type");
	}
	if (p_decl->body) {
		declare_locals(p_decl->body);
		statement(p_decl->body);
	}
	end(p_decl->end ? p_decl->end - 1 : p_decl->offset);
}

bool Lowerer::calls_super(Block *p_body) {
	bool found = false;
	walk(p_body, [&](Node *p_node) {
		found = found || (p_node->kind == NodeKind::Call && static_cast<Call *>(p_node)->callee->kind == NodeKind::SuperExpr);
	});
	return found;
}

void Lowerer::lower_constructor(Unit &p_unit, ClassDecl *p_class) {
	Symbol *symbol = p_class->symbol;
	uint32_t index = classes_[symbol];
	Symbol *constructor = symbol->klass->constructor;
	FunctionDecl *decl = constructor && constructor->node && constructor->node->kind == NodeKind::FunctionDecl
			? static_cast<FunctionDecl *>(constructor->node)
			: nullptr;
	begin(module_.classes[index].constructor, p_unit, symbol);
	constructor_ = true;
	parameters(decl);
	if (decl && decl->body) {
		declare_locals(decl->body);
	}
	// The class's own fields get their values first, before any constructor body runs. One of Godot's
	// values with parts starts at its zero, and does so before any written value is worked out, since
	// that may read it.
	for (Node *member : p_class->members) {
		if (member->kind != NodeKind::VarDecl || (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			continue;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
			auto field = fields_.find(declarator.symbol);
			if (field != fields_.end() && !declarator.initializer && module_.classes[index].fields[field->second].kind == Kind::Val) {
				uint32_t zero = zero_value(declarator.type, declarator.offset);
				if (zero != NONE) {
					emit(Op::SetField, declarator.offset, 0, field->second, zero);
				}
			}
		}
	}
	for (Node *member : p_class->members) {
		if (member->kind != NodeKind::VarDecl || (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			continue;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
			auto field = fields_.find(declarator.symbol);
			if (field != fields_.end() && declarator.initializer) {
				Kind kind = module_.classes[index].fields[field->second].kind;
				emit(Op::SetField, declarator.offset, 0, field->second, expression(declarator.initializer, kind));
			}
		}
	}
	// Then the base class's constructor, unless the body calls it itself.
	Symbol *base = symbol->klass->base;
	uint32_t base_index = 0;
	if (base && base != object_ && class_index(base, &base_index) && !(decl && decl->body && calls_super(decl->body))) {
		std::vector<uint32_t> list = { 0 };
		List<Expr *> none;
		constructor_arguments(base, none, p_class->name_offset, list);
		emit(Op::Call, p_class->name_offset, NONE, module_.classes[base_index].constructor, arguments(std::move(list)));
	}
	if (decl && decl->body) {
		statement(decl->body);
	}
	emit(Op::ReturnVoid, decl && decl->end ? decl->end - 1 : p_class->name_offset);
}

void Lowerer::lower_statics(Unit &p_unit, ClassDecl *p_class) {
	bool any = false;
	for (Node *member : p_class->members) {
		if (member->kind == NodeKind::VarDecl && (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
				auto global = globals_.find(declarator.symbol);
				any = any || (global != globals_.end() && (declarator.initializer || module_.globals[global->second].kind == Kind::Val));
			}
		}
	}
	if (!any) {
		return;
	}
	Function function;
	function.name = module_.classes[classes_[p_class->symbol]].name + "$static";
	function.file = file(&p_unit);
	function.position = p_class->name_offset;
	function.owner = classes_[p_class->symbol];
	function.is_static = true;
	module_.functions.push_back(std::move(function));
	uint32_t index = (uint32_t)module_.functions.size() - 1;
	module_.classes[classes_[p_class->symbol]].static_init = index; // Run on the class's first use.
	begin(index, p_unit, p_class->symbol);
	for (Node *member : p_class->members) {
		if (member->kind != NodeKind::VarDecl || !(static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			continue;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
			auto global = globals_.find(declarator.symbol);
			if (global != globals_.end() && !declarator.initializer && module_.globals[global->second].kind == Kind::Val) {
				uint32_t zero = zero_value(declarator.type, declarator.offset);
				if (zero != NONE) {
					emit(Op::SetGlobal, declarator.offset, global->second, zero);
				}
			}
		}
	}
	for (Node *member : p_class->members) {
		if (member->kind != NodeKind::VarDecl || !(static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			continue;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
			auto global = globals_.find(declarator.symbol);
			if (global != globals_.end() && declarator.initializer) {
				emit(Op::Stmt, declarator.offset);
				emit(Op::SetGlobal, declarator.offset, global->second,
						expression(declarator.initializer, module_.globals[global->second].kind));
			}
		}
	}
	emit(Op::ReturnVoid, p_class->name_offset);
}

void Lowerer::lower_package_variables(Unit &p_unit, VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		auto global = globals_.find(declarator.symbol);
		if (global == globals_.end() || !declarator.initializer) {
			continue;
		}
		Function function;
		function.name = std::string(declarator.name) + "$init";
		function.file = file(&p_unit);
		function.position = declarator.offset;
		function.is_static = true;
		module_.functions.push_back(std::move(function));
		uint32_t index = (uint32_t)module_.functions.size() - 1;
		module_.initializers.push_back(index);
		begin(index, p_unit, nullptr);
		emit(Op::SetGlobal, declarator.offset, global->second,
				expression(declarator.initializer, module_.globals[global->second].kind));
		emit(Op::ReturnVoid, declarator.offset);
	}
}

} // namespace ir
} // namespace kas3
