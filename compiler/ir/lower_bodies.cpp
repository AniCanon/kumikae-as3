#include "lower_bodies.h"
#include "lower_calls.h"
#include "lower_engine.h"
#include "lower_functions.h"
#include "lower_statements.h"

namespace kas3 {
namespace ir {

void Bodies::begin(uint32_t p_function, Unit &p_unit, Symbol *p_class) {
	l_.function_index_ = p_function;
	l_.unit_ = &p_unit;
	l_.class_ = p_class;
	l_.constructor_ = false;
	l_.result_type_ = nullptr;
	l_.locals_.clear();
	l_.loops_.clear();
	l_.received_env_ = false;
	l_.chain_.clear();
	l_.own_env_reg_ = NONE;
	l_.own_layout_ = EnvLayout();
}

void Bodies::parameters(FunctionDecl *p_function) {
	if (!l_.fn().is_static) {
		l_.fn().registers.push_back(Kind::Obj);
		l_.fn().locals.push_back({ "this", 0 });
	}
	if (p_function) {
		for (Parameter &parameter : p_function->parameters) {
			if (parameter.is_rest) {
				l_.fn().rest = true;
				l_.vector_class(l_.compilation_.instance_type(l_.array_)); // The Array it is made as.
			}
			Kind kind = parameter.is_rest ? Kind::Obj : l_.written_kind(parameter.type, parameter.offset, Kind::F64);
			uint32_t local = l_.reg(kind);
			if (kind == Kind::Obj) {
				// The class it holds, which a call through a function value or from Godot checks.
				l_.fn().parameter_classes.resize(local + 1, NONE);
				l_.fn().parameter_classes[local] = l_.declared_class(parameter.type);
			}
			if (is_engine(kind) && parameter.type) {
				// What the engine must be handing over for this parameter.
				const Type *type = l_.compilation_.type_of(parameter.type);
				std::optional<std::string> godot;
				if (type && type->symbol && (godot = godot_name(type->symbol->name, std::string_view()))) {
					l_.fn().engine_types.resize(local + 1);
					l_.fn().engine_types[local] = *godot;
				}
			}
			if (parameter.symbol) {
				l_.locals_[parameter.symbol] = local;
			}
			l_.fn().locals.push_back({ std::string(parameter.name), local });
		}
	}
	l_.fn().parameters = (uint32_t)l_.fn().registers.size();
	l_.fn().required = l_.fn().rest ? l_.fn().parameters - 1 : l_.fn().parameters;
	for (uint32_t i = 0; p_function && i < p_function->parameters.count; i++) {
		if (p_function->parameters[i].default_value) {
			l_.fn().required = l_.fn().parameters - (p_function->parameters.count - i);
			break;
		}
	}
	// A parameter the caller left out takes its default here, in the function that runs.
	for (uint32_t i = 0; p_function && i < p_function->parameters.count; i++) {
		Parameter &parameter = p_function->parameters[i];
		if (!parameter.default_value || !parameter.symbol) {
			continue;
		}
		uint32_t local = l_.locals_[parameter.symbol];
		uint32_t missing = l_.reg(Kind::Bool);
		l_.emit(Op::ArgMissing, parameter.offset, missing, local);
		uint32_t skip = l_.emit(Op::JumpIfNot, parameter.offset, missing, 0);
		uint32_t value = l_.expression(parameter.default_value, l_.kind_at(local));
		if (value != local) {
			l_.emit(Op::Move, parameter.offset, local, value);
		}
		l_.patch(skip, l_.here());
	}
}

void Bodies::end(uint32_t p_position) {
	if (l_.fn().result == Kind::Void) {
		l_.emit(Op::ReturnVoid, p_position);
	} else {
		// Falling off the end gives the type's default value.
		l_.emit(Op::Return, p_position, l_.fn().result == Kind::Any ? l_.empty_untyped(l_.result_type_, p_position) : l_.default_value(l_.fn().result, p_position, l_.result_type_));
	}
}

void Bodies::lower_function(Unit &p_unit, Symbol *p_class, FunctionDecl *p_decl) {
	auto found = l_.functions_.find(p_decl);
	if (found == l_.functions_.end()) {
		return;
	}
	begin(found->second, p_unit, p_class);
	// No return type written: AS3 takes it as `*`, and falling off the end gives undefined.
	l_.result_type_ = p_decl->return_type ? l_.compilation_.type_of(p_decl->return_type) : l_.compilation_.any_type();
	parameters(p_decl);
	l_.fn().result = p_decl->return_type ? l_.written_kind(p_decl->return_type, p_decl->return_type->offset, Kind::Void) : Kind::Any;
	if (p_decl->body) {
		Statements(l_).declare_locals(p_decl->body);
		Functions(l_).make_environment(p_decl);
		Statements(l_).statement(p_decl->body);
	}
	end(p_decl->end ? p_decl->end - 1 : p_decl->offset);
	Functions(l_).lower_pending_functions();
}

bool Bodies::calls_super(Block *p_body) {
	bool found = false;
	walk(p_body, [&](Node *p_node) {
		found = found || (p_node->kind == NodeKind::Call && static_cast<Call *>(p_node)->callee->kind == NodeKind::SuperExpr);
	});
	return found;
}

// A signal the script declares, as its field holds it: Signal(the Godot object, its name).
uint32_t Bodies::own_signal(std::string_view p_name, uint32_t p_position) {
	uint32_t type_name = 0;
	if (!l_.signal_ || !l_.engine_name(l_.signal_, std::string_view(), &type_name)) {
		l_.refuse(p_position, "a signal without Godot's API");
		return l_.default_value(Kind::Val, p_position);
	}
	const uint32_t owner = l_.reg(Kind::Ref);
	l_.emit(Op::OwnerOf, p_position, owner, l_.self(p_position, true));
	const uint32_t name = l_.reg(Kind::Str);
	l_.emit(Op::ConstStr, p_position, name, l_.string(std::string(p_name)));
	const uint32_t out = l_.engine_reg(Kind::Val, l_.module_.engine_names[type_name].name);
	l_.emit(Op::HostNew, p_position, out, type_name, l_.arguments({ owner, name }));
	return out;
}

// The class's own fields get their values first, before any constructor body runs. One of Godot's
// values with parts starts at its zero, and does so before any written value is worked out, since
// that may read it.
void Bodies::first_values(ClassDecl *p_class, uint32_t p_index) {
	for (Node *member : p_class->members) {
		if (member->kind != NodeKind::VarDecl || (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			continue;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
			auto field = l_.fields_.find(declarator.symbol);
			if (field != l_.fields_.end() && declares_signal(static_cast<VarDecl *>(member))) {
				l_.emit(Op::SetField, declarator.offset, 0, field->second, own_signal(declarator.name, declarator.offset));
				continue;
			}
			// Those with a written value too: a method that the values of the fields above call may read it.
			if (field != l_.fields_.end() && l_.module_.classes[p_index].fields[field->second].kind == Kind::Val) {
				uint32_t zero = Engine(l_).zero_value(declarator.type, declarator.offset);
				if (zero != NONE) {
					l_.emit(Op::SetField, declarator.offset, 0, field->second, zero);
				}
			}
		}
	}
	for (Node *member : p_class->members) {
		if (member->kind != NodeKind::VarDecl || (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			continue;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
			auto field = l_.fields_.find(declarator.symbol);
			if (field != l_.fields_.end() && declarator.initializer) {
				Kind kind = l_.module_.classes[p_index].fields[field->second].kind;
				l_.emit(Op::SetField, declarator.offset, 0, field->second, l_.typed(declarator.initializer, l_.compilation_.type_of(declarator.type), kind));
			}
		}
	}
}

void Bodies::lower_constructor(Unit &p_unit, ClassDecl *p_class) {
	Symbol *symbol = p_class->symbol;
	uint32_t index = l_.classes_[symbol];
	Symbol *constructor = symbol->klass->constructor;
	FunctionDecl *decl = constructor && constructor->node && constructor->node->kind == NodeKind::FunctionDecl
			? static_cast<FunctionDecl *>(constructor->node)
			: nullptr;
	begin(l_.module_.classes[index].constructor, p_unit, symbol);
	l_.constructor_ = true;
	parameters(decl);
	if (decl && decl->body) {
		Statements(l_).declare_locals(decl->body);
		Functions(l_).make_environment(decl);
	}
	first_values(p_class, index);
	// Then the base class's constructor, unless the body calls it itself.
	Symbol *base = symbol->klass->base;
	uint32_t base_index = 0;
	if (base && base != l_.object_ && l_.class_index(base, &base_index) && !(decl && decl->body && calls_super(decl->body))) {
		std::vector<uint32_t> list = { 0 };
		List<Expr *> none;
		Calls(l_).constructor_arguments(base, none, p_class->name_offset, list);
		l_.emit(Op::Call, p_class->name_offset, NONE, l_.module_.classes[base_index].constructor, l_.arguments(std::move(list)));
	}
	if (decl && decl->body) {
		Statements(l_).statement(decl->body);
	}
	l_.emit(Op::ReturnVoid, decl && decl->end ? decl->end - 1 : p_class->name_offset);
	Functions(l_).lower_pending_functions();
}

void Bodies::lower_statics(Unit &p_unit, ClassDecl *p_class) {
	bool any = false;
	for (Node *member : p_class->members) {
		if (member->kind == NodeKind::VarDecl && (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
				auto global = l_.globals_.find(declarator.symbol);
				any = any || (global != l_.globals_.end() && (declarator.initializer || l_.module_.globals[global->second].kind == Kind::Val));
			}
		}
	}
	if (!any) {
		return;
	}
	Function function;
	function.name = l_.module_.classes[l_.classes_[p_class->symbol]].name + "$static";
	function.file = l_.file(&p_unit);
	function.position = p_class->name_offset;
	function.owner = l_.classes_[p_class->symbol];
	function.is_static = true;
	l_.module_.functions.push_back(std::move(function));
	uint32_t index = (uint32_t)l_.module_.functions.size() - 1;
	l_.module_.classes[l_.classes_[p_class->symbol]].static_init = index; // Run on the class's first use.
	begin(index, p_unit, p_class->symbol);
	for (Node *member : p_class->members) {
		if (member->kind != NodeKind::VarDecl || !(static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			continue;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
			auto global = l_.globals_.find(declarator.symbol);
			// With a written value too: one worked out above it may read it.
			if (global != l_.globals_.end() && l_.module_.globals[global->second].kind == Kind::Val) {
				uint32_t zero = Engine(l_).zero_value(declarator.type, declarator.offset);
				if (zero != NONE) {
					l_.emit(Op::SetGlobal, declarator.offset, global->second, zero);
				}
			}
		}
	}
	for (Node *member : p_class->members) {
		if (member->kind != NodeKind::VarDecl || !(static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
			continue;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
			auto global = l_.globals_.find(declarator.symbol);
			if (global != l_.globals_.end() && declarator.initializer) {
				l_.emit(Op::Stmt, declarator.offset);
				l_.emit(Op::SetGlobal, declarator.offset, global->second,
						l_.typed(declarator.initializer, l_.compilation_.type_of(declarator.type), l_.module_.globals[global->second].kind));
			}
		}
	}
	l_.emit(Op::ReturnVoid, p_class->name_offset);
}

void Bodies::lower_package_variables(Unit &p_unit, VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		auto global = l_.globals_.find(declarator.symbol);
		if (global != l_.globals_.end() && l_.module_.globals[global->second].kind == Kind::Val) {
			// One of Godot's values with parts starts at its zero, before any package variable's value is
			// worked out: one of them may read it.
			Function zeroing;
			zeroing.name = std::string(declarator.name) + "$zero";
			zeroing.file = l_.file(&p_unit);
			zeroing.position = declarator.offset;
			zeroing.is_static = true;
			l_.module_.functions.push_back(std::move(zeroing));
			const uint32_t index = (uint32_t)l_.module_.functions.size() - 1;
			l_.module_.initializers.insert(l_.module_.initializers.begin() + static_cast<std::ptrdiff_t>(l_.zeroes_), index);
			l_.zeroes_++;
			begin(index, p_unit, nullptr);
			const uint32_t zero = Engine(l_).zero_value(declarator.type, declarator.offset);
			if (zero != NONE) {
				l_.emit(Op::SetGlobal, declarator.offset, global->second, zero);
			}
			l_.emit(Op::ReturnVoid, declarator.offset);
		}
		if (global == l_.globals_.end() || !declarator.initializer) {
			continue;
		}
		Function function;
		function.name = std::string(declarator.name) + "$init";
		function.file = l_.file(&p_unit);
		function.position = declarator.offset;
		function.is_static = true;
		l_.module_.functions.push_back(std::move(function));
		uint32_t index = (uint32_t)l_.module_.functions.size() - 1;
		l_.module_.initializers.push_back(index);
		begin(index, p_unit, nullptr);
		l_.emit(Op::SetGlobal, declarator.offset, global->second,
				l_.typed(declarator.initializer, l_.compilation_.type_of(declarator.type), l_.module_.globals[global->second].kind));
		l_.emit(Op::ReturnVoid, declarator.offset);
	}
}

} // namespace ir
} // namespace kas3
