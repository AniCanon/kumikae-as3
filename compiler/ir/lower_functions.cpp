// Function values: a method or a function taken as a value, and a call through one.
#include "lowerer.h"

namespace kas3 {
namespace ir {

// `obj.method`, `method`, `helper`: a function value. A method is bound to its object, and is the override
// of that object's class; a static method or a package function is bound to nothing.
uint32_t Lowerer::function_value(Expr *p_expression) {
	const uint32_t position = p_expression->offset;
	Symbol *symbol = callee_symbol(p_expression);
	uint32_t klass = NONE;
	if (!symbol || !class_index(function_, &klass)) {
		refuse(position, "this function used as a value");
		return default_value(Kind::Obj, position);
	}
	if (is_godot(symbol)) {
		refuse(position, "one of Godot's methods used as a value");
		return default_value(Kind::Obj, position);
	}
	auto function = functions_.find(symbol);
	if (function == functions_.end()) {
		refuse(position, "'" + std::string(symbol->name) + "' used as a value");
		return default_value(Kind::Obj, position);
	}
	uint32_t out = reg(Kind::Obj);
	if (symbol->kind != SymbolKind::Method || symbol->is_static()) {
		emit(Op::MakeFunction, position, out, function->second, NONE);
		return out;
	}
	uint32_t object = 0;
	if (p_expression->kind == NodeKind::Member) {
		Expr *written = static_cast<Member *>(p_expression)->object;
		if (written->kind == NodeKind::SuperExpr) {
			refuse(position, "a method of 'super' used as a value");
			return default_value(Kind::Obj, position);
		}
		object = written->kind == NodeKind::ThisExpr ? 0 : receiver(written);
	} else if (fn().is_static) {
		refuse(position, "this name");
		return default_value(Kind::Obj, position);
	}
	// A private or final method is that very one; any other is the object's class's own.
	if ((symbol->modifiers & Modifiers::Private) || (symbol->modifiers & Modifiers::Final)) {
		emit(Op::NullCheck, position, object);
		emit(Op::MakeFunction, position, out, function->second, object);
	} else {
		emit(Op::MakeMethod, position, out, slots_[symbol], object);
	}
	return out;
}

// Whether a call's callee is a value to call (a variable, a field, a parameter that holds a function), not
// a function or a method named.
bool Lowerer::called_value(const Symbol *p_symbol) {
	switch (p_symbol->kind) {
		case SymbolKind::Variable:
		case SymbolKind::Field:
		case SymbolKind::Parameter:
		case SymbolKind::Local:
		case SymbolKind::CatchVariable:
			return true;
		default:
			return false;
	}
}

// f(a, b) on a function value: each argument untyped, and what it gives back too.
uint32_t Lowerer::value_call(Call *p_call, Kind p_want) {
	const uint32_t position = p_call->offset;
	const uint32_t value = settled(expression(p_call->callee, Kind::Void), writer(p_call->arguments), position);
	if (kind_at(value) != Kind::Obj && kind_at(value) != Kind::Any) {
		refuse(p_call->callee->offset, "calling a value of type '" + compilation_.type_name(p_call->callee->type) + "'");
		return default_value(p_want, position);
	}
	if (module_.argument_error_class == NONE) {
		if (Symbol *argument_error = compilation_.builtin("ArgumentError")) {
			class_index(argument_error, &module_.argument_error_class);
		}
	}
	std::vector<uint32_t> list;
	for (uint32_t i = 0; i < p_call->arguments.count; i++) {
		Expr *later = nullptr;
		for (uint32_t next = i + 1; next < p_call->arguments.count && !later; next++) {
			later = writes_locals(p_call->arguments[next]) ? p_call->arguments[next] : nullptr;
		}
		list.push_back(settled(any_value(p_call->arguments[i]), later, position));
	}
	uint32_t out = reg(Kind::Any);
	emit(Op::CallFunction, position, out, value, arguments(std::move(list)));
	return out;
}

} // namespace ir
} // namespace kas3
