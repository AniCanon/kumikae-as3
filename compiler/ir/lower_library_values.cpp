// The library's own functions as values. `Math.floor`, `parseInt` and the rest are operations, not
// functions, where they are called by name. Read as a value, one is a function of the program that the
// lowerer writes from the library's own declaration of it (builtins/toplevel.as): it takes what the
// declaration says, and its body is the call written out, lowered as that call is anywhere else. So a
// call through the value counts and converts its arguments as a call of one of the program's functions
// does, `call` and `apply` work as they do on any function, and there is no second list of what each one
// does: the interpreter and native code both run what the one lowering of a call gives.
//
// A parameter the caller leaves out is left out of the call, which has its own rule for it: the body is
// the call once for each number of arguments. The three the library declares with `...rest` (Math's max
// and min, trace and String.fromCharCode) take as many as they are given, one after another.
#include "lower_bodies.h"
#include "lower_library_values.h"

namespace kas3 {
namespace ir {

bool LibraryValues::is_function(const Symbol *p_symbol) {
	const bool builtin = p_symbol && p_symbol->unit && p_symbol->unit->is_builtin && declaration(p_symbol);
	return builtin && (p_symbol->kind == SymbolKind::Function || (p_symbol->kind == SymbolKind::Method && p_symbol->is_static()));
}

uint32_t LibraryValues::function_value(Expr *p_expression, Symbol *p_function) {
	const uint32_t position = p_expression->offset;
	const uint32_t out = l_.reg(Kind::Obj);
	uint32_t index = NONE;
	if (const auto known = l_.forwarders_.find(p_function); known != l_.forwarders_.end()) {
		index = known->second;
	} else {
		Function made;
		made.name = (p_function->owner ? l_.class_name(p_function->owner) + "/" : std::string()) + std::string(p_function->name);
		made.file = l_.fn().file;
		made.position = position;
		made.is_static = true;
		l_.module_.functions.push_back(std::move(made));
		index = static_cast<uint32_t>(l_.module_.functions.size()) - 1;
		PendingFunction pending;
		pending.index = index;
		pending.unit = l_.unit_;
		pending.forwarded = p_function;
		pending.written = p_expression;
		l_.pending_functions_.push_back(std::move(pending));
		l_.forwarders_.emplace(p_function, index);
	}
	l_.emit(Op::MakeFunction, position, out, index, NONE);
	return out;
}

void LibraryValues::give(uint32_t p_value, uint32_t p_position) {
	if (l_.fn().result == Kind::Void) {
		l_.emit(Op::ReturnVoid, p_position);
	} else {
		l_.emit(Op::Return, p_position, l_.convert(p_value, l_.fn().result, p_position));
	}
}

void LibraryValues::call_with(const PendingFunction &p_pending, const std::vector<uint32_t> &p_registers, uint32_t p_count, uint32_t p_position) {
	const FunctionDecl *decl = declaration(p_pending.forwarded);
	// The call as it would be written: what was read as a value, called with the parameters, each of
	// which is in its register already.
	std::vector<Name> names(p_count);
	std::vector<Expr *> written(p_count);
	for (uint32_t i = 0; i < p_count; i++) {
		names[i].kind = NodeKind::Name;
		names[i].offset = names[i].end = p_position;
		names[i].text = decl->parameters[i].name;
		names[i].type = decl->parameters[i].type ? l_.compilation_.type_of(decl->parameters[i].type) : l_.compilation_.any_type();
		written[i] = &names[i];
		l_.evaluated_[written[i]] = p_registers[i];
	}
	Call call;
	call.kind = NodeKind::Call;
	call.offset = call.end = p_position;
	call.callee = p_pending.written;
	call.arguments.items = written.data();
	call.arguments.count = p_count;
	call.type = decl->return_type ? l_.compilation_.type_of(decl->return_type) : l_.compilation_.any_type();
	give(l_.expression(&call, l_.fn().result), p_position);
	for (Expr *each : written) {
		l_.evaluated_.erase(each);
	}
}

void LibraryValues::each_rest(uint32_t p_rest, uint32_t p_position, const std::function<void(uint32_t)> &p_body) {
	const uint32_t length = l_.reg(Kind::U32);
	l_.emit(Op::VecLength, p_position, length, p_rest);
	const uint32_t end = l_.convert(length, Kind::F64, p_position);
	const uint32_t at = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, p_position, at, l_.number(0));
	const uint32_t top = l_.here();
	const uint32_t more = l_.reg(Kind::Bool);
	l_.emit(Op::LtF64, p_position, more, at, end);
	const uint32_t exit = l_.emit(Op::JumpIfNot, p_position, more, 0);
	const uint32_t item = l_.reg(Kind::Any);
	l_.emit(Op::VecGet, p_position, item, p_rest, at);
	p_body(item);
	const uint32_t one = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, p_position, one, l_.number(1));
	l_.emit(Op::AddF64, p_position, at, at, one);
	l_.emit(Op::Jump, p_position, top);
	l_.patch(exit, l_.here());
}

// Math.max and Math.min: the two declared, each the other end of the numbers when it is left out (as the
// declaration has it), and then each of the rest, as a call with that many arguments compares them.
void LibraryValues::extreme(const PendingFunction &p_pending, const std::vector<uint32_t> &p_registers, uint32_t p_position) {
	const bool max = p_pending.forwarded->name == "max";
	for (uint32_t i = 0; i < 2; i++) {
		const uint32_t missing = l_.reg(Kind::Bool);
		l_.emit(Op::ArgMissing, p_position, missing, p_registers[i]);
		const uint32_t given = l_.emit(Op::JumpIfNot, p_position, missing, 0);
		l_.emit(Op::ConstF64, p_position, p_registers[i], l_.number(max ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity()));
		l_.patch(given, l_.here());
	}
	const uint32_t best = l_.reg(Kind::F64);
	l_.emit(max ? Op::MaxF64 : Op::MinF64, p_position, best, p_registers[0], p_registers[1]);
	each_rest(p_registers[2], p_position, [&](uint32_t p_item) {
		l_.emit(max ? Op::MaxF64 : Op::MinF64, p_position, best, best, l_.convert(p_item, Kind::F64, p_position));
	});
	give(best, p_position);
}

// trace(...): each value as String(x) gives it, with a space between two.
void LibraryValues::traced(uint32_t p_rest, uint32_t p_position) {
	const uint32_t line = l_.reg(Kind::Str), space = l_.reg(Kind::Str), first = l_.reg(Kind::Bool);
	l_.emit(Op::ConstStr, p_position, line, l_.string(""));
	l_.emit(Op::ConstStr, p_position, space, l_.string(" "));
	l_.emit(Op::ConstBool, p_position, first, 1);
	each_rest(p_rest, p_position, [&](uint32_t p_item) {
		const uint32_t leads = l_.emit(Op::JumpIf, p_position, first, 0);
		l_.emit(Op::Concat, p_position, line, line, space);
		l_.patch(leads, l_.here());
		l_.emit(Op::ConstBool, p_position, first, 0);
		const uint32_t words = l_.reg(Kind::Str);
		l_.emit(Op::AnyText, p_position, words, p_item);
		l_.emit(Op::Concat, p_position, line, line, words);
	});
	l_.emit(Op::Trace, p_position, l_.arguments({ line }));
	give(NONE, p_position);
}

// String.fromCharCode(...): the character of each code, joined.
void LibraryValues::characters(uint32_t p_rest, uint32_t p_position) {
	const uint32_t text = l_.reg(Kind::Str);
	l_.emit(Op::ConstStr, p_position, text, l_.string(""));
	each_rest(p_rest, p_position, [&](uint32_t p_item) {
		const uint32_t one = l_.reg(Kind::Str);
		l_.emit(Op::StrCall, p_position, one, static_cast<uint32_t>(StringFunction::FromCharCode), l_.arguments({ l_.convert(p_item, Kind::F64, p_position) }));
		l_.emit(Op::Concat, p_position, text, text, one);
	});
	give(text, p_position);
}

void LibraryValues::lower(const PendingFunction &p_pending) {
	const Symbol *function = p_pending.forwarded;
	FunctionDecl *decl = declaration(function);
	const uint32_t position = l_.fn().position;
	std::vector<uint32_t> registers;
	uint32_t least = decl->parameters.count;
	for (uint32_t i = 0; i < decl->parameters.count; i++) {
		const Parameter &parameter = decl->parameters[i];
		Kind kind = parameter.is_rest ? Kind::Obj : Kind::Any;
		if (!parameter.is_rest && parameter.type && !l_.kind_of(l_.compilation_.type_of(parameter.type), &kind)) {
			l_.refuse(position, "'" + std::string(function->name) + "' used as a value");
		}
		if (parameter.is_rest) {
			l_.fn().rest = true;
			l_.vector_class(l_.compilation_.instance_type(l_.array_)); // The Array it is made as.
		}
		registers.push_back(Bodies(l_).parameter_register(parameter, kind));
		l_.fn().locals.push_back({ std::string(parameter.name), registers.back() });
		least = (parameter.default_value || parameter.is_rest) && least == decl->parameters.count ? i : least;
	}
	l_.fn().parameters = static_cast<uint32_t>(registers.size());
	l_.fn().required = least;
	l_.fn().result = Kind::Any;
	if (decl->return_type && !l_.kind_of(l_.compilation_.type_of(decl->return_type), &l_.fn().result)) {
		l_.refuse(position, "'" + std::string(function->name) + "' used as a value");
	}
	if (l_.fn().rest) {
		const std::string_view name = function->name;
		if (function->owner == l_.math_ && (name == "max" || name == "min") && registers.size() == 3) {
			extreme(p_pending, registers, position);
		} else if (!function->owner && name == "trace" && registers.size() == 1) {
			traced(registers[0], position);
		} else if (function->owner == l_.string_ && name == "fromCharCode" && registers.size() == 1) {
			characters(registers[0], position);
		} else {
			l_.refuse(position, "'" + std::string(name) + "' used as a value");
			give(l_.default_value(l_.fn().result == Kind::Void ? Kind::Any : l_.fn().result, position), position);
		}
		return;
	}
	for (uint32_t count = static_cast<uint32_t>(registers.size());; count--) {
		uint32_t fewer = NONE;
		if (count > least) {
			const uint32_t missing = l_.reg(Kind::Bool);
			l_.emit(Op::ArgMissing, position, missing, registers[count - 1]);
			fewer = l_.emit(Op::JumpIf, position, missing, 0);
		}
		call_with(p_pending, registers, count, position);
		if (count == least) {
			break;
		}
		l_.patch(fewer, l_.here());
	}
}

} // namespace ir
} // namespace kas3
