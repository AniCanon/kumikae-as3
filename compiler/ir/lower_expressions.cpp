#include "lower_calls.h"
#include "lower_dynamic.h"
#include "lower_engine.h"
#include "lower_engine_calls.h"
#include "lower_expressions.h"
#include "lower_functions.h"
#include "lower_library.h"
#include "lower_operators.h"
#include "lower_vectors.h"

namespace kas3 {
namespace ir {

bool Lowerer::is_null(const Expr *p_expression) {
	return p_expression->kind == NodeKind::NullLit;
}

uint32_t Lowerer::expression(Expr *p_expression, Kind p_want) {
	if (!p_expression) {
		return default_value(p_want, 0);
	}
	if (is_null(p_expression) && p_want == Kind::Any) {
		uint32_t out = reg(Kind::Any);
		emit(Op::AnyConst, p_expression->offset, out, 1);
		return out;
	}
	if (is_null(p_expression)) {
		// null is whatever is asked for: no reference, 0, or false.
		uint32_t out = default_value(p_want == Kind::Void ? Kind::Obj : p_want == Kind::F64 ? Kind::I32 : p_want, p_expression->offset);
		return p_want == Kind::F64 ? convert(out, Kind::F64, p_expression->offset) : out;
	}
	uint32_t value = Expressions(*this).evaluate(p_expression, p_want);
	return convert(value, p_want, p_expression->offset);
}

Symbol *Lowerer::callee_symbol(Expr *p_callee) {
	if (p_callee->kind == NodeKind::Name) {
		return static_cast<Name *>(p_callee)->symbol;
	}
	if (p_callee->kind == NodeKind::Member) {
		Member *member = static_cast<Member *>(p_callee);
		return member->resolved();
	}
	return nullptr;
}

Symbol *Lowerer::named_class(Expr *p_expression) {
	Symbol *symbol = callee_symbol(p_expression);
	return symbol && symbol->is_type() ? symbol : nullptr;
}

// A class named where a value is wanted (`var c:Class = Player`): the class itself. Nothing if the
// expression names no class.
std::optional<uint32_t> Expressions::class_value(Expr *p_expression) {
	const Type *type = p_expression->type;
	if (!type || type->kind != Type::ClassObject) {
		return std::nullopt;
	}
	const uint32_t position = p_expression->offset;
	const std::optional<uint32_t> code = l_.class_code(type->element);
	uint32_t klass = NONE;
	if (!code || !l_.class_type_ || !l_.class_index(l_.class_type_, &klass)) {
		l_.refuse(position, "the class '" + l_.compilation_.type_name(type->element) + "' as a value");
		return l_.default_value(Kind::Obj, position);
	}
	if (const Symbol *symbol = l_.named_class(p_expression)) {
		l_.touch(symbol, position); // A class named is set up, as in AVM2.
	}
	const uint32_t out = l_.reg(Kind::Obj);
	l_.emit(Op::ClassValue, position, out, *code);
	return out;
}

uint32_t Expressions::name_value(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	Name *name = static_cast<Name *>(p_expression);
	if (const std::optional<uint32_t> klass = class_value(p_expression)) {
		return *klass;
	}
	uint32_t constant = 0;
	if (l_.builtin_constant(name->symbol, position, &constant)) {
		return constant;
	}
	if (name->symbol && name->symbol->kind == SymbolKind::Arguments && l_.arguments_reg_ != NONE) {
		return l_.arguments_reg_;
	}
	if (name->symbol && (name->symbol->kind == SymbolKind::Method || name->symbol->kind == SymbolKind::Function)) {
		return Functions(l_).function_value(p_expression); // A method or a function as a value, not called.
	}
	l_.natural(p_expression);
	return l_.load(l_.place_of(name), position);
}

uint32_t Expressions::member_value(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	Member *member = static_cast<Member *>(p_expression);
	Symbol *symbol = member->resolved();
	if (const std::optional<uint32_t> klass = class_value(p_expression)) {
		return *klass; // `flash.utils.Dictionary`, written with its package.
	}
	// The one member of a built-in value so far: a string's length.
	if (symbol && symbol->owner == l_.string_ && symbol->name == "length") {
		uint32_t text = l_.expression(member->object, Kind::Str);
		l_.emit(Op::NullCheck, member->object->offset, text);
		uint32_t out = l_.reg(Kind::I32);
		l_.emit(Op::StrLength, position, out, text);
		return out;
	}
	if ((!symbol || symbol->owner == l_.signal_) && l_.signal_ && l_.signal_->klass && member->object->kind == NodeKind::Index) {
		// `node[name].numListeners`: a signal's property, the signal found by its name as the line runs.
		const auto property = l_.signal_->klass->members.find(member->name);
		uint32_t getter = 0, object = 0, name = 0;
		Kind kind = Kind::Void;
		const Symbol *read = property != l_.signal_->klass->members.end() && property->second->kind == SymbolKind::Property ? property->second : nullptr;
		TypeRef *written = read ? (read->type ? read->type : read->getter ? read->getter->return_type : nullptr) : nullptr;
		if (written && l_.kind_of(l_.compilation_.type_of(written), &kind) && l_.engine_name(l_.signal_, member->name, &getter)) {
			const uint32_t found = EngineCalls(l_).signal_by_name(member->object, nullptr, &object, &name);
			if (found != NONE) {
				l_.emit(Op::HostMember, member->object->offset, found, object, name);
				const uint32_t out = l_.reg(kind);
				l_.emit(Op::HostGet, position, out, found, getter);
				return out;
			}
		}
	}
	if (symbol && l_.function_ && symbol->owner == l_.function_ && symbol->name == "length") {
		const uint32_t value = l_.expression(member->object, Kind::Obj);
		const uint32_t out = l_.reg(Kind::I32);
		l_.emit(Op::FunctionLength, position, out, value);
		return out;
	}
	uint32_t constant = 0;
	if (l_.builtin_constant(symbol, position, &constant)) {
		return constant;
	}
	if (symbol && (symbol->kind == SymbolKind::Method || symbol->kind == SymbolKind::Function)) {
		return Functions(l_).function_value(p_expression); // A method or a function as a value, not called.
	}
	l_.natural(p_expression);
	return l_.load(l_.place_of(member), member->name_offset);
}

uint32_t Expressions::new_value(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	New *made = static_cast<New *>(p_expression);
	if (l_.is_vector(made->type)) {
		return Vectors(l_).vector_new(made->type, made->arguments, made->callee->offset);
	}
	if (const std::optional<uint32_t> library = Library::construct(l_, made)) {
		return *library;
	}
	Symbol *symbol = l_.named_class(made->callee);
	uint32_t index = 0;
	uint32_t type_name = 0;
	if (symbol && Lowerer::is_godot(symbol) && !l_.is_godot_object(symbol) && l_.engine_name(symbol, std::string_view(), &type_name)) {
		// One of Godot's value types: the engine makes it.
		Symbol *constructor = symbol->klass ? symbol->klass->constructor : nullptr;
		FunctionDecl *decl = constructor && constructor->node && constructor->node->kind == NodeKind::FunctionDecl
				? static_cast<FunctionDecl *>(constructor->node)
				: nullptr;
		std::vector<uint32_t> list;
		EngineCalls(l_).engine_arguments(decl, made->arguments, position, list);
		if (Engine(l_).stores_int32(made->type)) {
			// Its constructor takes anything in its first places, so the width is checked here.
			for (size_t i = 0; i < list.size() && i < made->arguments.count; i++) {
				if (Lowerer::is_integral(l_.kind_at(list[i])) || l_.whole(made->arguments[i])) {
					list[i] = Engine(l_).as_int32(list[i], made->arguments[i]->offset);
				}
			}
		}
		uint32_t value = l_.engine_reg(Kind::Val, l_.module_.engine_names[type_name].name);
		l_.emit(Op::HostNew, position, value, type_name, l_.arguments(std::move(list)));
		return value;
	}
	if (symbol && Lowerer::is_godot(symbol) && l_.is_godot_object(symbol) && made->arguments.count == 0 && l_.engine_name(symbol, std::string_view(), &type_name)) {
		// One of Godot's own objects: the engine makes it, as GDScript's `Node2D.new()` does. A node is
		// then the program's to put in the tree or free; a counted object goes when nothing holds it.
		uint32_t value = l_.engine_reg(Kind::Ref, made->type);
		l_.emit(Op::HostNew, position, value, type_name, l_.arguments({}));
		return value;
	}
	if (const std::optional<uint32_t> table = Dynamic(l_).made(made, symbol)) {
		return *table;
	}
	Kind primitive = Kind::Void;
	if (symbol && l_.kind_of_class(symbol, &primitive) && made->arguments.count <= 1) {
		// AS3 has no wrapper objects: `new String(x)` is the String itself, as String(x) gives it.
		return Calls(l_).conversion_of(made->arguments, primitive, position);
	}
	if (!symbol) {
		// `new c(a)`, the class held in a variable: found out, and made, as the line runs.
		Dynamic(l_).table_class(false);
		l_.vector_class(l_.compilation_.instance_type(l_.array_));
		Functions(l_).use_argument_error();
		const uint32_t klass = l_.settled(l_.any_value(made->callee), Lowerer::writer(made->arguments), position);
		std::vector<uint32_t> list;
		for (uint32_t i = 0; i < made->arguments.count; i++) {
			Expr *later = Lowerer::writer(made->arguments, i + 1);
			list.push_back(l_.settled(l_.any_value(made->arguments[i]), later, position));
		}
		const uint32_t out = l_.reg(Kind::Any);
		l_.emit(Op::ClassNew, position, out, klass, l_.arguments(std::move(list)));
		return out;
	}
	if (!l_.class_index(symbol, &index)) {
		l_.refuse(made->callee->offset, "creating a '" + std::string(symbol->name) + "'");
		return l_.default_value(Kind::Obj, position);
	}
	l_.touch(symbol, position);
	uint32_t out = l_.reg(Kind::Obj);
	l_.emit(Op::New, position, out, index);
	std::vector<uint32_t> list = { out };
	Calls(l_).constructor_arguments(symbol, made->arguments, position, list);
	l_.emit(Op::Call, position, NONE, l_.module_.classes[index].constructor, l_.arguments(std::move(list)));
	return out;
}

// Whether a script class's Godot object may be reference-counted: its first Godot base is Godot's Object,
// which Godot lets a RefCounted carry as well as any other object, or it extends RefCounted.
bool Lowerer::may_be_counted(const Symbol *p_class) const {
	const Symbol *godot_base = p_class;
	while (godot_base && godot_base->klass && !is_godot(godot_base)) {
		godot_base = godot_base->klass->base;
	}
	if (godot_base && godot_base == godot_object_) {
		return true;
	}
	for (const Symbol *at = godot_base; at && at->klass; at = at->klass->base) {
		if (at->name == "RefCounted" && is_godot(at)) {
			return true;
		}
	}
	return false;
}

uint32_t Expressions::postfix(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	Postfix *postfix = static_cast<Postfix *>(p_expression);
	Place place = l_.place_of(postfix->operand);
	if (place.kind == Kind::I64) {
		uint32_t old = l_.reg(Kind::I64);
		l_.emit(Op::Move, position, old, l_.load(place, position));
		uint32_t one = l_.reg(Kind::I64);
		l_.emit(Op::ConstI64, position, one, l_.wide(1));
		uint32_t next = l_.reg(Kind::I64);
		l_.emit(postfix->op == TokenKind::PlusPlus ? Op::AddI64 : Op::SubI64, position, next, old, one);
		l_.store(place, next, position);
		return old;
	}
	// The value is the old one, as a number.
	uint32_t old = l_.convert(l_.load(place, position), Kind::F64, position);
	if (old == place.index && place.where == Place::Register) {
		uint32_t copy = l_.reg(Kind::F64);
		l_.emit(Op::Move, position, copy, old);
		old = copy;
	}
	uint32_t one = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, position, one, l_.number(1));
	uint32_t next = l_.reg(Kind::F64);
	l_.emit(postfix->op == TokenKind::PlusPlus ? Op::AddF64 : Op::SubF64, position, next, old, one);
	l_.store(place, l_.convert(next, place.kind, position), position);
	return old;
}

uint32_t Expressions::conditional(Expr *p_expression, Kind p_want) {
	uint32_t position = p_expression->offset;
	Conditional *conditional = static_cast<Conditional *>(p_expression);
	Kind kind = p_want != Kind::Void ? p_want : l_.natural(p_expression);
	uint32_t out = l_.engine_reg(kind, p_expression->type);
	uint32_t test = l_.expression(conditional->condition, Kind::Bool);
	uint32_t to_else = l_.emit(Op::JumpIfNot, position, test, 0);
	uint32_t then_value = l_.expression(conditional->then_value, kind);
	l_.emit(Op::Move, position, out, then_value);
	uint32_t to_end = l_.emit(Op::Jump, position, 0);
	l_.patch(to_else, l_.here());
	uint32_t else_value = l_.expression(conditional->else_value, kind);
	l_.emit(Op::Move, position, out, else_value);
	l_.patch(to_end, l_.here());
	return out;
}

uint32_t Expressions::string_literal(StringLit *p_literal) {
	uint32_t out = l_.reg(Kind::Str);
	l_.emit(Op::ConstStr, p_literal->offset, out, l_.string(Lowerer::unescape(p_literal->text)));
	return out;
}

uint32_t Expressions::boolean_literal(BoolLit *p_literal) {
	uint32_t out = l_.reg(Kind::Bool);
	l_.emit(Op::ConstBool, p_literal->offset, out, p_literal->value ? 1 : 0);
	return out;
}

uint32_t Expressions::this_value(uint32_t p_position) {
	if (l_.fn().is_static && !l_.received_env_) {
		l_.refuse(p_position, "'this' here");
		return l_.default_value(Kind::Obj, p_position);
	}
	return l_.self(p_position, true);
}

uint32_t Expressions::evaluate(Expr *p_expression, Kind p_want) {
	uint32_t position = p_expression->offset;
	if (const auto held = l_.evaluated_.find(p_expression); held != l_.evaluated_.end()) {
		return held->second;
	}
	switch (p_expression->kind) {
		case NodeKind::NumberLit:
			return l_.number_literal(p_expression, p_want);
		case NodeKind::StringLit:
			return string_literal(static_cast<StringLit *>(p_expression));
		case NodeKind::BoolLit:
			return boolean_literal(static_cast<BoolLit *>(p_expression));
		case NodeKind::ThisExpr:
			return this_value(position);
		case NodeKind::Name:
			return name_value(p_expression);
		case NodeKind::Member:
			return member_value(p_expression);
		case NodeKind::Call:
			return Calls(l_).call(static_cast<Call *>(p_expression), p_want);
		case NodeKind::New:
			return new_value(p_expression);
		case NodeKind::Unary:
			return unary(static_cast<Unary *>(p_expression), p_want);
		case NodeKind::Postfix:
			return postfix(p_expression);
		case NodeKind::Binary:
			return Operators(l_).binary(static_cast<Binary *>(p_expression), p_want);
		case NodeKind::Assign:
			return assign(static_cast<Assign *>(p_expression));
		case NodeKind::Conditional:
			return conditional(p_expression, p_want);
		case NodeKind::SuperExpr:
			l_.refuse(position, "'super' used as a value");
			break;
		case NodeKind::RegexLit:
			return Library::regex_literal(l_, static_cast<RegexLit *>(p_expression));
		case NodeKind::ArrayLit:
			return Vectors(l_).vector_literal(p_expression, static_cast<ArrayLit *>(p_expression)->elements);
		case NodeKind::ObjectLit:
			return Dynamic(l_).literal(static_cast<ObjectLit *>(p_expression));
		case NodeKind::VectorLit:
			return Vectors(l_).vector_literal(p_expression, static_cast<VectorLit *>(p_expression)->elements);
		case NodeKind::FunctionExpr:
			return Functions(l_).closure_value(static_cast<FunctionExpr *>(p_expression));
		case NodeKind::Index:
			return l_.load(l_.place_of(p_expression), position);
		case NodeKind::TypeApply:
			if (const std::optional<uint32_t> klass = class_value(p_expression)) {
				return *klass; // `Vector.<int>` as a value.
			}
			l_.refuse(position, "a Vector");
			break;
		default:
			l_.refuse(position, "this expression");
			break;
	}
	return l_.default_value(p_want, position);
}

// `-x` and `+x`.
uint32_t Expressions::sign(Unary *p_unary, Kind p_want) {
	uint32_t position = p_unary->offset;
	Kind result = Kind::Void;
	if (is_engine(l_.natural(p_unary->operand)) && l_.kind_of(p_unary->type, &result) && result != Kind::Void) {
		return Engine(l_).engine_operate(p_unary->op, { Engine(l_).operand(p_unary->operand) }, result, p_unary->type, position);
	}
	if (const std::optional<int64_t> exact = Lowerer::wide_literal(p_unary); exact && p_want == Kind::I64) {
		uint32_t out = l_.reg(Kind::I64);
		l_.emit(Op::ConstI64, position, out, l_.wide(*exact)); // The least Int64 has no positive to negate.
		return out;
	}
	if (l_.kind_of(p_unary->operand->type, &result) && result == Kind::I64) {
		uint32_t value = l_.expression(p_unary->operand, Kind::I64);
		if (p_unary->op == TokenKind::Plus) {
			return value;
		}
		uint32_t out = l_.reg(Kind::I64);
		l_.emit(Op::NegI64, position, out, value);
		return out;
	}
	if (p_unary->op == TokenKind::Plus) {
		return l_.expression(p_unary->operand, Kind::F64);
	}
	if (p_unary->operand->kind == NodeKind::NumberLit) {
		// `-1` is one constant, not a 1 negated as the program runs. -0 stays a Number.
		const double value = -Lowerer::literal_value(static_cast<NumberLit *>(p_unary->operand));
		return l_.number_constant(value, l_.natural(p_unary), value == 0 ? Kind::F64 : p_want, position);
	}
	uint32_t out = l_.reg(Kind::F64);
	l_.emit(Op::NegF64, position, out, l_.expression(p_unary->operand, Kind::F64));
	return out;
}

// `++x` and `--x`: the value is the new one.
uint32_t Expressions::prefix(Unary *p_unary) {
	uint32_t position = p_unary->offset;
	Place place = l_.place_of(p_unary->operand);
	if (place.kind == Kind::I64) {
		uint32_t one = l_.reg(Kind::I64);
		l_.emit(Op::ConstI64, position, one, l_.wide(1));
		uint32_t next = l_.reg(Kind::I64);
		l_.emit(p_unary->op == TokenKind::PlusPlus ? Op::AddI64 : Op::SubI64, position, next, l_.load(place, position), one);
		l_.store(place, next, position);
		return next;
	}
	uint32_t old = l_.convert(l_.load(place, position), Kind::F64, position);
	uint32_t one = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, position, one, l_.number(1));
	uint32_t next = l_.reg(Kind::F64);
	l_.emit(p_unary->op == TokenKind::PlusPlus ? Op::AddF64 : Op::SubF64, position, next, old, one);
	uint32_t stored = l_.convert(next, place.kind, position);
	l_.store(place, stored, position);
	return stored;
}

uint32_t Expressions::unary(Unary *p_unary, Kind p_want) {
	uint32_t position = p_unary->offset;
	switch (p_unary->op) {
		case TokenKind::Minus:
		case TokenKind::Plus:
			return sign(p_unary, p_want);
		case TokenKind::KwTypeof:
			return l_.any_typeof(p_unary->operand, position);
		case TokenKind::KwDelete:
			return Dynamic(l_).remove(p_unary);
		case TokenKind::Bang: {
			uint32_t out = l_.reg(Kind::Bool);
			l_.emit(Op::Not, position, out, l_.expression(p_unary->operand, Kind::Bool));
			return out;
		}
		case TokenKind::Tilde: {
			Kind operand_kind = Kind::Void;
			if (l_.kind_of(p_unary->operand->type, &operand_kind) && operand_kind == Kind::I64) {
				uint32_t out = l_.reg(Kind::I64);
				l_.emit(Op::BitNotI64, position, out, l_.expression(p_unary->operand, Kind::I64));
				return out;
			}
			uint32_t out = l_.reg(Kind::I32);
			l_.emit(Op::BitNot, position, out, l_.expression(p_unary->operand, Kind::I32));
			return out;
		}
		case TokenKind::PlusPlus:
		case TokenKind::MinusMinus:
			return prefix(p_unary);
		default:
			l_.refuse(position, "this operator");
			return l_.default_value(Kind::F64, position);
	}
}

uint32_t Lowerer::wide_operand(Expr *p_expression, Kind p_beside) {
	if (const std::optional<int64_t> exact = wide_literal(p_expression); exact && p_beside == Kind::I64) {
		uint32_t out = reg(Kind::I64);
		emit(Op::ConstI64, p_expression->offset, out, wide(*exact));
		return out;
	}
	return expression(p_expression, Kind::Void);
}

Kind Lowerer::operand_kind(Expr *p_expression, Expr *p_other) {
	Kind kind = Kind::Void, other = Kind::Void;
	kind_of(p_expression->type, &kind);
	if (kind_of(p_other->type, &other) && other == Kind::I64 && wide_literal(p_expression)) {
		return Kind::I64;
	}
	return kind;
}

uint32_t Expressions::assign(Assign *p_assign) {
	uint32_t position = p_assign->offset;
	Place place = l_.place_of(p_assign->target, p_assign->value);
	uint32_t value = 0;
	if (p_assign->op == TokenKind::Assign) {
		std::optional<uint32_t> collection = place.where == Place::Engine ? Vectors(l_).vector_to_engine(p_assign->value, place.type) : std::nullopt;
		collection = collection || place.where != Place::Engine ? collection : Dynamic(l_).to_engine(p_assign->value, place.type);
		value = collection ? *collection : l_.typed(p_assign->value, p_assign->target->type, place.kind);
	} else if (p_assign->op == TokenKind::AmpAmpAssign || p_assign->op == TokenKind::PipePipeAssign) {
		// a &&= b stores b only if a is true; a ||= b only if a is false. Otherwise a stays as it is.
		value = l_.reg(place.kind);
		l_.emit(Op::Move, position, value, l_.load(place, position));
		const uint32_t test = l_.convert(value, Kind::Bool, position);
		const uint32_t skip = l_.emit(p_assign->op == TokenKind::AmpAmpAssign ? Op::JumpIfNot : Op::JumpIf, position, test, 0);
		l_.emit(Op::Move, position, value, l_.typed(p_assign->value, p_assign->target->type, place.kind));
		l_.store(place, value, position);
		l_.patch(skip, l_.here());
		return value;
	} else if (p_assign->op == TokenKind::PlusAssign &&
			(place.kind == Kind::Any || l_.untyped(p_assign->value) ||
					(l_.natural(p_assign->value) == Kind::Obj && (place.kind != Kind::Str || Library::own_value_of(p_assign->value))))) {
		// `+=` with an untyped side, or an object as `+` takes one: text or numbers, by what each holds when
		// it runs.
		uint32_t old = l_.settled(l_.convert(l_.load(place, position), Kind::Any, position), p_assign->value, position);
		uint32_t sum = l_.reg(Kind::Any);
		l_.emit(Op::AnyAdd, position, sum, old, l_.any_value(p_assign->value));
		value = l_.convert(sum, place.kind, position);
	} else {
		uint32_t old = l_.settled(l_.load(place, position), p_assign->value, position);
		Kind right_kind = Kind::Void;
		l_.kind_of(p_assign->value->type, &right_kind);
		bool strings = p_assign->op == TokenKind::PlusAssign && (place.kind == Kind::Str || l_.is_string_type(p_assign->value->type));
		bool engine = !strings && (is_engine(l_.kind_at(old)) || is_engine(right_kind));
		uint32_t right = engine ? Engine(l_).operand(p_assign->value, p_assign->target->type) : l_.wide_operand(p_assign->value, strings || p_assign->op == TokenKind::SlashAssign ? Kind::Void : place.kind);
		if (engine) {
			value = l_.convert(Engine(l_).engine_operate(p_assign->op, { old, right }, place.kind, p_assign->target->type, position), place.kind, position);
		} else {
			value = l_.convert(Operators(l_).operate(p_assign->op, old, right, strings, position), place.kind, position);
		}
	}
	l_.store(place, value, position);
	return value;
}

} // namespace ir
} // namespace kas3
