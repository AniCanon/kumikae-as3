#include "lowerer.h"

namespace kas3 {
namespace ir {

void Lowerer::begin(uint32_t p_function, Unit &p_unit, Symbol *p_class) {
	function_index_ = p_function;
	unit_ = &p_unit;
	class_ = p_class;
	constructor_ = false;
	result_type_ = nullptr;
	locals_.clear();
	loops_.clear();
	received_env_ = false;
	chain_.clear();
	own_env_reg_ = NONE;
	own_layout_ = EnvLayout();
}

void Lowerer::parameters(FunctionDecl *p_function) {
	if (!fn().is_static) {
		fn().registers.push_back(Kind::Obj);
		fn().locals.push_back({ "this", 0 });
	}
	if (p_function) {
		for (Parameter &parameter : p_function->parameters) {
			if (parameter.is_rest) {
				fn().rest = true;
				vector_class(compilation_.instance_type(array_)); // The Array it is made as.
			}
			Kind kind = parameter.is_rest ? Kind::Obj : written_kind(parameter.type, parameter.offset, Kind::F64);
			uint32_t local = reg(kind);
			if (kind == Kind::Obj) {
				// The class it holds, which a call through a function value or from Godot checks.
				fn().parameter_classes.resize(local + 1, NONE);
				fn().parameter_classes[local] = declared_class(parameter.type);
			}
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
	fn().required = fn().rest ? fn().parameters - 1 : fn().parameters;
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
		emit(Op::Return, p_position, fn().result == Kind::Any ? empty_untyped(result_type_, p_position) : default_value(fn().result, p_position));
	}
}

void Lowerer::lower_function(Unit &p_unit, Symbol *p_class, FunctionDecl *p_decl) {
	auto found = functions_.find(p_decl);
	if (found == functions_.end()) {
		return;
	}
	begin(found->second, p_unit, p_class);
	// No return type written: AS3 takes it as `*`, and falling off the end gives undefined.
	result_type_ = p_decl->return_type ? compilation_.type_of(p_decl->return_type) : compilation_.any_type();
	parameters(p_decl);
	fn().result = p_decl->return_type ? written_kind(p_decl->return_type, p_decl->return_type->offset, Kind::Void) : Kind::Any;
	if (p_decl->body) {
		declare_locals(p_decl->body);
		make_environment(p_decl);
		statement(p_decl->body);
	}
	end(p_decl->end ? p_decl->end - 1 : p_decl->offset);
	lower_pending_functions();
}

bool Lowerer::calls_super(Block *p_body) {
	bool found = false;
	walk(p_body, [&](Node *p_node) {
		found = found || (p_node->kind == NodeKind::Call && static_cast<Call *>(p_node)->callee->kind == NodeKind::SuperExpr);
	});
	return found;
}

// A signal the script declares, as its field holds it: Signal(the Godot object, its name).
uint32_t Lowerer::own_signal(std::string_view p_name, uint32_t p_position) {
	uint32_t type_name = 0;
	if (!signal_ || !engine_name(signal_, std::string_view(), &type_name)) {
		refuse(p_position, "a signal without Godot's API");
		return default_value(Kind::Val, p_position);
	}
	const uint32_t owner = reg(Kind::Ref);
	emit(Op::OwnerOf, p_position, owner, self(p_position, true));
	const uint32_t name = reg(Kind::Str);
	emit(Op::ConstStr, p_position, name, string(std::string(p_name)));
	const uint32_t out = reg(Kind::Val);
	emit(Op::HostNew, p_position, out, type_name, arguments({ owner, name }));
	return out;
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
		make_environment(decl);
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
			if (field != fields_.end() && declares_signal(static_cast<VarDecl *>(member))) {
				emit(Op::SetField, declarator.offset, 0, field->second, own_signal(declarator.name, declarator.offset));
				continue;
			}
			// Those with a written value too: a method that the values of the fields above call may read it.
			if (field != fields_.end() && module_.classes[index].fields[field->second].kind == Kind::Val) {
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
				emit(Op::SetField, declarator.offset, 0, field->second, typed(declarator.initializer, compilation_.type_of(declarator.type), kind));
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
	lower_pending_functions();
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
			// With a written value too: one worked out above it may read it.
			if (global != globals_.end() && module_.globals[global->second].kind == Kind::Val) {
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
						typed(declarator.initializer, compilation_.type_of(declarator.type), module_.globals[global->second].kind));
			}
		}
	}
	emit(Op::ReturnVoid, p_class->name_offset);
}

void Lowerer::lower_package_variables(Unit &p_unit, VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		auto global = globals_.find(declarator.symbol);
		if (global != globals_.end() && module_.globals[global->second].kind == Kind::Val) {
			// One of Godot's values with parts starts at its zero, before any package variable's value is
			// worked out: one of them may read it.
			Function zeroing;
			zeroing.name = std::string(declarator.name) + "$zero";
			zeroing.file = file(&p_unit);
			zeroing.position = declarator.offset;
			zeroing.is_static = true;
			module_.functions.push_back(std::move(zeroing));
			const uint32_t index = (uint32_t)module_.functions.size() - 1;
			module_.initializers.insert(module_.initializers.begin() + static_cast<std::ptrdiff_t>(zeroes_), index);
			zeroes_++;
			begin(index, p_unit, nullptr);
			const uint32_t zero = zero_value(declarator.type, declarator.offset);
			if (zero != NONE) {
				emit(Op::SetGlobal, declarator.offset, global->second, zero);
			}
			emit(Op::ReturnVoid, declarator.offset);
		}
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
				typed(declarator.initializer, compilation_.type_of(declarator.type), module_.globals[global->second].kind));
		emit(Op::ReturnVoid, declarator.offset);
	}
}

} // namespace ir
} // namespace kas3
