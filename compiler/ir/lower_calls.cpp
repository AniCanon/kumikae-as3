#include "lower_calls.h"
#include "lower_dynamic.h"
#include "lower_engine_calls.h"
#include "lower_functions.h"
#include "lower_members.h"
#include "lower_vectors.h"

namespace kas3 {
namespace ir {

void Calls::call_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list) {
	uint32_t count = p_function ? p_function->parameters.count : 0;
	const bool rest = count > 0 && p_function->parameters[count - 1].is_rest;
	if (p_arguments.count > count && !rest) {
		l_.refuse(p_position, "a call with extra arguments");
	}
	for (uint32_t i = 0; i < count; i++) {
		Parameter &parameter = p_function->parameters[i];
		if (parameter.is_rest) {
			// What is left goes untyped: the function that is called makes them its Array.
			for (uint32_t at = i; at < p_arguments.count; at++) {
				Expr *later = Lowerer::writer(p_arguments, at + 1);
				r_list.push_back(l_.settled(l_.any_value(p_arguments[at]), later, p_position));
			}
			break;
		}
		Kind kind = Kind::F64;
		// A parameter with no type written is `*`.
		const Type *type = parameter.type ? l_.compilation_.type_of(parameter.type) : l_.compilation_.any_type();
		if (!l_.kind_of(type, &kind)) {
			l_.refuse(p_position, "a call to a function with a parameter of type '" + l_.compilation_.type_name(type) + "'");
			break;
		}
		if (i >= p_arguments.count) {
			break; // Left out: the function that is called supplies its own default.
		}
		Expr *later = Lowerer::writer(p_arguments, i + 1);
		r_list.push_back(l_.settled(l_.typed(p_arguments[i], type, kind), later, p_position));
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

uint32_t Calls::call_result(Symbol *p_function, uint32_t p_position, Kind *r_kind) {
	// A function with no return type written gives `*`; `:void` is written, and gives nothing.
	Kind kind = Kind::Any;
	if (p_function->type && !l_.kind_of(l_.compilation_.type_of(p_function->type), &kind)) {
		l_.refuse(p_position, "a call to a function that returns '" + l_.compilation_.type_name(l_.compilation_.type_of(p_function->type)) + "'");
	}
	*r_kind = kind;
	return kind == Kind::Void ? NONE : l_.reg(kind);
}

// super(...): the base class's constructor. It has no value: the checker lets nothing use it.
uint32_t Calls::super_call(Call *p_call) {
	Symbol *base = l_.class_ && l_.class_->klass ? l_.class_->klass->base : nullptr;
	uint32_t base_index = 0;
	if (base && base != l_.object_ && l_.class_index(base, &base_index)) {
		std::vector<uint32_t> list = { l_.self(p_call->offset, true) };
		constructor_arguments(base, p_call->arguments, p_call->offset, list);
		l_.emit(Op::Call, p_call->offset, NONE, l_.module_.classes[base_index].constructor, l_.arguments(std::move(list)));
	}
	return 0;
}

// A conversion written as a call: int(x), String(x).
// A value converted by a conversion written out. To or from an Int64 that is AS3's own (see convert()),
// and in each branch of a `?:`, whatever the two branches join to: `int(c ? wide : 1.5)` cuts the wide one.
uint32_t Calls::written_value(Expr *p_value, Kind p_conversion, uint32_t p_position) {
	if (p_value->kind == NodeKind::Conditional) {
		Conditional *conditional = static_cast<Conditional *>(p_value);
		uint32_t out = l_.reg(p_conversion);
		uint32_t to_else = l_.emit(Op::JumpIfNot, p_position, l_.expression(conditional->condition, Kind::Bool), 0);
		l_.emit(Op::Move, p_position, out, written_value(conditional->then_value, p_conversion, p_position));
		uint32_t to_end = l_.emit(Op::Jump, p_position, 0);
		l_.patch(to_else, l_.here());
		l_.emit(Op::Move, p_position, out, written_value(conditional->else_value, p_conversion, p_position));
		l_.patch(to_end, l_.here());
		return out;
	}
	Kind given = Kind::Void;
	bool known = l_.kind_of(p_value->type, &given);
	if (known && (given == Kind::I64 || p_conversion == Kind::I64) && !Lowerer::is_null(p_value)) {
		return l_.convert(l_.wide_operand(p_value, p_conversion), p_conversion, p_position, true);
	}
	return l_.expression(p_value, p_conversion);
}

uint32_t Calls::cast(Call *p_call, Symbol *p_type, Kind p_want) {
	uint32_t position = p_call->offset;
	if (p_type == l_.array_ && l_.array_) {
		return Vectors(l_).vector_new(l_.compilation_.instance_type(l_.array_), p_call->arguments, p_call->callee->offset); // Array(4) is new Array(4).
	}
	if (p_type == l_.object_ && p_call->arguments.count <= 1) {
		return Dynamic(l_).object_from(p_call->arguments.count ? p_call->arguments[0] : nullptr, position);
	}
	Kind conversion = Kind::Void;
	if (!l_.kind_of_class(p_type, &conversion) || p_call->arguments.count > 1) {
		l_.refuse(p_call->callee->offset, "a cast to '" + std::string(p_type->name) + "'");
		return l_.default_value(p_want, position);
	}
	return conversion_of(p_call->arguments, conversion, position);
}

uint32_t Calls::conversion_of(const List<Expr *> &p_arguments, Kind p_conversion, uint32_t p_position) {
	if (p_arguments.count == 0) {
		if (p_conversion == Kind::Str || p_conversion == Kind::F64) {
			const uint32_t empty = l_.reg(p_conversion);
			l_.emit(p_conversion == Kind::Str ? Op::ConstStr : Op::ConstF64, p_position, empty, p_conversion == Kind::Str ? l_.string("") : l_.number(0));
			return empty;
		}
		return l_.default_value(p_conversion, p_position);
	}
	if (p_conversion == Kind::Str && l_.untyped(p_arguments[0])) {
		return l_.any_text(p_arguments[0]); // String(x): "undefined" and "null" are words.
	}
	uint32_t value = written_value(p_arguments[0], p_conversion, p_position);
	if (p_conversion != Kind::Str) {
		return value;
	}
	// The conversion written out always gives a string: a missing one becomes "null".
	uint32_t text = l_.reg(Kind::Str);
	l_.emit(Op::StrToStr, p_position, text, value);
	return text;
}

uint32_t Calls::trace_call(Call *p_call) {
	// Every argument is worked out first, and only then is each put into words, as in Flash: a vector
	// that a later argument changes is traced as it is after the change.
	std::vector<uint32_t> values;
	for (uint32_t i = 0; i < p_call->arguments.count; i++) {
		Expr *later = Lowerer::writer(p_call->arguments, i + 1);
		Kind kind = Kind::Void;
		const bool spoken_later = !Lowerer::is_null(p_call->arguments[i]) && l_.kind_of(p_call->arguments[i]->type, &kind) && (kind == Kind::Obj || kind == Kind::Any);
		values.push_back(l_.settled(l_.expression(p_call->arguments[i], spoken_later ? kind : Kind::Str), later, p_call->offset));
	}
	std::vector<uint32_t> list;
	for (uint32_t i = 0; i < p_call->arguments.count; i++) {
		// A null string traces as "null".
		uint32_t text = l_.reg(Kind::Str);
		uint32_t words = values[i];
		if (l_.kind_at(words) == Kind::Any) {
			words = l_.reg(Kind::Str);
			l_.emit(Op::AnyText, p_call->arguments[i]->offset, words, values[i]); // "undefined", not null.
		}
		l_.emit(Op::StrToStr, p_call->arguments[i]->offset, text, l_.convert(words, Kind::Str, p_call->arguments[i]->offset));
		list.push_back(text);
	}
	l_.emit(Op::Trace, p_call->offset, l_.arguments(std::move(list)));
	return 0;
}

// A function or method of the program itself.
uint32_t Calls::script_call(Call *p_call, Symbol *p_symbol, uint32_t p_function) {
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
			list.push_back(l_.settled(l_.receiver(object), Lowerer::writer(p_call->arguments), position));
		} else {
			if (l_.fn().is_static) {
				l_.refuse(callee->offset, "this call");
			}
			list.push_back(l_.self(callee->offset));
		}
	}
	// The class is looked up before the arguments, and set up then, as in AVM2.
	if (p_symbol->is_static() && p_symbol->owner) {
		l_.touch(p_symbol->owner, position);
	}
	call_arguments(decl, p_call->arguments, position, list);
	if (on_another) {
		l_.emit(Op::NullCheck, position, list[0]); // After the arguments.
	}
	Kind kind = Kind::Void;
	uint32_t out = call_result(p_symbol, position, &kind);
	if (direct) {
		l_.emit(Op::Call, position, out, p_function, l_.arguments(std::move(list)));
	} else {
		const uint32_t at = l_.emit(Op::CallVirtual, position, out, l_.slots_[decl], l_.arguments(std::move(list)));
		uint32_t declared = NONE;
		if (on_another && l_.class_index(p_symbol->owner, &declared)) {
			l_.fn().virtual_receivers.emplace_back(at, declared);
		}
	}
	return out == NONE ? 0 : out;
}

uint32_t Calls::call(Call *p_call, Kind p_want) {
	Expr *callee = p_call->callee;
	if (callee->kind == NodeKind::SuperExpr) {
		return super_call(p_call);
	}
	if (callee->kind == NodeKind::TypeApply) {
		return Vectors(l_).vector_from(p_call);
	}
	if (const std::optional<uint32_t> signal = l_.callee_symbol(callee) ? std::nullopt : EngineCalls(l_).signal_call_by_name(p_call)) {
		return *signal; // `timer[name].add(f)`: a signal by its AS3 name.
	}
	if (const std::optional<uint32_t> by_name = Dynamic(l_).call(p_call)) {
		return *by_name;
	}
	if (callee->kind == NodeKind::Member && l_.is_vector(static_cast<Member *>(callee)->object->type)) {
		return Vectors(l_).vector_call(p_call);
	}
	if (const std::optional<uint32_t> called = Functions(l_).function_method(p_call)) {
		return *called; // f.call(), f.apply()
	}
	Symbol *symbol = l_.callee_symbol(callee);
	if (!symbol) {
		if (const std::optional<uint32_t> by_name = EngineCalls(l_).signal_call_by_name(p_call)) {
			return *by_name;
		}
		if (const std::optional<uint32_t> text = l_.library_call(p_call, nullptr)) {
			return *text; // `x.toString()` on an untyped value.
		}
		return Functions(l_).value_call(p_call, p_want); // `make()(1)`, `table[name](2)`: whatever it gives is called.
	}
	return symbol_call(p_call, symbol, p_want);
}

// A call of what a name or a member names, by what it is.
uint32_t Calls::symbol_call(Call *p_call, Symbol *p_symbol, Kind p_want) {
	Expr *callee = p_call->callee;
	uint32_t position = p_call->offset;
	if (Functions::called_value(p_symbol)) {
		return Functions(l_).value_call(p_call, p_want); // A variable, a field or a parameter that holds a function.
	}
	if (p_symbol->is_type()) {
		return cast(p_call, p_symbol, p_want);
	}
	if (p_symbol->unit && p_symbol->unit->is_builtin && p_symbol->kind == SymbolKind::Function && p_symbol->name == "trace") {
		return trace_call(p_call);
	}
	if (p_symbol->name == "toString" && p_call->arguments.count == 0 && callee->kind == NodeKind::Member &&
			static_cast<Member *>(callee)->object->kind != NodeKind::SuperExpr && is_engine(l_.natural(static_cast<Member *>(callee)->object))) {
		return EngineCalls(l_).engine_text(p_call);
	}
	if (Lowerer::is_godot(p_symbol) && p_symbol->kind == SymbolKind::Method) {
		return EngineCalls(l_).godot_call(p_call, p_symbol);
	}
	if (Lowerer::is_godot(p_symbol) && p_symbol->kind == SymbolKind::Function) {
		return EngineCalls(l_).global_call(p_call, p_symbol, p_want);
	}
	if (const std::optional<uint32_t> math = l_.math_call(p_call, p_symbol)) {
		return *math;
	}
	if (const std::optional<uint32_t> library = l_.library_call(p_call, p_symbol)) {
		return *library;
	}
	if (p_symbol->name == "getStackTrace" && p_symbol->owner == l_.error_ && callee->kind == NodeKind::Member && p_call->arguments.count == 0) {
		// An Error's stack trace, taken when it was made.
		const uint32_t object = l_.receiver(static_cast<Member *>(callee)->object);
		l_.emit(Op::NullCheck, position, object);
		uint32_t out = l_.reg(Kind::Str);
		l_.emit(Op::GetField, position, out, object, ERROR_STACK);
		return out;
	}
	if (const std::optional<uint32_t> through = Members(l_).interface_call(p_call, p_symbol)) {
		return *through; // On whatever class the object is.
	}
	auto function = l_.functions_.find(declaration(p_symbol));
	if (function == l_.functions_.end()) {
		l_.refuse(callee->offset, "calling '" + std::string(p_symbol->name) + "'");
		return l_.default_value(p_want, position);
	}
	return script_call(p_call, p_symbol, function->second);
}

void Calls::constructor_arguments(Symbol *p_class, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list) {
	if (l_.is_error(p_class) && p_class->unit->is_builtin) {
		// Error(message = "", id = 0).
		if (p_arguments.count >= 1) {
			r_list.push_back(l_.settled(l_.expression(p_arguments[0], Kind::Str), p_arguments.count > 1 ? p_arguments[1] : nullptr, p_position));
		} else {
			uint32_t empty = l_.reg(Kind::Str);
			l_.emit(Op::ConstStr, p_position, empty, l_.string(""));
			r_list.push_back(empty);
		}
		r_list.push_back(p_arguments.count >= 2 ? l_.expression(p_arguments[1], Kind::I32) : l_.default_value(Kind::I32, p_position));
		if (p_arguments.count > 2) {
			l_.refuse(p_position, "an Error with more than a message and an id");
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
