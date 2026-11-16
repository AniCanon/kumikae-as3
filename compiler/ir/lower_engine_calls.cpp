#include "lower_dynamic.h"
#include "lower_engine.h"
#include "lower_engine_calls.h"
#include "lower_vectors.h"

namespace kas3 {
namespace ir {

// `timer[name].add(f)`: the checker can't know what `timer[name]` is. A signal's method written after
// it says a signal is meant; the engine checks that it is one when the line runs. Nothing if the call is
// not of that shape.
std::optional<uint32_t> EngineCalls::signal_call_by_name(Call *p_call) {
	if (p_call->callee->kind != NodeKind::Member || !l_.signal_ || !l_.signal_->klass) {
		return std::nullopt;
	}
	Member *member = static_cast<Member *>(p_call->callee);
	auto verb = l_.signal_->klass->members.find(member->name);
	if (verb == l_.signal_->klass->members.end() || verb->second->kind != SymbolKind::Method) {
		return std::nullopt;
	}
	uint32_t object = 0, name = 0;
	uint32_t found = signal_by_name(member->object, Lowerer::writer(p_call->arguments), &object, &name);
	if (found == NONE) {
		return std::nullopt;
	}
	return engine_call(p_call, verb->second, found, Later{ Op::HostMember, member->object->offset, found, object, name });
}

// AS3's own toString() on one of Godot's values or objects: Godot's text for it.
uint32_t EngineCalls::engine_text(Call *p_call) {
	uint32_t value = l_.expression(static_cast<Member *>(p_call->callee)->object, Kind::Void);
	l_.emit(Op::NullCheck, p_call->offset, value);
	uint32_t out = l_.reg(Kind::Str);
	l_.emit(Op::HostToStr, p_call->offset, out, value);
	return out;
}

// A method of Godot's. `Input.isActionPressed(...)` is a method of the engine's one Input; any other
// static one is called on no object.
uint32_t EngineCalls::godot_call(Call *p_call, Symbol *p_method) {
	uint32_t position = p_call->offset;
	if (p_method->is_static()) {
		if (Engine::is_singleton(p_method->owner) && Engine::on_object(p_call->callee)) {
			return engine_call(p_call, p_method); // `i.isActionPressed()`: on the object `i` holds.
		}
		return Engine::is_singleton(p_method->owner) ? engine_call(p_call, p_method, Engine(l_).singleton(p_method->owner, position)) : static_call(p_call, p_method);
	}
	Expr *callee = p_call->callee;
	bool through_super = callee->kind == NodeKind::Member && static_cast<Member *>(callee)->object->kind == NodeKind::SuperExpr;
	if (!through_super || (p_method->modifiers & Modifiers::Final)) {
		return engine_call(p_call, p_method);
	}
	// Godot's own version of a virtual does nothing. Asking the engine for it would come straight back
	// to the script. The arguments are still evaluated.
	for (Expr *argument : p_call->arguments) {
		l_.expression(argument, Kind::Void);
	}
	Kind kind = Kind::Void;
	if (p_method->type) {
		l_.kind_of(l_.compilation_.type_of(p_method->type), &kind);
	}
	return kind == Kind::Void ? 0 : l_.default_value(kind, position);
}

// One of Godot's global functions: `lerpf(a, b, t)`, `print(...)`. The call carries the function's
// signature, which the engine's entry for such functions needs.
uint32_t EngineCalls::global_call(Call *p_call, Symbol *p_function, Kind p_want) {
	uint32_t position = p_call->offset;
	std::string signature;
	if (!godot_name("@global", p_function->name, &signature)) {
		l_.refuse(p_call->callee->offset, "calling '" + std::string(p_function->name) + "'");
		return l_.default_value(p_want, position);
	}
	const Type *type = p_function->type ? l_.compilation_.type_of(p_function->type) : nullptr;
	Kind kind = Kind::Void;
	if (type && type->kind == Type::Any) {
		// It gives back anything. Where the program says it expects a number or text, that is asked for and
		// checked as it arrives: Godot answers a wrong argument with something rather than nothing, so this
		// is where it is caught. An int or a uint is asked for as a Number and then cut, as AS3 does
		// (`int(lerp(a, b, t))`). Anywhere else, an untyped value, converted where it is used.
		kind = p_want == Kind::I32 || p_want == Kind::U32 ? Kind::F64 : Lowerer::is_numeric(p_want) ? p_want : p_want == Kind::Str ? Kind::Val : Kind::Any;
		type = nullptr;
	} else if (type && !l_.kind_of(type, &kind)) {
		l_.refuse(position, "a call to a Godot function that returns '" + l_.compilation_.type_name(type) + "'");
	}
	std::vector<uint32_t> list;
	FunctionDecl *decl = p_function->node && p_function->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_function->node) : nullptr;
	engine_arguments(decl, p_call->arguments, position, list);
	uint32_t out = kind == Kind::Void ? NONE : l_.engine_reg(kind, type);
	// No class before the dot: the engine's side reads that as a global function.
	l_.emit(Op::HostCallStatic, position, out, l_.string("." + signature), l_.arguments(std::move(list)));
	if (out != NONE && (p_want == Kind::Str || p_want == Kind::I32 || p_want == Kind::U32) && kind != p_want) {
		return l_.convert(out, p_want, position); // Godot's text for it; a Number cut to a whole one.
	}
	return out == NONE ? 0 : out;
}

uint32_t EngineCalls::bound_method(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	if (Lowerer::is_null(p_expression)) {
		return l_.default_value(Kind::Val, position);
	}
	Symbol *symbol = l_.callee_symbol(p_expression);
	auto function = l_.functions_.find(declaration(symbol));
	if (!symbol || symbol->kind != SymbolKind::Method || symbol->is_static() || function == l_.functions_.end()) {
		// Any other function value (a function expression, a static method, one held in a variable): a
		// Godot Callable that calls it.
		const uint32_t value = l_.expression(p_expression, Kind::Obj);
		uint32_t out = l_.reg(Kind::Val);
		l_.emit(Op::FunctionToCallable, position, out, value);
		return out;
	}
	uint32_t object = 0;
	bool on_this = true;
	if (p_expression->kind == NodeKind::Member) {
		Expr *written = static_cast<Member *>(p_expression)->object;
		if (written->kind == NodeKind::SuperExpr) {
			l_.refuse(position, "a method of 'super' used as a value");
			return l_.default_value(Kind::Val, position);
		}
		if (written->kind != NodeKind::ThisExpr) {
			on_this = false;
			object = l_.receiver(written);
			l_.emit(Op::NullCheck, position, object);
		}
	}
	if (on_this && l_.fn().is_static) {
		l_.refuse(position, "this name");
		return l_.default_value(Kind::Val, position);
	}
	if (on_this) {
		object = l_.self(position);
	}
	bool exact = (symbol->modifiers & Modifiers::Private) != 0;
	uint32_t out = l_.reg(Kind::Val);
	l_.emit(Op::HostBind, position, out, object, exact ? function->second : (l_.slots_[function->first] | ir::BIND_SLOT));
	return out;
}

uint32_t EngineCalls::signal_by_name(Expr *p_expression, Expr *p_later, uint32_t *r_object, uint32_t *r_name) {
	if (p_expression->kind != NodeKind::Index) {
		return NONE;
	}
	Index *index = static_cast<Index *>(p_expression);
	Kind object = Kind::Void;
	if (!l_.kind_of(index->object->type, &object) || !l_.is_string_type(index->index->type)) {
		return NONE;
	}
	// A Godot object, or a script object, which stands for the Godot object it is attached to.
	uint32_t klass = 0;
	const Type *type = index->object->type;
	bool script = object == Kind::Obj && type && type->kind == Type::Instance && l_.class_index(type->symbol, &klass) &&
			!l_.module_.classes[klass].native_base.empty();
	if (object != Kind::Ref && !script) {
		return NONE;
	}
	// A script object goes as itself: the name may be one of its fields that holds a signal, one the code
	// here may see (the interpreter decides, by the class the code is written in). Else its Godot object's.
	bool of_value = false;
	const bool own = script && index->object->kind == NodeKind::ThisExpr && !l_.fn().is_static;
	uint32_t object_reg = own ? l_.self(p_expression->offset, true)
			: script && index->object->kind != NodeKind::ThisExpr ? l_.expression(index->object, Kind::Obj)
			: Engine(l_).engine_receiver(index->object, p_expression->offset, &of_value);
	*r_object = l_.settled(object_reg, index->index, p_expression->offset);
	*r_object = l_.settled(*r_object, p_later, p_expression->offset);
	*r_name = l_.settled(l_.expression(index->index, Kind::Str), p_later, p_expression->offset);
	uint32_t out = l_.reg(Kind::Val);
	if (l_.fn().engine_types.size() <= out) {
		l_.fn().engine_types.resize(out + 1);
	}
	l_.fn().engine_types[out] = "Signal";
	return out;
}

void EngineCalls::engine_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list,
		const std::string &p_defaults) {
	uint32_t count = p_function ? p_function->parameters.count : 0;
	// A default that AS3 can't write is declared `= null`, and Godot fills in the real one when the
	// argument is left out. A `null` written last for such a parameter is the same as leaving it out.
	uint32_t written = p_arguments.count;
	while (written > 0 && written <= count && Lowerer::is_null(p_arguments[written - 1]) && p_function->parameters[written - 1].default_value &&
			Lowerer::is_null(p_function->parameters[written - 1].default_value)) {
		written--;
	}
	for (uint32_t i = 0; i < written; i++) {
		Kind kind = Kind::Void;
		const Type *type = nullptr;
		if (i < count && !p_function->parameters[i].is_rest && p_function->parameters[i].type) {
			type = l_.compilation_.type_of(p_function->parameters[i].type);
			if (type && type->kind == Type::Instance && type->symbol == l_.function_ && l_.function_) {
				// Godot takes a function here: a method goes bound to its object.
				Expr *later = Lowerer::writer(p_arguments, i + 1, written);
				r_list.push_back(l_.settled(bound_method(p_arguments[i]), later, p_position));
				continue;
			}
			std::string godot_default;
			if (!p_defaults.empty() && p_function->parameters[i].default_value && Lowerer::is_null(p_function->parameters[i].default_value) &&
					l_.kind_of(type, &kind) && kind == Kind::Val && godot_name("@default", p_defaults + "." + std::to_string(i), &godot_default)) {
				// `null` where the default is one AS3 can't write, with arguments after it: Godot's own
				// default, from Godot's text for it. Written `null`, it is known now; a value that may be
				// null (`var c:Color = null`) is looked at as the call is made.
				uint32_t value = l_.engine_reg(Kind::Val, type);
				if (!Lowerer::is_null(p_arguments[i])) {
					// Into a register of its own: what comes later can't change it.
					l_.emit(Op::Move, p_arguments[i]->offset, value, l_.expression(p_arguments[i], Kind::Val));
					uint32_t present = l_.reg(Kind::Bool);
					l_.emit(Op::HostPresent, p_arguments[i]->offset, present, value);
					uint32_t given = l_.emit(Op::JumpIf, p_arguments[i]->offset, present, 0);
					l_.emit(Op::HostDefault, p_arguments[i]->offset, value, l_.string(godot_default));
					l_.patch(given, l_.here());
				} else {
					l_.emit(Op::HostDefault, p_arguments[i]->offset, value, l_.string(godot_default));
				}
				r_list.push_back(value);
				continue;
			}
			kind = Kind::Void;
			if (type && type->kind != Type::Any && !l_.kind_of(type, &kind)) {
				l_.refuse(p_arguments[i]->offset, "passing a value of type '" + l_.compilation_.type_name(type) + "' to Godot");
				kind = Kind::Void;
			}
		}
		Expr *later = Lowerer::writer(p_arguments, i + 1);
		if (kind == Kind::Void && Lowerer::is_null(p_arguments[i])) {
			kind = Kind::Ref; // `null` where Godot takes anything: nothing.
		}
		Kind own = Kind::Void;
		if (kind == Kind::Void && l_.kind_of(p_arguments[i]->type, &own) && Lowerer::is_numeric(own) && l_.whole(p_arguments[i])) {
			kind = Kind::I64; // Where Godot takes anything, `-5` and `2 * 3` are its integers, not 5.0 and 6.0.
		}
		std::optional<uint32_t> collection = Vectors(l_).vector_to_engine(p_arguments[i], type);
		collection = collection ? collection : Dynamic(l_).to_engine(p_arguments[i], type);
		uint32_t value = collection ? *collection : l_.expression(p_arguments[i], kind);
		if (l_.kind_at(value) == Kind::Obj) {
			l_.refuse(p_arguments[i]->offset, "passing a script object to Godot");
		}
		r_list.push_back(l_.settled(value, later, p_position));
	}
}

// "Class.method" in Godot's names: where the name table keeps a method's defaults that AS3 can't write.
std::string EngineCalls::defaults_key(const Symbol *p_method) const {
	std::string klass, method;
	if (!p_method->owner || !godot_name(p_method->owner->name, std::string_view(), &klass) ||
			!godot_name(p_method->owner->name, p_method->name, &method)) {
		return std::string();
	}
	return klass + "." + method;
}

uint32_t EngineCalls::engine_call(Call *p_call, Symbol *p_method, uint32_t p_receiver, std::optional<Later> p_before) {
	uint32_t position = p_call->offset;
	uint32_t name = 0;
	if (!l_.engine_name(p_method->owner, p_method->name, &name)) {
		l_.refuse(p_call->callee->offset, "calling '" + std::string(p_method->name) + "'");
		return 0;
	}
	Kind kind = Kind::Void;
	if (p_method->type && !l_.kind_of(l_.compilation_.type_of(p_method->type), &kind)) {
		l_.refuse(position, "a call to a Godot method that returns '" + l_.compilation_.type_name(l_.compilation_.type_of(p_method->type)) + "'");
	}
	bool of_value = false;
	Expr *object = p_call->callee->kind == NodeKind::Member ? static_cast<Member *>(p_call->callee)->object : nullptr;
	std::vector<uint32_t> list;
	uint32_t script_object = NONE; // Set if the member is reached through another script object.
	const uint32_t receiver = p_receiver != NONE ? p_receiver : Engine(l_).engine_receiver(object, position, &of_value, &script_object);
	if (script_object != NONE) {
		script_object = l_.settled(script_object, Lowerer::writer(p_call->arguments), position);
		list.push_back(receiver); // Filled in below, after the arguments.
	} else {
		list.push_back(l_.settled(receiver, Lowerer::writer(p_call->arguments), position));
	}
	FunctionDecl *decl = p_method->node && p_method->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_method->node) : nullptr;
	engine_arguments(decl, p_call->arguments, position, list, defaults_key(p_method));
	if (script_object != NONE) {
		Engine(l_).owner_of(script_object, receiver, object->offset);
	}
	if (p_before) {
		l_.emit(p_before->op, p_before->position, p_before->a, p_before->b, p_before->c); // What the receiver still needs.
	}
	uint32_t out = kind == Kind::Void ? NONE : l_.engine_reg(kind, p_method->type ? l_.compilation_.type_of(p_method->type) : nullptr);
	l_.emit(Op::HostCall, position, out, name, l_.arguments(std::move(list)));
	return out == NONE ? 0 : out;
}

uint32_t EngineCalls::static_call(Call *p_call, Symbol *p_method) {
	uint32_t position = p_call->offset;
	std::string klass, method;
	if (!p_method->owner || !godot_name(p_method->owner->name, std::string_view(), &klass) ||
			!godot_name(p_method->owner->name, p_method->name, &method)) {
		l_.refuse(p_call->callee->offset, "calling '" + std::string(p_method->name) + "'");
		return 0;
	}
	Kind kind = Kind::Void;
	if (p_method->type && !l_.kind_of(l_.compilation_.type_of(p_method->type), &kind)) {
		l_.refuse(position, "a call to a Godot method that returns '" + l_.compilation_.type_name(l_.compilation_.type_of(p_method->type)) + "'");
	}
	std::vector<uint32_t> list;
	FunctionDecl *decl = p_method->node && p_method->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_method->node) : nullptr;
	engine_arguments(decl, p_call->arguments, position, list, klass + "." + method);
	uint32_t out = kind == Kind::Void ? NONE : l_.engine_reg(kind, p_method->type ? l_.compilation_.type_of(p_method->type) : nullptr);
	l_.emit(Op::HostCallStatic, position, out, l_.string(klass + "." + method), l_.arguments(std::move(list)));
	return out == NONE ? 0 : out;
}

} // namespace ir
} // namespace kas3
