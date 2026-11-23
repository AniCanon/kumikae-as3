// Godot's methods and functions as values. A method read from an object where Godot takes a function is
// Godot's own Callable of the two (HostBindMethod). Anywhere else it is a Function value like any other:
// the function is a forwarder, a function of the program that takes what Godot's method declares and
// calls it, so that a call through the value counts and converts its arguments as a call of one of the
// program's functions does, and a typed callback checks its signature. A method of an object or of a
// value is bound to an Array of what it is read from, its AS3 name and Godot's name for it (the pair a
// method of the library read by name is bound to, with Godot's name after it): the interpreter finds the
// same method of the same object equal by it, and hands Godot its own Callable of the two.
#include "lower_engine.h"
#include "lower_engine_values.h"

namespace kas3 {
namespace ir {

namespace {

// Whether a parameter is declared `= null`: for one of Godot's values that is the default AS3 can't write.
bool null_default(const Parameter &p_parameter) {
	return p_parameter.default_value && p_parameter.default_value->kind == NodeKind::NullLit;
}

} // namespace

bool EngineValues::forwards(const Symbol *p_method) const {
	const FunctionDecl *decl = declaration(p_method);
	if (!decl) {
		return false;
	}
	Kind kind = Kind::Void;
	if (p_method->type && !l_.kind_of(l_.compilation_.type_of(p_method->type), &kind)) {
		return false;
	}
	for (const Parameter &parameter : decl->parameters) {
		const Type *type = parameter.type ? l_.compilation_.type_of(parameter.type) : nullptr;
		kind = Kind::Any;
		if (parameter.is_rest || (type && !l_.kind_of(type, &kind))) {
			return false;
		}
		// The one kind of object Godot takes from a program: a function.
		if (kind == Kind::Obj && !(type->kind == Type::Instance && type->symbol == l_.function_)) {
			return false;
		}
	}
	return true;
}

uint32_t EngineValues::forwarder(Symbol *p_method, uint32_t p_position) {
	if (const auto known = l_.forwarders_.find(p_method); known != l_.forwarders_.end()) {
		return known->second;
	}
	Function made;
	// In the package Godot's API is declared in, whatever the program names its own functions.
	made.name = (p_method->owner ? l_.class_name(p_method->owner) + "/" : std::string("godot.")) + std::string(p_method->name);
	made.file = l_.fn().file;
	made.position = p_position;
	made.is_static = p_method->is_static() || p_method->kind != SymbolKind::Method;
	l_.module_.functions.push_back(std::move(made));
	const uint32_t index = static_cast<uint32_t>(l_.module_.functions.size()) - 1;
	PendingFunction pending;
	pending.index = index;
	pending.unit = l_.unit_;
	pending.forwarded = p_method;
	l_.pending_functions_.push_back(std::move(pending));
	l_.forwarders_.emplace(p_method, index);
	return index;
}

uint32_t EngineValues::holder(Expr *p_expression, uint32_t p_position) {
	Expr *written = p_expression->kind == NodeKind::Member ? static_cast<Member *>(p_expression)->object : nullptr;
	if (written && written->kind == NodeKind::SuperExpr) {
		l_.refuse(p_position, "a method of 'super' used as a value");
		return NONE;
	}
	bool of_value = false;
	const uint32_t object = Engine(l_).engine_receiver(written, p_position, &of_value);
	l_.emit(Op::NullCheck, p_position, object);
	return object;
}

std::optional<uint32_t> EngineValues::callable(Expr *p_expression, Symbol *p_method) {
	// A static method of a singleton's class is a method of the engine's one object of it.
	const bool shared = p_method->is_static() && Engine::is_singleton(p_method->owner);
	uint32_t name = 0;
	if (p_method->kind != SymbolKind::Method || (p_method->is_static() && !shared) || !l_.is_godot_object(p_method->owner) ||
			!l_.engine_name(p_method->owner, p_method->name, &name)) {
		return std::nullopt;
	}
	const uint32_t position = p_expression->offset;
	const uint32_t object = shared ? Engine(l_).singleton(p_method->owner, position) : holder(p_expression, position);
	const uint32_t out = l_.engine_reg(Kind::Val, "Callable");
	if (object != NONE) {
		l_.emit(Op::HostBindMethod, position, out, object, name);
	}
	return out;
}

uint32_t EngineValues::function_value(Expr *p_expression, Symbol *p_method) {
	const uint32_t position = p_expression->offset;
	uint32_t klass = NONE, name = 0;
	const bool global = p_method->kind != SymbolKind::Method;
	const std::optional<uint32_t> entry = global ? l_.engine_entry("@global." + std::string(p_method->name)) : std::nullopt;
	const std::optional<uint32_t> array = l_.vector_class(l_.compilation_.instance_type(l_.array_));
	if (!forwards(p_method) || !array || !l_.class_index(l_.function_, &klass) ||
			(global ? !entry : !l_.engine_name(p_method->owner, p_method->name, &name))) {
		l_.refuse(position, "'" + std::string(p_method->name) + "' of Godot's used as a value");
		return l_.default_value(Kind::Obj, position);
	}
	const bool on_object = !global && !p_method->is_static();
	const uint32_t out = l_.reg(Kind::Obj);
	if (!on_object) {
		l_.emit(Op::MakeFunction, position, out, forwarder(p_method, position), NONE);
		return out;
	}
	const uint32_t object = holder(p_expression, position);
	if (object == NONE) {
		return l_.default_value(Kind::Obj, position);
	}
	std::vector<uint32_t> bound = { l_.convert(object, Kind::Any, position) };
	for (const std::string &text : { std::string(p_method->name), l_.module_.engine_names[name].name }) {
		const uint32_t constant = l_.reg(Kind::Str);
		l_.emit(Op::ConstStr, position, constant, l_.string(text));
		bound.push_back(l_.convert(constant, Kind::Any, position));
	}
	const uint32_t pair = l_.reg(Kind::Obj);
	l_.emit(Op::ArrayNew, position, pair, *array, l_.arguments(std::move(bound)));
	l_.emit(Op::MakeFunction, position, out, forwarder(p_method, position), pair);
	return out;
}

// One parameter of a forwarder as Godot is handed it. A function goes as a Callable. One of Godot's values
// declared `= null` takes the default Godot's description gives when it holds nothing, as it does where
// the method is called by name.
EngineValues::Taken EngineValues::parameter(const Parameter &p_parameter, uint32_t p_register, const std::string &p_defaults, uint32_t p_index,
		uint32_t p_position) {
	Taken taken;
	taken.value = p_register;
	const Kind kind = l_.kind_at(p_register);
	if (kind == Kind::Obj) {
		taken.value = l_.engine_reg(Kind::Val, "Callable");
		l_.emit(Op::FunctionToCallable, p_position, taken.value, p_register);
	}
	if (!null_default(p_parameter) || !(is_engine(kind) || kind == Kind::Obj)) {
		return taken;
	}
	const std::optional<uint32_t> godot_default =
			kind == Kind::Val && !p_defaults.empty() ? l_.engine_entry("@default." + p_defaults + "." + std::to_string(p_index)) : std::nullopt;
	if (!godot_default) {
		taken.trimmed = true;
		return taken;
	}
	const uint32_t present = l_.reg(Kind::Bool);
	l_.emit(Op::HostPresent, p_position, present, p_register);
	const uint32_t given = l_.emit(Op::JumpIf, p_position, present, 0);
	l_.emit(Op::HostDefault, p_position, p_register, *godot_default);
	l_.patch(given, l_.here());
	return taken;
}

// The call itself, once for each number of arguments Godot may be handed: the parameters the caller left
// out are left out of the call, so that Godot fills in its own defaults, as it does where the method is
// called by name with fewer arguments.
void EngineValues::call(const PendingFunction &p_pending, uint32_t p_receiver, const std::vector<Taken> &p_taken, uint32_t p_least, uint32_t p_position) {
	Symbol *method = p_pending.forwarded;
	const bool global = method->kind != SymbolKind::Method;
	const Type *type = method->type ? l_.compilation_.type_of(method->type) : nullptr;
	Kind kind = Kind::Void;
	if (type) {
		l_.kind_of(type, &kind);
	}
	uint32_t name = 0;
	if (global) {
		name = *l_.engine_entry("@global." + std::string(method->name));
	} else {
		l_.engine_name(method->owner, method->name, &name);
	}
	const uint32_t first = l_.fn().is_static ? 0 : 1;
	const uint32_t out = kind == Kind::Void ? NONE : l_.engine_reg(kind, type);
	std::vector<uint32_t> ends;
	for (uint32_t count = static_cast<uint32_t>(p_taken.size());; count--) {
		uint32_t fewer = NONE;
		if (count > p_least) {
			const Taken &last = p_taken[count - 1];
			uint32_t left_out = l_.reg(Kind::Bool);
			if (last.trimmed) {
				const uint32_t present = l_.reg(Kind::Bool);
				l_.emit(Op::HostPresent, p_position, present, last.value);
				l_.emit(Op::Not, p_position, left_out, present);
			} else {
				l_.emit(Op::ArgMissing, p_position, left_out, first + count - 1);
			}
			fewer = l_.emit(Op::JumpIf, p_position, left_out, 0);
		}
		std::vector<uint32_t> list;
		if (p_receiver != NONE) {
			list.push_back(p_receiver);
		}
		for (uint32_t i = 0; i < count; i++) {
			list.push_back(p_taken[i].value);
		}
		l_.emit(p_receiver != NONE ? Op::HostCall : Op::HostCallStatic, p_position, out, name, l_.arguments(std::move(list)));
		if (count == p_least) {
			break;
		}
		ends.push_back(l_.emit(Op::Jump, p_position, 0));
		l_.patch(fewer, l_.here());
	}
	for (const uint32_t end : ends) {
		l_.patch(end, l_.here());
	}
	l_.fn().result = kind;
	if (out == NONE) {
		l_.emit(Op::ReturnVoid, p_position);
	} else {
		l_.emit(Op::Return, p_position, out);
	}
}

void EngineValues::lower(const PendingFunction &p_pending) {
	Symbol *method = p_pending.forwarded;
	const FunctionDecl *decl = declaration(method);
	const uint32_t position = l_.fn().position;
	const bool bound = !l_.fn().is_static;
	if (bound) {
		l_.fn().registers.push_back(Kind::Obj);
		l_.fn().locals.push_back({ "$bound", 0 });
		l_.fn().parameter_classes.push_back(*l_.vector_class(l_.compilation_.instance_type(l_.array_)));
	}
	uint32_t least = decl->parameters.count;
	for (uint32_t i = 0; i < decl->parameters.count; i++) {
		const Parameter &parameter = decl->parameters[i];
		const Type *type = parameter.type ? l_.compilation_.type_of(parameter.type) : nullptr;
		Kind kind = Kind::Any;
		if (type) {
			l_.kind_of(type, &kind);
		}
		const uint32_t local = l_.engine_reg(kind, type);
		if (kind == Kind::Obj) {
			l_.fn().parameter_classes.resize(local + 1, NONE);
			l_.fn().parameter_classes[local] = l_.declared_class(parameter.type);
		}
		l_.fn().locals.push_back({ std::string(parameter.name), local });
		least = parameter.default_value && least == decl->parameters.count ? i : least;
	}
	const uint32_t first = bound ? 1 : 0;
	l_.fn().parameters = static_cast<uint32_t>(l_.fn().registers.size());
	l_.fn().required = first + least;
	uint32_t receiver = NONE;
	if (bound) {
		// What the method was read from: the first of the Array the function is bound to.
		const uint32_t held = l_.reg(Kind::Any);
		l_.emit(Op::VecGet, position, held, 0, l_.default_value(Kind::I32, position));
		Kind kind = Kind::Ref;
		const Type *owner = l_.compilation_.instance_type(method->owner);
		l_.kind_of(owner, &kind);
		receiver = l_.from_untyped(held, owner, kind, position);
	} else if (method->kind == SymbolKind::Method && Engine::is_singleton(method->owner)) {
		receiver = Engine(l_).singleton(method->owner, position);
	}
	std::string defaults;
	if (method->kind == SymbolKind::Method) {
		const std::optional<std::string> klass = godot_name(method->owner->name, std::string_view());
		const std::optional<std::string> name = klass ? godot_name(method->owner->name, method->name) : std::nullopt;
		defaults = name ? *klass + "." + *name : std::string();
	}
	std::vector<Taken> taken;
	for (uint32_t i = 0; i < decl->parameters.count; i++) {
		taken.push_back(parameter(decl->parameters[i], first + i, defaults, i, position));
	}
	call(p_pending, receiver, taken, least, position);
}

} // namespace ir
} // namespace kas3
