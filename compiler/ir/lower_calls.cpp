#include "lowerer.h"

namespace kas3 {
namespace ir {

void Lowerer::call_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list) {
	uint32_t count = p_function ? p_function->parameters.count : 0;
	if (p_arguments.count > count) {
		refuse(p_position, "a call with extra arguments");
	}
	for (uint32_t i = 0; i < count; i++) {
		Parameter &parameter = p_function->parameters[i];
		if (parameter.is_rest) {
			refuse(p_position, "a call to a function with a rest parameter");
			break;
		}
		Kind kind = Kind::F64;
		const Type *type = compilation_.type_of(parameter.type);
		if (!parameter.type || !kind_of(type, &kind)) {
			refuse(p_position, "a call to a function with a parameter of type '" + compilation_.type_name(type) + "'");
			break;
		}
		if (i >= p_arguments.count) {
			break; // Left out: the function that is called supplies its own default.
		}
		Expr *later = nullptr;
		for (uint32_t next = i + 1; next < p_arguments.count && !later; next++) {
			later = writes_locals(p_arguments[next]) ? p_arguments[next] : nullptr;
		}
		r_list.push_back(settled(expression(p_arguments[i], kind), later, p_position));
	}
}

Expr *Lowerer::writer(const List<Expr *> &p_expressions) {
	for (Expr *expression : p_expressions) {
		if (writes_locals(expression)) {
			return expression;
		}
	}
	return nullptr;
}

uint32_t Lowerer::call_result(Symbol *p_function, uint32_t p_position, Kind *r_kind) {
	Kind kind = Kind::Void;
	if (p_function->type && !kind_of(compilation_.type_of(p_function->type), &kind)) {
		refuse(p_position, "a call to a function that returns '" + compilation_.type_name(compilation_.type_of(p_function->type)) + "'");
	}
	*r_kind = kind;
	return kind == Kind::Void ? NONE : reg(kind);
}

uint32_t Lowerer::call(Call *p_call, Kind p_want) {
	Expr *callee = p_call->callee;
	uint32_t position = p_call->offset;
	// super(...): the base class's constructor.
	if (callee->kind == NodeKind::SuperExpr) {
		Symbol *base = class_ && class_->klass ? class_->klass->base : nullptr;
		uint32_t base_index = 0;
		if (base && base != object_ && class_index(base, &base_index)) {
			std::vector<uint32_t> list = { 0 };
			constructor_arguments(base, p_call->arguments, position, list);
			emit(Op::Call, position, NONE, module_.classes[base_index].constructor, arguments(std::move(list)));
		}
		return 0; // No value: the checker lets nothing use it.
	}
	Symbol *symbol = callee_symbol(callee);
	if (!symbol && callee->kind == NodeKind::Member && signal_ && signal_->klass) {
		// `timer[name].add(f)`: the checker can't know what `timer[name]` is. A signal's method written
		// after it says a signal is meant; the engine checks that it is one when the line runs.
		Member *member = static_cast<Member *>(callee);
		auto verb = signal_->klass->members.find(member->name);
		if (verb != signal_->klass->members.end() && verb->second->kind == SymbolKind::Method) {
			uint32_t object = 0, name = 0;
			uint32_t found = signal_by_name(member->object, writer(p_call->arguments), &object, &name);
			if (found != NONE) {
				return engine_call(p_call, verb->second, found, [&]() { emit(Op::HostMember, member->object->offset, found, object, name); });
			}
		}
	}
	if (!symbol) {
		refuse(callee->offset, "calling a value");
		return default_value(p_want, position);
	}
	// A conversion written as a call: int(x), String(x).
	Kind conversion = Kind::Void;
	if (symbol->is_type()) {
		if (kind_of_class(symbol, &conversion) && p_call->arguments.count == 1) {
			Kind given = Kind::Void;
			bool known = kind_of(p_call->arguments[0]->type, &given);
			// To or from an Int64, a conversion that is written out is AS3's own: see convert().
			uint32_t value = known && (given == Kind::I64 || conversion == Kind::I64) && !is_null(p_call->arguments[0])
					? convert(wide_operand(p_call->arguments[0], conversion), conversion, position, true)
					: expression(p_call->arguments[0], conversion);
			if (conversion == Kind::Str) {
				// The conversion written out always gives a string: a missing one becomes "null".
				uint32_t text = reg(Kind::Str);
				emit(Op::StrToStr, position, text, value);
				return text;
			}
			return value;
		}
		refuse(callee->offset, "a cast to '" + std::string(symbol->name) + "'");
		return default_value(p_want, position);
	}
	if (symbol->unit && symbol->unit->is_builtin && symbol->kind == SymbolKind::Function && symbol->name == "trace") {
		std::vector<uint32_t> list;
		for (uint32_t i = 0; i < p_call->arguments.count; i++) {
			Expr *later = nullptr;
			for (uint32_t next = i + 1; next < p_call->arguments.count && !later; next++) {
				later = writes_locals(p_call->arguments[next]) ? p_call->arguments[next] : nullptr;
			}
			uint32_t value = expression(p_call->arguments[i], Kind::Str);
			// A null string traces as "null".
			uint32_t text = reg(Kind::Str);
			emit(Op::StrToStr, p_call->arguments[i]->offset, text, value);
			list.push_back(settled(text, later, position));
		}
		emit(Op::Trace, position, arguments(std::move(list)));
		return 0;
	}
	if (symbol->name == "toString" && p_call->arguments.count == 0 && callee->kind == NodeKind::Member &&
			static_cast<Member *>(callee)->object->kind != NodeKind::SuperExpr && is_engine(natural(static_cast<Member *>(callee)->object))) {
		// AS3's own toString() on one of Godot's values or objects: Godot's text for it.
		uint32_t value = expression(static_cast<Member *>(callee)->object, Kind::Void);
		emit(Op::NullCheck, position, value);
		uint32_t out = reg(Kind::Str);
		emit(Op::HostToStr, position, out, value);
		return out;
	}
	if (is_godot(symbol) && symbol->kind == SymbolKind::Method && symbol->is_static()) {
		// `Input.isActionPressed(...)`: a method of the engine's one Input. Any other is called on no object.
		return is_singleton(symbol->owner) ? engine_call(p_call, symbol, singleton(symbol->owner, position)) : static_call(p_call, symbol);
	}
	if (is_godot(symbol) && symbol->kind == SymbolKind::Method && !symbol->is_static()) {
		bool through_super = callee->kind == NodeKind::Member && static_cast<Member *>(callee)->object->kind == NodeKind::SuperExpr;
		if (through_super && !(symbol->modifiers & Modifiers::Final)) {
			// Godot's own version of a virtual does nothing. Asking the engine for it would come straight
			// back to the script. The arguments are still evaluated.
			for (Expr *argument : p_call->arguments) {
				expression(argument, Kind::Void);
			}
			Kind kind = Kind::Void;
			if (symbol->type) {
				kind_of(compilation_.type_of(symbol->type), &kind);
			}
			return kind == Kind::Void ? 0 : default_value(kind, position);
		}
		return engine_call(p_call, symbol);
	}
	auto function = functions_.find(symbol);
	if (function == functions_.end() || !symbol->node || symbol->node->kind != NodeKind::FunctionDecl) {
		refuse(callee->offset, "calling '" + std::string(symbol->name) + "'");
		return default_value(p_want, position);
	}
	FunctionDecl *decl = static_cast<FunctionDecl *>(symbol->node);
	std::vector<uint32_t> list;
	bool direct = true;
	bool on_another = false; // Called on something other than `this`: it may be missing.
	if (symbol->kind == SymbolKind::Method && !symbol->is_static()) {
		direct = (symbol->modifiers & Modifiers::Private) != 0;
		if (callee->kind == NodeKind::Member) {
			Expr *object = static_cast<Member *>(callee)->object;
			on_another = object->kind != NodeKind::ThisExpr && object->kind != NodeKind::SuperExpr;
			direct = direct || object->kind == NodeKind::SuperExpr;
			list.push_back(settled(receiver(object), writer(p_call->arguments), position));
		} else {
			if (fn().is_static) {
				refuse(callee->offset, "this call");
			}
			list.push_back(0);
		}
	}
	call_arguments(decl, p_call->arguments, position, list);
	if (on_another) {
		emit(Op::NullCheck, position, list[0]); // After the arguments.
	}
	if (symbol->is_static() && symbol->owner) {
		touch(symbol->owner, position);
	}
	Kind kind = Kind::Void;
	uint32_t out = call_result(symbol, position, &kind);
	if (direct) {
		emit(Op::Call, position, out, function->second, arguments(std::move(list)));
	} else {
		emit(Op::CallVirtual, position, out, slots_[symbol], arguments(std::move(list)));
	}
	return out == NONE ? 0 : out;
}

uint32_t Lowerer::bound_method(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	if (is_null(p_expression)) {
		return default_value(Kind::Val, position);
	}
	Symbol *symbol = callee_symbol(p_expression);
	auto function = functions_.find(symbol);
	if (!symbol || symbol->kind != SymbolKind::Method || symbol->is_static() || function == functions_.end()) {
		refuse(position, "a function used as a value, other than a method of a script object");
		return default_value(Kind::Val, position);
	}
	uint32_t object = 0;
	bool on_this = true;
	if (p_expression->kind == NodeKind::Member) {
		Expr *written = static_cast<Member *>(p_expression)->object;
		if (written->kind == NodeKind::SuperExpr) {
			refuse(position, "a method of 'super' used as a value");
			return default_value(Kind::Val, position);
		}
		if (written->kind != NodeKind::ThisExpr) {
			on_this = false;
			object = receiver(written);
			emit(Op::NullCheck, position, object);
		}
	}
	if (on_this && fn().is_static) {
		refuse(position, "this name");
		return default_value(Kind::Val, position);
	}
	bool exact = (symbol->modifiers & Modifiers::Private) != 0;
	uint32_t out = reg(Kind::Val);
	emit(Op::HostBind, position, out, object, exact ? function->second : (slots_[symbol] | ir::BIND_SLOT));
	return out;
}

uint32_t Lowerer::signal_by_name(Expr *p_expression, Expr *p_later, uint32_t *r_object, uint32_t *r_name) {
	if (p_expression->kind != NodeKind::Index) {
		return NONE;
	}
	Index *index = static_cast<Index *>(p_expression);
	Kind object = Kind::Void;
	if (!kind_of(index->object->type, &object) || !is_string_type(index->index->type)) {
		return NONE;
	}
	// A Godot object, or a script object, which stands for the Godot object it is attached to.
	uint32_t klass = 0;
	const Type *type = index->object->type;
	bool script = object == Kind::Obj && type && type->kind == Type::Instance && class_index(type->symbol, &klass) &&
			!module_.classes[klass].native_base.empty();
	if (object != Kind::Ref && !script) {
		return NONE;
	}
	// `this` goes as itself: the name may be one of the script's own fields that holds a signal.
	// Another script object goes as the Godot object behind it: its fields are its own business.
	bool of_value = false;
	uint32_t object_reg = script && index->object->kind == NodeKind::ThisExpr && !fn().is_static
			? 0u
			: engine_receiver(index->object, p_expression->offset, &of_value);
	*r_object = settled(object_reg, index->index, p_expression->offset);
	*r_object = settled(*r_object, p_later, p_expression->offset);
	*r_name = settled(expression(index->index, Kind::Str), p_later, p_expression->offset);
	uint32_t out = reg(Kind::Val);
	if (fn().engine_types.size() <= out) {
		fn().engine_types.resize(out + 1);
	}
	fn().engine_types[out] = "Signal";
	return out;
}

void Lowerer::engine_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list) {
	uint32_t count = p_function ? p_function->parameters.count : 0;
	// A default that AS3 can't write is declared `= null`, and Godot fills in the real one when the
	// argument is left out. A `null` written last for such a parameter is the same as leaving it out.
	uint32_t written = p_arguments.count;
	while (written > 0 && written <= count && is_null(p_arguments[written - 1]) && p_function->parameters[written - 1].default_value &&
			is_null(p_function->parameters[written - 1].default_value)) {
		written--;
	}
	for (uint32_t i = 0; i < written; i++) {
		Kind kind = Kind::Void;
		if (i < count && !p_function->parameters[i].is_rest && p_function->parameters[i].type) {
			const Type *type = compilation_.type_of(p_function->parameters[i].type);
			if (type && type->kind == Type::Instance && type->symbol == function_ && function_) {
				// Godot takes a function here: a method goes bound to its object.
				Expr *later = nullptr;
				for (uint32_t next = i + 1; next < written && !later; next++) {
					later = writes_locals(p_arguments[next]) ? p_arguments[next] : nullptr;
				}
				r_list.push_back(settled(bound_method(p_arguments[i]), later, p_position));
				continue;
			}
			if (type && type->kind != Type::Any && !kind_of(type, &kind)) {
				refuse(p_arguments[i]->offset, "passing a value of type '" + compilation_.type_name(type) + "' to Godot");
				kind = Kind::Void;
			}
		}
		Expr *later = nullptr;
		for (uint32_t next = i + 1; next < p_arguments.count && !later; next++) {
			later = writes_locals(p_arguments[next]) ? p_arguments[next] : nullptr;
		}
		if (kind == Kind::Void && is_null(p_arguments[i])) {
			kind = Kind::Ref; // `null` where Godot takes anything: nothing.
		}
		uint32_t value = expression(p_arguments[i], kind);
		if (kind_at(value) == Kind::Obj) {
			refuse(p_arguments[i]->offset, "passing a script object to Godot");
		}
		r_list.push_back(settled(value, later, p_position));
	}
}

uint32_t Lowerer::engine_call(Call *p_call, Symbol *p_method, uint32_t p_receiver, const std::function<void()> &p_before) {
	uint32_t position = p_call->offset;
	uint32_t name = 0;
	if (!engine_name(p_method->owner, p_method->name, &name)) {
		refuse(p_call->callee->offset, "calling '" + std::string(p_method->name) + "'");
		return 0;
	}
	Kind kind = Kind::Void;
	if (p_method->type && !kind_of(compilation_.type_of(p_method->type), &kind)) {
		refuse(position, "a call to a Godot method that returns '" + compilation_.type_name(compilation_.type_of(p_method->type)) + "'");
	}
	bool of_value = false;
	Expr *object = p_call->callee->kind == NodeKind::Member ? static_cast<Member *>(p_call->callee)->object : nullptr;
	std::vector<uint32_t> list;
	list.push_back(settled(p_receiver != NONE ? p_receiver : engine_receiver(object, position, &of_value), writer(p_call->arguments), position));
	FunctionDecl *decl = p_method->node && p_method->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_method->node) : nullptr;
	engine_arguments(decl, p_call->arguments, position, list);
	if (p_before) {
		p_before(); // What the receiver still needs, now that the arguments are worked out.
	}
	uint32_t out = kind == Kind::Void ? NONE : engine_reg(kind, p_method->type ? compilation_.type_of(p_method->type) : nullptr);
	emit(Op::HostCall, position, out, name, arguments(std::move(list)));
	return out == NONE ? 0 : out;
}

uint32_t Lowerer::static_call(Call *p_call, Symbol *p_method) {
	uint32_t position = p_call->offset;
	std::string klass, method;
	if (!p_method->owner || !godot_name(p_method->owner->name, std::string_view(), &klass) ||
			!godot_name(p_method->owner->name, p_method->name, &method)) {
		refuse(p_call->callee->offset, "calling '" + std::string(p_method->name) + "'");
		return 0;
	}
	Kind kind = Kind::Void;
	if (p_method->type && !kind_of(compilation_.type_of(p_method->type), &kind)) {
		refuse(position, "a call to a Godot method that returns '" + compilation_.type_name(compilation_.type_of(p_method->type)) + "'");
	}
	std::vector<uint32_t> list;
	FunctionDecl *decl = p_method->node && p_method->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_method->node) : nullptr;
	engine_arguments(decl, p_call->arguments, position, list);
	uint32_t out = kind == Kind::Void ? NONE : engine_reg(kind, p_method->type ? compilation_.type_of(p_method->type) : nullptr);
	emit(Op::HostCallStatic, position, out, string(klass + "." + method), arguments(std::move(list)));
	return out == NONE ? 0 : out;
}

void Lowerer::constructor_arguments(Symbol *p_class, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list) {
	if (is_error(p_class) && p_class->unit->is_builtin) {
		// Error(message = ""): the one parameter the interpreter keeps.
		if (p_arguments.count > 1) {
			refuse(p_position, "an Error with an id");
		}
		if (p_arguments.count >= 1) {
			r_list.push_back(expression(p_arguments[0], Kind::Str));
		} else {
			uint32_t empty = reg(Kind::Str);
			emit(Op::ConstStr, p_position, empty, string(""));
			r_list.push_back(empty);
		}
		return;
	}
	Symbol *constructor = p_class->klass ? p_class->klass->constructor : nullptr;
	FunctionDecl *decl = constructor && constructor->node && constructor->node->kind == NodeKind::FunctionDecl
			? static_cast<FunctionDecl *>(constructor->node)
			: nullptr;
	call_arguments(decl, p_arguments, p_position, r_list);
}

} // namespace ir
} // namespace kas3
