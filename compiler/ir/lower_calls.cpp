#include "lower_dynamic.h"
#include "lower_members.h"

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
				Expr *later = nullptr;
				for (uint32_t next = at + 1; next < p_arguments.count && !later; next++) {
					later = writes_locals(p_arguments[next]) ? p_arguments[next] : nullptr;
				}
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
		Expr *later = nullptr;
		for (uint32_t next = i + 1; next < p_arguments.count && !later; next++) {
			later = writes_locals(p_arguments[next]) ? p_arguments[next] : nullptr;
		}
		r_list.push_back(settled(typed(p_arguments[i], type, kind), later, p_position));
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

// `timer[name].add(f)`: the checker can't know what `timer[name]` is. A signal's method written after
// it says a signal is meant; the engine checks that it is one when the line runs. Nothing if the call is
// not of that shape.
std::optional<uint32_t> Lowerer::signal_call_by_name(Call *p_call) {
	if (p_call->callee->kind != NodeKind::Member || !signal_ || !signal_->klass) {
		return std::nullopt;
	}
	Member *member = static_cast<Member *>(p_call->callee);
	auto verb = signal_->klass->members.find(member->name);
	if (verb == signal_->klass->members.end() || verb->second->kind != SymbolKind::Method) {
		return std::nullopt;
	}
	uint32_t object = 0, name = 0;
	uint32_t found = signal_by_name(member->object, writer(p_call->arguments), &object, &name);
	if (found == NONE) {
		return std::nullopt;
	}
	return engine_call(p_call, verb->second, found, Later{ Op::HostMember, member->object->offset, found, object, name });
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
		return vector_new(compilation_.instance_type(array_), p_call->arguments, p_call->callee->offset); // Array(4) is new Array(4).
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
		Expr *later = nullptr;
		for (uint32_t next = i + 1; next < p_call->arguments.count && !later; next++) {
			later = writes_locals(p_call->arguments[next]) ? p_call->arguments[next] : nullptr;
		}
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

// AS3's own toString() on one of Godot's values or objects: Godot's text for it.
uint32_t Lowerer::engine_text(Call *p_call) {
	uint32_t value = expression(static_cast<Member *>(p_call->callee)->object, Kind::Void);
	emit(Op::NullCheck, p_call->offset, value);
	uint32_t out = reg(Kind::Str);
	emit(Op::HostToStr, p_call->offset, out, value);
	return out;
}

// A method of Godot's. `Input.isActionPressed(...)` is a method of the engine's one Input; any other
// static one is called on no object.
uint32_t Lowerer::godot_call(Call *p_call, Symbol *p_method) {
	uint32_t position = p_call->offset;
	if (p_method->is_static()) {
		if (is_singleton(p_method->owner) && on_object(p_call->callee)) {
			return engine_call(p_call, p_method); // `i.isActionPressed()`: on the object `i` holds.
		}
		return is_singleton(p_method->owner) ? engine_call(p_call, p_method, singleton(p_method->owner, position)) : static_call(p_call, p_method);
	}
	Expr *callee = p_call->callee;
	bool through_super = callee->kind == NodeKind::Member && static_cast<Member *>(callee)->object->kind == NodeKind::SuperExpr;
	if (!through_super || (p_method->modifiers & Modifiers::Final)) {
		return engine_call(p_call, p_method);
	}
	// Godot's own version of a virtual does nothing. Asking the engine for it would come straight back
	// to the script. The arguments are still evaluated.
	for (Expr *argument : p_call->arguments) {
		expression(argument, Kind::Void);
	}
	Kind kind = Kind::Void;
	if (p_method->type) {
		kind_of(compilation_.type_of(p_method->type), &kind);
	}
	return kind == Kind::Void ? 0 : default_value(kind, position);
}

// One of Godot's global functions: `lerpf(a, b, t)`, `print(...)`. The call carries the function's
// signature, which the engine's entry for such functions needs.
uint32_t Lowerer::global_call(Call *p_call, Symbol *p_function, Kind p_want) {
	uint32_t position = p_call->offset;
	std::string signature;
	if (!godot_name("@global", p_function->name, &signature)) {
		refuse(p_call->callee->offset, "calling '" + std::string(p_function->name) + "'");
		return default_value(p_want, position);
	}
	const Type *type = p_function->type ? compilation_.type_of(p_function->type) : nullptr;
	Kind kind = Kind::Void;
	if (type && type->kind == Type::Any) {
		// It gives back anything. Where the program says it expects a number or text, that is asked for and
		// checked as it arrives: Godot answers a wrong argument with something rather than nothing, so this
		// is where it is caught. An int or a uint is asked for as a Number and then cut, as AS3 does
		// (`int(lerp(a, b, t))`). Anywhere else, an untyped value, converted where it is used.
		kind = p_want == Kind::I32 || p_want == Kind::U32 ? Kind::F64 : is_numeric(p_want) ? p_want : p_want == Kind::Str ? Kind::Val : Kind::Any;
		type = nullptr;
	} else if (type && !kind_of(type, &kind)) {
		refuse(position, "a call to a Godot function that returns '" + compilation_.type_name(type) + "'");
	}
	std::vector<uint32_t> list;
	FunctionDecl *decl = p_function->node && p_function->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_function->node) : nullptr;
	engine_arguments(decl, p_call->arguments, position, list);
	uint32_t out = kind == Kind::Void ? NONE : engine_reg(kind, type);
	// No class before the dot: the engine's side reads that as a global function.
	emit(Op::HostCallStatic, position, out, string("." + signature), arguments(std::move(list)));
	if (out != NONE && (p_want == Kind::Str || p_want == Kind::I32 || p_want == Kind::U32) && kind != p_want) {
		return convert(out, p_want, position); // Godot's text for it; a Number cut to a whole one.
	}
	return out == NONE ? 0 : out;
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
		emit(Op::CallVirtual, position, out, slots_[decl], arguments(std::move(list)));
	}
	return out == NONE ? 0 : out;
}

uint32_t Lowerer::call(Call *p_call, Kind p_want) {
	Expr *callee = p_call->callee;
	uint32_t position = p_call->offset;
	if (callee->kind == NodeKind::SuperExpr) {
		return super_call(p_call);
	}
	if (const std::optional<uint32_t> signal = callee_symbol(callee) ? std::nullopt : signal_call_by_name(p_call)) {
		return *signal; // `timer[name].add(f)`: a signal by its AS3 name.
	}
	if (const std::optional<uint32_t> by_name = Dynamic(*this).call(p_call)) {
		return *by_name;
	}
	if (callee->kind == NodeKind::Member && is_vector(static_cast<Member *>(callee)->object->type)) {
		return vector_call(p_call);
	}
	if (const std::optional<uint32_t> called = function_method(p_call)) {
		return *called; // f.call(), f.apply()
	}
	Symbol *symbol = callee_symbol(callee);
	if (!symbol) {
		if (const std::optional<uint32_t> by_name = signal_call_by_name(p_call)) {
			return *by_name;
		}
		if (const std::optional<uint32_t> text = library_call(p_call, nullptr)) {
			return *text; // `x.toString()` on an untyped value.
		}
		return value_call(p_call, p_want); // `make()(1)`, `table[name](2)`: whatever it gives is called.
	}
	if (called_value(symbol)) {
		return value_call(p_call, p_want); // A variable, a field or a parameter that holds a function.
	}
	if (symbol->is_type()) {
		return cast(p_call, symbol, p_want);
	}
	if (symbol->unit && symbol->unit->is_builtin && symbol->kind == SymbolKind::Function && symbol->name == "trace") {
		return trace_call(p_call);
	}
	if (symbol->name == "toString" && p_call->arguments.count == 0 && callee->kind == NodeKind::Member &&
			static_cast<Member *>(callee)->object->kind != NodeKind::SuperExpr && is_engine(natural(static_cast<Member *>(callee)->object))) {
		return engine_text(p_call);
	}
	if (is_godot(symbol) && symbol->kind == SymbolKind::Method) {
		return godot_call(p_call, symbol);
	}
	if (is_godot(symbol) && symbol->kind == SymbolKind::Function) {
		return global_call(p_call, symbol, p_want);
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

uint32_t Lowerer::bound_method(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	if (is_null(p_expression)) {
		return default_value(Kind::Val, position);
	}
	Symbol *symbol = callee_symbol(p_expression);
	auto function = functions_.find(declaration(symbol));
	if (!symbol || symbol->kind != SymbolKind::Method || symbol->is_static() || function == functions_.end()) {
		// Any other function value (a function expression, a static method, one held in a variable): a
		// Godot Callable that calls it.
		const uint32_t value = expression(p_expression, Kind::Obj);
		uint32_t out = reg(Kind::Val);
		emit(Op::FunctionToCallable, position, out, value);
		return out;
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
	if (on_this) {
		object = self(position);
	}
	bool exact = (symbol->modifiers & Modifiers::Private) != 0;
	uint32_t out = reg(Kind::Val);
	emit(Op::HostBind, position, out, object, exact ? function->second : (slots_[function->first] | ir::BIND_SLOT));
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
	// A script object goes as itself: the name may be one of its fields that holds a signal, one the code
	// here may see (the interpreter decides, by the class the code is written in). Else its Godot object's.
	bool of_value = false;
	const bool own = script && index->object->kind == NodeKind::ThisExpr && !fn().is_static;
	uint32_t object_reg = own ? self(p_expression->offset, true)
			: script && index->object->kind != NodeKind::ThisExpr ? expression(index->object, Kind::Obj)
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

void Lowerer::engine_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list,
		const std::string &p_defaults) {
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
		const Type *type = nullptr;
		if (i < count && !p_function->parameters[i].is_rest && p_function->parameters[i].type) {
			type = compilation_.type_of(p_function->parameters[i].type);
			if (type && type->kind == Type::Instance && type->symbol == function_ && function_) {
				// Godot takes a function here: a method goes bound to its object.
				Expr *later = nullptr;
				for (uint32_t next = i + 1; next < written && !later; next++) {
					later = writes_locals(p_arguments[next]) ? p_arguments[next] : nullptr;
				}
				r_list.push_back(settled(bound_method(p_arguments[i]), later, p_position));
				continue;
			}
			std::string godot_default;
			if (!p_defaults.empty() && p_function->parameters[i].default_value && is_null(p_function->parameters[i].default_value) &&
					kind_of(type, &kind) && kind == Kind::Val && godot_name("@default", p_defaults + "." + std::to_string(i), &godot_default)) {
				// `null` where the default is one AS3 can't write, with arguments after it: Godot's own
				// default, from Godot's text for it. Written `null`, it is known now; a value that may be
				// null (`var c:Color = null`) is looked at as the call is made.
				uint32_t value = engine_reg(Kind::Val, type);
				if (!is_null(p_arguments[i])) {
					// Into a register of its own: what comes later can't change it.
					emit(Op::Move, p_arguments[i]->offset, value, expression(p_arguments[i], Kind::Val));
					uint32_t present = reg(Kind::Bool);
					emit(Op::HostPresent, p_arguments[i]->offset, present, value);
					uint32_t given = emit(Op::JumpIf, p_arguments[i]->offset, present, 0);
					emit(Op::HostDefault, p_arguments[i]->offset, value, string(godot_default));
					patch(given, here());
				} else {
					emit(Op::HostDefault, p_arguments[i]->offset, value, string(godot_default));
				}
				r_list.push_back(value);
				continue;
			}
			kind = Kind::Void;
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
		Kind own = Kind::Void;
		if (kind == Kind::Void && kind_of(p_arguments[i]->type, &own) && is_numeric(own) && whole(p_arguments[i])) {
			kind = Kind::I64; // Where Godot takes anything, `-5` and `2 * 3` are its integers, not 5.0 and 6.0.
		}
		std::optional<uint32_t> collection = vector_to_engine(p_arguments[i], type);
		collection = collection ? collection : Dynamic(*this).to_engine(p_arguments[i], type);
		uint32_t value = collection ? *collection : expression(p_arguments[i], kind);
		if (kind_at(value) == Kind::Obj) {
			refuse(p_arguments[i]->offset, "passing a script object to Godot");
		}
		r_list.push_back(settled(value, later, p_position));
	}
}

// "Class.method" in Godot's names: where the name table keeps a method's defaults that AS3 can't write.
std::string Lowerer::defaults_key(const Symbol *p_method) const {
	std::string klass, method;
	if (!p_method->owner || !godot_name(p_method->owner->name, std::string_view(), &klass) ||
			!godot_name(p_method->owner->name, p_method->name, &method)) {
		return std::string();
	}
	return klass + "." + method;
}

uint32_t Lowerer::engine_call(Call *p_call, Symbol *p_method, uint32_t p_receiver, std::optional<Later> p_before) {
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
	uint32_t script_object = NONE; // Set if the member is reached through another script object.
	const uint32_t receiver = p_receiver != NONE ? p_receiver : engine_receiver(object, position, &of_value, &script_object);
	if (script_object != NONE) {
		script_object = settled(script_object, writer(p_call->arguments), position);
		list.push_back(receiver); // Filled in below, after the arguments.
	} else {
		list.push_back(settled(receiver, writer(p_call->arguments), position));
	}
	FunctionDecl *decl = p_method->node && p_method->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_method->node) : nullptr;
	engine_arguments(decl, p_call->arguments, position, list, defaults_key(p_method));
	if (script_object != NONE) {
		owner_of(script_object, receiver, object->offset);
	}
	if (p_before) {
		emit(p_before->op, p_before->position, p_before->a, p_before->b, p_before->c); // What the receiver still needs.
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
	engine_arguments(decl, p_call->arguments, position, list, klass + "." + method);
	uint32_t out = kind == Kind::Void ? NONE : engine_reg(kind, p_method->type ? compilation_.type_of(p_method->type) : nullptr);
	emit(Op::HostCallStatic, position, out, string(klass + "." + method), arguments(std::move(list)));
	return out == NONE ? 0 : out;
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
