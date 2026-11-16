#include "lower_dynamic.h"
#include "lower_engine_calls.h"
#include "lower_functions.h"
#include "lower_members.h"
#include "lower_vectors.h"

namespace kas3 {
namespace ir {

void Lowerer::call_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list) {
	uint32_t count = p_function ? p_function->parameters.count : 0;
	const bool rest = count > 0 && p_function->parameters[count - 1].is_rest;
	if (p_arguments.count > count && !rest) {
		refuse(p_position, "a call with extra arguments");
	}
	for (uint32_t i = 0; i < count; i++) {
		Parameter &parameter = p_function->parameters[i];
		if (parameter.is_rest) {
			// What is left goes untyped: the function that is called makes them its Array.
			for (uint32_t at = i; at < p_arguments.count; at++) {
				Expr *later = writer(p_arguments, at + 1);
				r_list.push_back(settled(any_value(p_arguments[at]), later, p_position));
			}
			break;
		}
		Kind kind = Kind::F64;
		// A parameter with no type written is `*`.
		const Type *type = parameter.type ? compilation_.type_of(parameter.type) : compilation_.any_type();
		if (!kind_of(type, &kind)) {
			refuse(p_position, "a call to a function with a parameter of type '" + compilation_.type_name(type) + "'");
			break;
		}
		if (i >= p_arguments.count) {
			break; // Left out: the function that is called supplies its own default.
		}
		Expr *later = writer(p_arguments, i + 1);
		r_list.push_back(settled(typed(p_arguments[i], type, kind), later, p_position));
	}
}

Expr *Lowerer::writer(const List<Expr *> &p_expressions, uint32_t p_from, uint32_t p_end) {
	for (uint32_t at = p_from; at < p_end && at < p_expressions.count; at++) {
		if (writes_locals(p_expressions[at])) {
			return p_expressions[at];
		}
	}
	return nullptr;
}

uint32_t Lowerer::call_result(Symbol *p_function, uint32_t p_position, Kind *r_kind) {
	// A function with no return type written gives `*`; `:void` is written, and gives nothing.
	Kind kind = Kind::Any;
	if (p_function->type && !kind_of(compilation_.type_of(p_function->type), &kind)) {
		refuse(p_position, "a call to a function that returns '" + compilation_.type_name(compilation_.type_of(p_function->type)) + "'");
	}
	*r_kind = kind;
	return kind == Kind::Void ? NONE : reg(kind);
}

// super(...): the base class's constructor. It has no value: the checker lets nothing use it.
uint32_t Lowerer::super_call(Call *p_call) {
	Symbol *base = class_ && class_->klass ? class_->klass->base : nullptr;
	uint32_t base_index = 0;
	if (base && base != object_ && class_index(base, &base_index)) {
		std::vector<uint32_t> list = { self(p_call->offset, true) };
		constructor_arguments(base, p_call->arguments, p_call->offset, list);
		emit(Op::Call, p_call->offset, NONE, module_.classes[base_index].constructor, arguments(std::move(list)));
	}
	return 0;
}

// A conversion written as a call: int(x), String(x).
// A value converted by a conversion written out. To or from an Int64 that is AS3's own (see convert()),
// and in each branch of a `?:`, whatever the two branches join to: `int(c ? wide : 1.5)` cuts the wide one.
uint32_t Lowerer::written_value(Expr *p_value, Kind p_conversion, uint32_t p_position) {
	if (p_value->kind == NodeKind::Conditional) {
		Conditional *conditional = static_cast<Conditional *>(p_value);
		uint32_t out = reg(p_conversion);
		uint32_t to_else = emit(Op::JumpIfNot, p_position, expression(conditional->condition, Kind::Bool), 0);
		emit(Op::Move, p_position, out, written_value(conditional->then_value, p_conversion, p_position));
		uint32_t to_end = emit(Op::Jump, p_position, 0);
		patch(to_else, here());
		emit(Op::Move, p_position, out, written_value(conditional->else_value, p_conversion, p_position));
		patch(to_end, here());
		return out;
	}
	Kind given = Kind::Void;
	bool known = kind_of(p_value->type, &given);
	if (known && (given == Kind::I64 || p_conversion == Kind::I64) && !is_null(p_value)) {
		return convert(wide_operand(p_value, p_conversion), p_conversion, p_position, true);
	}
	return expression(p_value, p_conversion);
}

uint32_t Lowerer::cast(Call *p_call, Symbol *p_type, Kind p_want) {
	uint32_t position = p_call->offset;
	if (p_type == array_ && array_) {
		return Vectors(*this).vector_new(compilation_.instance_type(array_), p_call->arguments, p_call->callee->offset); // Array(4) is new Array(4).
	}
	Kind conversion = Kind::Void;
	if (!kind_of_class(p_type, &conversion) || p_call->arguments.count != 1) {
		refuse(p_call->callee->offset, "a cast to '" + std::string(p_type->name) + "'");
		return default_value(p_want, position);
	}
	if (conversion == Kind::Str && untyped(p_call->arguments[0])) {
		return any_text(p_call->arguments[0]); // String(x): "undefined" and "null" are words.
	}
	uint32_t value = written_value(p_call->arguments[0], conversion, position);
	if (conversion != Kind::Str) {
		return value;
	}
	// The conversion written out always gives a string: a missing one becomes "null".
	uint32_t text = reg(Kind::Str);
	emit(Op::StrToStr, position, text, value);
	return text;
}

uint32_t Lowerer::trace_call(Call *p_call) {
	// Every argument is worked out first, and only then is each put into words, as in Flash: a vector
	// that a later argument changes is traced as it is after the change.
	std::vector<uint32_t> values;
	for (uint32_t i = 0; i < p_call->arguments.count; i++) {
		Expr *later = writer(p_call->arguments, i + 1);
		Kind kind = Kind::Void;
		const bool spoken_later = !is_null(p_call->arguments[i]) && kind_of(p_call->arguments[i]->type, &kind) && (kind == Kind::Obj || kind == Kind::Any);
		values.push_back(settled(expression(p_call->arguments[i], spoken_later ? kind : Kind::Str), later, p_call->offset));
	}
	std::vector<uint32_t> list;
	for (uint32_t i = 0; i < p_call->arguments.count; i++) {
		// A null string traces as "null".
		uint32_t text = reg(Kind::Str);
		uint32_t words = values[i];
		if (kind_at(words) == Kind::Any) {
			words = reg(Kind::Str);
			emit(Op::AnyText, p_call->arguments[i]->offset, words, values[i]); // "undefined", not null.
		}
		emit(Op::StrToStr, p_call->arguments[i]->offset, text, convert(words, Kind::Str, p_call->arguments[i]->offset));
		list.push_back(text);
	}
	emit(Op::Trace, p_call->offset, arguments(std::move(list)));
	return 0;
}

// A function or method of the program itself.
uint32_t Lowerer::script_call(Call *p_call, Symbol *p_symbol, uint32_t p_function) {
	Expr *callee = p_call->callee;
	uint32_t position = p_call->offset;
	FunctionDecl *decl = static_cast<FunctionDecl *>(p_symbol->node);
	std::vector<uint32_t> list;
	bool direct = true;
	bool on_another = false; // Called on something other than `this`: it may be missing.
	if (p_symbol->kind == SymbolKind::Method && !p_symbol->is_static()) {
		direct = (p_symbol->modifiers & Modifiers::Private) != 0;
		if (callee->kind == NodeKind::Member) {
			Expr *object = static_cast<Member *>(callee)->object;
			on_another = object->kind != NodeKind::ThisExpr && object->kind != NodeKind::SuperExpr;
			direct = direct || object->kind == NodeKind::SuperExpr;
			list.push_back(settled(receiver(object), writer(p_call->arguments), position));
		} else {
			if (fn().is_static) {
				refuse(callee->offset, "this call");
			}
			list.push_back(self(callee->offset));
		}
	}
	// The class is looked up before the arguments, and set up then, as in AVM2.
	if (p_symbol->is_static() && p_symbol->owner) {
		touch(p_symbol->owner, position);
	}
	call_arguments(decl, p_call->arguments, position, list);
	if (on_another) {
		emit(Op::NullCheck, position, list[0]); // After the arguments.
	}
	Kind kind = Kind::Void;
	uint32_t out = call_result(p_symbol, position, &kind);
	if (direct) {
		emit(Op::Call, position, out, p_function, arguments(std::move(list)));
	} else {
		const uint32_t at = emit(Op::CallVirtual, position, out, slots_[decl], arguments(std::move(list)));
		uint32_t declared = NONE;
		if (on_another && class_index(p_symbol->owner, &declared)) {
			fn().virtual_receivers.emplace_back(at, declared);
		}
	}
	return out == NONE ? 0 : out;
}

uint32_t Lowerer::call(Call *p_call, Kind p_want) {
	Expr *callee = p_call->callee;
	uint32_t position = p_call->offset;
	if (callee->kind == NodeKind::SuperExpr) {
		return super_call(p_call);
	}
	if (const std::optional<uint32_t> signal = callee_symbol(callee) ? std::nullopt : EngineCalls(*this).signal_call_by_name(p_call)) {
		return *signal; // `timer[name].add(f)`: a signal by its AS3 name.
	}
	if (const std::optional<uint32_t> by_name = Dynamic(*this).call(p_call)) {
		return *by_name;
	}
	if (callee->kind == NodeKind::Member && is_vector(static_cast<Member *>(callee)->object->type)) {
		return Vectors(*this).vector_call(p_call);
	}
	if (const std::optional<uint32_t> called = Functions(*this).function_method(p_call)) {
		return *called; // f.call(), f.apply()
	}
	Symbol *symbol = callee_symbol(callee);
	if (!symbol) {
		if (const std::optional<uint32_t> by_name = EngineCalls(*this).signal_call_by_name(p_call)) {
			return *by_name;
		}
		if (const std::optional<uint32_t> text = library_call(p_call, nullptr)) {
			return *text; // `x.toString()` on an untyped value.
		}
		return Functions(*this).value_call(p_call, p_want); // `make()(1)`, `table[name](2)`: whatever it gives is called.
	}
	if (Functions::called_value(symbol)) {
		return Functions(*this).value_call(p_call, p_want); // A variable, a field or a parameter that holds a function.
	}
	if (symbol->is_type()) {
		return cast(p_call, symbol, p_want);
	}
	if (symbol->unit && symbol->unit->is_builtin && symbol->kind == SymbolKind::Function && symbol->name == "trace") {
		return trace_call(p_call);
	}
	if (symbol->name == "toString" && p_call->arguments.count == 0 && callee->kind == NodeKind::Member &&
			static_cast<Member *>(callee)->object->kind != NodeKind::SuperExpr && is_engine(natural(static_cast<Member *>(callee)->object))) {
		return EngineCalls(*this).engine_text(p_call);
	}
	if (is_godot(symbol) && symbol->kind == SymbolKind::Method) {
		return EngineCalls(*this).godot_call(p_call, symbol);
	}
	if (is_godot(symbol) && symbol->kind == SymbolKind::Function) {
		return EngineCalls(*this).global_call(p_call, symbol, p_want);
	}
	if (const std::optional<uint32_t> math = math_call(p_call, symbol)) {
		return *math;
	}
	if (const std::optional<uint32_t> library = library_call(p_call, symbol)) {
		return *library;
	}
	if (symbol->name == "getStackTrace" && symbol->owner == error_ && callee->kind == NodeKind::Member && p_call->arguments.count == 0) {
		// An Error's stack trace, taken when it was made.
		const uint32_t object = receiver(static_cast<Member *>(callee)->object);
		emit(Op::NullCheck, position, object);
		uint32_t out = reg(Kind::Str);
		emit(Op::GetField, position, out, object, ERROR_STACK);
		return out;
	}
	if (const std::optional<uint32_t> through = Members(*this).interface_call(p_call, symbol)) {
		return *through; // On whatever class the object is.
	}
	auto function = functions_.find(declaration(symbol));
	if (function == functions_.end()) {
		refuse(callee->offset, "calling '" + std::string(symbol->name) + "'");
		return default_value(p_want, position);
	}
	return script_call(p_call, symbol, function->second);
}

void Lowerer::constructor_arguments(Symbol *p_class, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list) {
	if (is_error(p_class) && p_class->unit->is_builtin) {
		// Error(message = "", id = 0).
		if (p_arguments.count >= 1) {
			r_list.push_back(settled(expression(p_arguments[0], Kind::Str), p_arguments.count > 1 ? p_arguments[1] : nullptr, p_position));
		} else {
			uint32_t empty = reg(Kind::Str);
			emit(Op::ConstStr, p_position, empty, string(""));
			r_list.push_back(empty);
		}
		r_list.push_back(p_arguments.count >= 2 ? expression(p_arguments[1], Kind::I32) : default_value(Kind::I32, p_position));
		if (p_arguments.count > 2) {
			refuse(p_position, "an Error with more than a message and an id");
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
