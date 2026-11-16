// Function values: a method or a function taken as a value, and a call through one.
#include "lower_dynamic.h"
#include "lower_functions.h"
#include "lower_members.h"
#include "lower_vectors.h"

namespace kas3 {
namespace ir {

// `obj.method`, `method`, `helper`: a function value. A method is bound to its object, and is the override
// of that object's class; a static method or a package function is bound to nothing.
uint32_t Functions::function_value(Expr *p_expression) {
	const uint32_t position = p_expression->offset;
	Symbol *symbol = l_.callee_symbol(p_expression);
	uint32_t klass = NONE;
	if (!symbol || !l_.class_index(l_.function_, &klass)) {
		l_.refuse(position, "this function used as a value");
		return l_.default_value(Kind::Obj, position);
	}
	if (Lowerer::is_godot(symbol)) {
		l_.refuse(position, "one of Godot's methods used as a value");
		return l_.default_value(Kind::Obj, position);
	}
	if (const std::optional<uint32_t> bound = Members(l_).interface_value(p_expression, symbol)) {
		return *bound;
	}
	auto function = l_.functions_.find(declaration(symbol));
	const bool library = symbol->unit && symbol->unit->is_builtin && symbol->kind == SymbolKind::Method && !symbol->is_static();
	if (function == l_.functions_.end() && library && p_expression->kind == NodeKind::Member &&
			static_cast<Member *>(p_expression)->object->kind != NodeKind::SuperExpr) {
		return Dynamic(l_).method_value(static_cast<Member *>(p_expression), klass);
	}
	if (function == l_.functions_.end()) {
		l_.refuse(position, "'" + std::string(symbol->name) + "' used as a value");
		return l_.default_value(Kind::Obj, position);
	}
	uint32_t out = l_.reg(Kind::Obj);
	if (symbol->kind != SymbolKind::Method || symbol->is_static()) {
		l_.emit(Op::MakeFunction, position, out, function->second, NONE);
		return out;
	}
	uint32_t object = 0;
	if (p_expression->kind == NodeKind::Member) {
		Expr *written = static_cast<Member *>(p_expression)->object;
		if (written->kind == NodeKind::SuperExpr) {
			l_.refuse(position, "a method of 'super' used as a value");
			return l_.default_value(Kind::Obj, position);
		}
		object = l_.receiver(written);
	} else if (l_.fn().is_static) {
		l_.refuse(position, "this name");
		return l_.default_value(Kind::Obj, position);
	} else {
		object = l_.self(position);
	}
	// A private or final method is that very one; any other is the object's class's own.
	if ((symbol->modifiers & Modifiers::Private) || (symbol->modifiers & Modifiers::Final)) {
		l_.emit(Op::NullCheck, position, object);
		l_.emit(Op::MakeFunction, position, out, function->second, object);
	} else {
		l_.emit(Op::MakeMethod, position, out, l_.slots_[function->first], object);
	}
	return out;
}

// Whether a call's callee is a value to call (a variable, a field, a parameter that holds a function), not
// a function or a method named.
bool Functions::called_value(const Symbol *p_symbol) {
	switch (p_symbol->kind) {
		case SymbolKind::Variable:
		case SymbolKind::Field:
		case SymbolKind::Parameter:
		case SymbolKind::Local:
		case SymbolKind::CatchVariable:
			return true;
		case SymbolKind::Property: // A getter that gives a function: `o.action(3)` calls what it gives.
			return p_symbol->unit && !p_symbol->unit->is_declaration;
		default:
			return false;
	}
}

// A call of a function value may raise AS3's ArgumentError: the class it is of.
void Functions::use_argument_error() {
	if (l_.module_.argument_error_class == NONE) {
		if (Symbol *argument_error = l_.compilation_.builtin("ArgumentError")) {
			l_.class_index(argument_error, &l_.module_.argument_error_class);
		}
	}
}

// f(a, b) on a function value: each argument untyped, and what it gives back too.
uint32_t Functions::value_call(Call *p_call, Kind p_want) {
	const uint32_t position = p_call->offset;
	const uint32_t value = l_.settled(l_.expression(p_call->callee, Kind::Void), Lowerer::writer(p_call->arguments), position);
	if (l_.kind_at(value) != Kind::Obj && l_.kind_at(value) != Kind::Any) {
		l_.refuse(p_call->callee->offset, "calling a value of type '" + l_.compilation_.type_name(p_call->callee->type) + "'");
		return l_.default_value(p_want, position);
	}
	use_argument_error();
	std::vector<uint32_t> list;
	for (uint32_t i = 0; i < p_call->arguments.count; i++) {
		Expr *later = Lowerer::writer(p_call->arguments, i + 1);
		list.push_back(l_.settled(l_.any_value(p_call->arguments[i]), later, position));
	}
	uint32_t out = l_.reg(Kind::Any);
	l_.emit(Op::CallFunction, position, out, value, l_.arguments(std::move(list)));
	return out;
}

// What map and filter make, a vector of the class, and what some and every start from; nothing for forEach.
uint32_t Vectors::each_result(std::string_view p_name, uint32_t p_class, uint32_t p_limit, uint32_t p_position) {
	const bool map = p_name == "map", filter = p_name == "filter", some = p_name == "some", every = p_name == "every";
	uint32_t out = NONE;
	if (map || filter) {
		out = l_.reg(Kind::Obj);
		const bool sized = map && !l_.module_.classes[p_class].array;
		l_.emit(Op::VecNew, p_position, out, p_class, l_.arguments({ sized ? p_limit : l_.default_value(Kind::U32, p_position), l_.default_value(Kind::Bool, p_position) }));
	} else if (some || every) {
		out = l_.reg(Kind::Bool);
		l_.emit(Op::ConstBool, p_position, out, every ? 1 : 0);
	}
	return out;
}

// What the function's answer for one element does: map keeps it, filter keeps the element when the answer
// is true, and the first that settles some() or every() ends the loop. The jump out of it, or NONE.
uint32_t Vectors::each_answer(Call *p_call, uint32_t p_class, uint32_t p_out, uint32_t p_at, uint32_t p_item, uint32_t p_answer,
		uint32_t p_yes) {
	const uint32_t position = p_call->offset;
	Member *callee = static_cast<Member *>(p_call->callee);
	const std::string_view name = callee->name;
	const bool map = name == "map", filter = name == "filter", some = name == "some", every = name == "every";
	const Kind element = l_.module_.classes[p_class].element;
	// Whether the function answered true itself, not just something that is true as a condition.
	const auto answered_true = [&](uint32_t p_value) {
		const uint32_t out = l_.reg(Kind::Bool);
		l_.emit(Op::AnyStrictEq, position, out, p_value, p_yes);
		return out;
	};
	uint32_t found = NONE;
	if (map && l_.module_.classes[p_class].array) {
		l_.emit(Op::VecCall, position, NONE, static_cast<uint32_t>(VectorMethod::Push), l_.arguments({ p_out, p_answer }));
	} else if (map) {
		l_.emit(Op::VecSet, position, p_out, p_at, l_.from_untyped(p_answer, callee->object->type->element, element, position));
	} else if (filter) {
		const uint32_t skip = l_.emit(Op::JumpIfNot, position, answered_true(p_answer), 0);
		l_.emit(Op::VecCall, position, NONE, static_cast<uint32_t>(VectorMethod::Push), l_.arguments({ p_out, p_item }));
		l_.patch(skip, l_.here());
	} else if (some || every) {
		// The first that answers true for some(), false for every(), settles it.
		const uint32_t carry_on = l_.emit(every ? Op::JumpIf : Op::JumpIfNot, position, answered_true(p_answer), 0);
		l_.emit(Op::ConstBool, position, p_out, some ? 1 : 0);
		found = l_.emit(Op::Jump, position, 0);
		l_.patch(carry_on, l_.here());
	}
	return found;
}

// v.forEach(f), map, filter, some and every: a loop that calls the function with each element, its index and
// the vector, as Flash's own Vector does (avmplus): the length read once, before the first call, and each
// element read as it is reached (one taken out by then is AS3's RangeError). Only an answer of true counts
// for filter, some and every. No function at all is no call: a Vector's map gives a vector of empty
// elements, an Array's an empty one (it pushes each answer), and filter an empty one. The object to call it on is checked (a method with one is a TypeError, but for map) and
// set aside: a method keeps its own object, and a function expression can't use `this`.
uint32_t Vectors::vector_each(Call *p_call) {
	const uint32_t position = p_call->offset;
	Member *callee = static_cast<Member *>(p_call->callee);
	const std::string_view name = callee->name;
	const uint32_t count = p_call->arguments.count;
	const std::optional<uint32_t> klass = l_.vector_class(callee->object->type);
	if (!klass || count < 1 || count > 2) {
		l_.refuse(callee->name_offset, "this call of '" + std::string(name) + "' on a Vector");
		return l_.default_value(Kind::Obj, position);
	}
	const bool map = name == "map", filter = name == "filter", some = name == "some", every = name == "every";
	const Kind element = l_.module_.classes[*klass].element;
	const uint32_t vector = l_.settled(l_.expression(callee->object, Kind::Obj), Lowerer::writer(p_call->arguments), position);
	Expr *object = count > 1 ? p_call->arguments[1] : nullptr;
	const uint32_t function = l_.settled(l_.expression(p_call->arguments[0], Kind::Any), object && Lowerer::writes_locals(object) ? object : nullptr, position);
	const uint32_t given = object ? l_.expression(object, Kind::Any) : NONE;
	l_.emit(Op::NullCheck, position, vector);
	if (given != NONE && !map) {
		l_.emit(Op::CallbackThis, position, function, given);
	}
	Functions(l_).use_argument_error();
	const uint32_t limit = l_.reg(Kind::U32);
	l_.emit(Op::VecLength, position, limit, vector);
	uint32_t out = each_result(name, *klass, limit, position);
	const uint32_t absent = l_.emit(Op::JumpIfNot, position, l_.convert(function, Kind::Bool, position), 0);
	uint32_t yes = NONE;
	if (filter || some || every) {
		const uint32_t truth = l_.reg(Kind::Bool);
		l_.emit(Op::ConstBool, position, truth, 1);
		yes = l_.convert(truth, Kind::Any, position);
	}
	const uint32_t end = l_.convert(limit, Kind::F64, position);
	const uint32_t at = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, position, at, l_.number(0));
	const uint32_t top = l_.here();
	const uint32_t more = l_.reg(Kind::Bool);
	l_.emit(Op::LtF64, position, more, at, end);
	const uint32_t exit = l_.emit(Op::JumpIfNot, position, more, 0);
	const uint32_t item = l_.reg(element);
	l_.emit(Op::VecGet, position, item, vector, at);
	const uint32_t index = l_.convert(l_.convert(at, Kind::I32, position), Kind::Any, position);
	const uint32_t answer = map || filter || some || every ? l_.reg(Kind::Any) : NONE;
	l_.emit(Op::CallFunction, position, answer, function, l_.arguments({ l_.convert(item, Kind::Any, position), index, l_.convert(vector, Kind::Any, position) }));
	const uint32_t found = each_answer(p_call, *klass, out, at, item, answer, yes);
	const uint32_t one = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, position, one, l_.number(1));
	l_.emit(Op::AddF64, position, at, at, one);
	l_.emit(Op::Jump, position, top);
	l_.patch(exit, l_.here());
	l_.patch(absent, l_.here());
	if (found != NONE) {
		l_.patch(found, l_.here());
	}
	return out == NONE ? 0 : out;
}

// f.call(thisArg, a, b) and f.apply(thisArg, [a, b]) on a Function: thisArg is worked out and set aside, as
// vector_each() does with the object to call a callback on: a method keeps its own object, and a function
// expression can't use `this`. Nothing if the call is not one of these.
std::optional<uint32_t> Functions::function_method(Call *p_call) {
	Member *member = p_call->callee->kind == NodeKind::Member ? static_cast<Member *>(p_call->callee) : nullptr;
	Symbol *symbol = member ? member->resolved() : nullptr;
	if (!symbol || !l_.function_ || symbol->owner != l_.function_ || (symbol->name != "call" && symbol->name != "apply")) {
		return std::nullopt;
	}
	const uint32_t position = p_call->offset;
	const List<Expr *> &given = p_call->arguments;
	const uint32_t value = l_.settled(l_.expression(member->object, Kind::Obj), Lowerer::writer(given), position);
	if (given.count > 0) {
		Expr *later = Lowerer::writer(given, 1);
		l_.settled(l_.expression(given[0], Kind::Void), later, position);
	}
	use_argument_error();
	const uint32_t out = l_.reg(Kind::Any);
	if (symbol->name == "apply") {
		uint32_t array = NONE;
		if (given.count > 1) {
			array = l_.any_value(given[1]);
		} else {
			array = l_.reg(Kind::Any);
			l_.emit(Op::AnyConst, position, array, 1);
		}
		l_.emit(Op::ApplyFunction, position, out, value, array);
		return out;
	}
	std::vector<uint32_t> list;
	for (uint32_t i = 1; i < given.count; i++) {
		Expr *later = Lowerer::writer(given, i + 1);
		list.push_back(l_.settled(l_.any_value(given[i]), later, position));
	}
	l_.emit(Op::CallFunction, position, out, value, l_.arguments(std::move(list)));
	return out;
}

} // namespace ir
} // namespace kas3
