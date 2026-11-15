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
		object = receiver(written);
	} else if (fn().is_static) {
		refuse(position, "this name");
		return default_value(Kind::Obj, position);
	} else {
		object = self(position, false);
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

// A call of a function value may raise AS3's ArgumentError: the class it is of.
void Lowerer::use_argument_error() {
	if (module_.argument_error_class == NONE) {
		if (Symbol *argument_error = compilation_.builtin("ArgumentError")) {
			class_index(argument_error, &module_.argument_error_class);
		}
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
	use_argument_error();
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

// v.forEach(f), map, filter, some and every: a loop that calls the function with each element, its index and
// the vector, as Flash's own Vector does (avmplus): the length read once, before the first call, and each
// element read as it is reached (one taken out by then is AS3's RangeError). Only an answer of true counts
// for filter, some and every. No function at all is no call: a Vector's map gives a vector of empty
// elements, an Array's an empty one (it pushes each answer), and filter an empty one. The object to call it on is checked (a method with one is a TypeError, but for map) and
// set aside: a method keeps its own object, and a function expression can't use `this`.
uint32_t Lowerer::vector_each(Call *p_call) {
	const uint32_t position = p_call->offset;
	Member *callee = static_cast<Member *>(p_call->callee);
	const std::string_view name = callee->name;
	const uint32_t count = p_call->arguments.count;
	const std::optional<uint32_t> klass = vector_class(callee->object->type);
	if (!klass || count < 1 || count > 2) {
		refuse(callee->name_offset, "this call of '" + std::string(name) + "' on a Vector");
		return default_value(Kind::Obj, position);
	}
	const bool map = name == "map", filter = name == "filter", some = name == "some", every = name == "every";
	const Kind element = module_.classes[*klass].element;
	const uint32_t vector = settled(expression(callee->object, Kind::Obj), writer(p_call->arguments), position);
	Expr *object = count > 1 ? p_call->arguments[1] : nullptr;
	const uint32_t function = settled(expression(p_call->arguments[0], Kind::Any), object && writes_locals(object) ? object : nullptr, position);
	const uint32_t given = object ? expression(object, Kind::Any) : NONE;
	emit(Op::NullCheck, position, vector);
	if (given != NONE && !map) {
		emit(Op::CallbackThis, position, function, given);
	}
	use_argument_error();
	const uint32_t limit = reg(Kind::U32);
	emit(Op::VecLength, position, limit, vector);
	uint32_t out = NONE;
	if (map || filter) {
		out = reg(Kind::Obj);
		const bool sized = map && !module_.classes[*klass].array;
		emit(Op::VecNew, position, out, *klass, arguments({ sized ? limit : default_value(Kind::U32, position), default_value(Kind::Bool, position) }));
	} else if (some || every) {
		out = reg(Kind::Bool);
		emit(Op::ConstBool, position, out, every ? 1 : 0);
	}
	const uint32_t absent = emit(Op::JumpIfNot, position, convert(function, Kind::Bool, position), 0);
	uint32_t yes = NONE;
	if (filter || some || every) {
		const uint32_t truth = reg(Kind::Bool);
		emit(Op::ConstBool, position, truth, 1);
		yes = convert(truth, Kind::Any, position);
	}
	// Whether the function answered true itself, not just something that is true as a condition.
	const auto answered_true = [&](uint32_t p_answer) {
		const uint32_t out = reg(Kind::Bool);
		emit(Op::AnyStrictEq, position, out, p_answer, yes);
		return out;
	};
	const uint32_t end = convert(limit, Kind::F64, position);
	const uint32_t at = reg(Kind::F64);
	emit(Op::ConstF64, position, at, number(0));
	const uint32_t top = here();
	const uint32_t more = reg(Kind::Bool);
	emit(Op::LtF64, position, more, at, end);
	const uint32_t exit = emit(Op::JumpIfNot, position, more, 0);
	const uint32_t item = reg(element);
	emit(Op::VecGet, position, item, vector, at);
	const uint32_t index = convert(convert(at, Kind::I32, position), Kind::Any, position);
	const uint32_t answer = map || filter || some || every ? reg(Kind::Any) : NONE;
	emit(Op::CallFunction, position, answer, function, arguments({ convert(item, Kind::Any, position), index, convert(vector, Kind::Any, position) }));
	uint32_t found = NONE;
	if (map && module_.classes[*klass].array) {
		emit(Op::VecCall, position, NONE, static_cast<uint32_t>(VectorMethod::Push), arguments({ out, answer }));
	} else if (map) {
		emit(Op::VecSet, position, out, at, from_untyped(answer, callee->object->type->element, element, position));
	} else if (filter) {
		const uint32_t skip = emit(Op::JumpIfNot, position, answered_true(answer), 0);
		emit(Op::VecCall, position, NONE, static_cast<uint32_t>(VectorMethod::Push), arguments({ out, item }));
		patch(skip, here());
	} else if (some || every) {
		// The first that answers true for some(), false for every(), settles it.
		const uint32_t carry_on = emit(every ? Op::JumpIf : Op::JumpIfNot, position, answered_true(answer), 0);
		emit(Op::ConstBool, position, out, some ? 1 : 0);
		found = emit(Op::Jump, position, 0);
		patch(carry_on, here());
	}
	const uint32_t one = reg(Kind::F64);
	emit(Op::ConstF64, position, one, number(1));
	emit(Op::AddF64, position, at, at, one);
	emit(Op::Jump, position, top);
	patch(exit, here());
	patch(absent, here());
	if (found != NONE) {
		patch(found, here());
	}
	return out == NONE ? 0 : out;
}

// f.call(thisArg, a, b) and f.apply(thisArg, [a, b]) on a Function: thisArg is worked out and set aside, as
// vector_each() does with the object to call a callback on: a method keeps its own object, and a function
// expression can't use `this`. Nothing if the call is not one of these.
std::optional<uint32_t> Lowerer::function_method(Call *p_call) {
	Member *member = p_call->callee->kind == NodeKind::Member ? static_cast<Member *>(p_call->callee) : nullptr;
	Symbol *symbol = member ? member->resolved() : nullptr;
	if (!symbol || !function_ || symbol->owner != function_ || (symbol->name != "call" && symbol->name != "apply")) {
		return std::nullopt;
	}
	const uint32_t position = p_call->offset;
	const List<Expr *> &given = p_call->arguments;
	const uint32_t value = settled(expression(member->object, Kind::Obj), writer(given), position);
	if (given.count > 0) {
		Expr *later = nullptr;
		for (uint32_t next = 1; next < given.count && !later; next++) {
			later = writes_locals(given[next]) ? given[next] : nullptr;
		}
		settled(expression(given[0], Kind::Void), later, position);
	}
	use_argument_error();
	const uint32_t out = reg(Kind::Any);
	if (symbol->name == "apply") {
		uint32_t array = NONE;
		if (given.count > 1) {
			array = any_value(given[1]);
		} else {
			array = reg(Kind::Any);
			emit(Op::AnyConst, position, array, 1);
		}
		emit(Op::ApplyFunction, position, out, value, array);
		return out;
	}
	std::vector<uint32_t> list;
	for (uint32_t i = 1; i < given.count; i++) {
		Expr *later = nullptr;
		for (uint32_t next = i + 1; next < given.count && !later; next++) {
			later = writes_locals(given[next]) ? given[next] : nullptr;
		}
		list.push_back(settled(any_value(given[i]), later, position));
	}
	emit(Op::CallFunction, position, out, value, arguments(std::move(list)));
	return out;
}

} // namespace ir
} // namespace kas3
