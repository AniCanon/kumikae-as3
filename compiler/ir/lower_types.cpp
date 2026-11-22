#include "lower_dynamic.h"
#include "lower_library.h"

namespace kas3 {
namespace ir {

std::string Lowerer::class_name(const Symbol *p_class) const {
	return p_class->package && !p_class->package->name.empty() ? p_class->package->name + "." + std::string(p_class->name)
															   : std::string(p_class->name);
}

bool Lowerer::kind_of(const Type *p_type, Kind *r_kind) const {
	if (!p_type) {
		return false;
	}
	if (p_type->kind == Type::Void) {
		*r_kind = Kind::Void;
		return true;
	}
	if (p_type->kind == Type::Any) {
		*r_kind = Kind::Any;
		return true;
	}
	if (p_type->typed_sequence()) {
		// A vector is an object, if the interpreter can hold what is in it.
		Kind element = Kind::Void;
		*r_kind = Kind::Obj;
		return kind_of(p_type->element, &element) && element != Kind::Void;
	}
	if (p_type->kind == Type::Callback || p_type->kind == Type::ClassObject) {
		*r_kind = Kind::Obj; // A class itself: a class value.
		return true;
	}
	if (p_type->kind != Type::Instance || !p_type->symbol) {
		return false;
	}
	const Symbol *symbol = p_type->symbol;
	if (symbol == class_type_ && class_type_) {
		*r_kind = Kind::Obj;
	} else if (symbol == int_) {
		*r_kind = Kind::I32;
	} else if (symbol == uint_) {
		*r_kind = Kind::U32;
	} else if (symbol == int64_) {
		*r_kind = Kind::I64;
	} else if (symbol == byte_) {
		*r_kind = Kind::U8;
	} else if (symbol == float_) {
		*r_kind = Kind::F32;
	} else if (symbol == number_) {
		*r_kind = Kind::F64;
	} else if (symbol == boolean_) {
		*r_kind = Kind::Bool;
	} else if (symbol == string_) {
		*r_kind = Kind::Str;
	} else if (symbol == object_) {
		*r_kind = Kind::Any; // An Object may hold any value, numbers too.
	} else if (classes_.count(symbol) || is_error(symbol) || is_dictionary(symbol) || (symbol == function_ && function_) || (symbol == array_ && array_) ||
			Library::holds(symbol)) {
		*r_kind = Kind::Obj;
	} else if (is_godot(symbol) && symbol->kind == SymbolKind::Class) {
		*r_kind = is_godot_object(symbol) ? Kind::Ref : Kind::Val;
	} else {
		return false;
	}
	return true;
}

bool Lowerer::is_godot(const Symbol *p_symbol) {
	return p_symbol && p_symbol->unit && p_symbol->unit->is_declaration && p_symbol->unit->source.path() == GODOT_DECLARATIONS_PATH;
}

bool Lowerer::is_godot_object(const Symbol *p_class) const {
	for (const Symbol *at = p_class; at && at->klass; at = at->klass->base) {
		if (at == godot_object_) {
			return true;
		}
	}
	return false;
}

bool Lowerer::engine_name(const Symbol *p_class, std::string_view p_member, uint32_t *r_name) {
	if (!p_class) {
		return false;
	}
	const std::string key = p_member.empty() ? std::string(p_class->name) : std::string(p_class->name) + "." + std::string(p_member);
	const std::optional<uint32_t> found = engine_entry(key);
	if (found) {
		*r_name = *found;
	}
	return found.has_value();
}

std::optional<uint32_t> Lowerer::engine_entry(std::string_view p_key) {
	const std::optional<uint32_t> id = godot_entry(p_key);
	if (!id) {
		return std::nullopt;
	}
	if (auto known = engine_names_.find(*id); known != engine_names_.end()) {
		return known->second;
	}
	const GodotEntry entry = godot_entry_at(*id);
	EngineName name;
	name.id = *id;
	name.key = std::string(entry.key);
	name.value = std::string(entry.value);
	// A default's text is all one value; anything else starts with Godot's name.
	name.name = entry.key.rfind("@default.", 0) == 0 ? name.value : name.value.substr(0, name.value.find(' '));
	const size_t dot = entry.key.find('.');
	if (entry.key[0] != '@' && dot != std::string_view::npos) {
		name.owner = godot_name(entry.key.substr(0, dot), std::string_view()).value_or(std::string());
	}
	module_.engine_names.push_back(std::move(name));
	const uint32_t index = (uint32_t)module_.engine_names.size() - 1;
	engine_names_[*id] = index;
	return index;
}

std::optional<uint32_t> Lowerer::engine_operator(std::string_view p_name) {
	const std::optional<uint32_t> id = godot_operator(p_name);
	return id ? engine_entry(godot_entry_at(*id).key) : std::nullopt;
}

Kind Lowerer::written_kind(TypeRef *p_type, uint32_t p_offset, Kind p_missing) {
	if (!p_type) {
		// No type written: AS3 takes it as `*` (the checker says so).
		(void)p_offset;
		return p_missing == Kind::Void ? Kind::Void : Kind::Any;
	}
	Kind kind = Kind::Void;
	if (!kind_of(compilation_.type_of(p_type), &kind)) {
		refuse(p_type->offset, "the type '" + compilation_.type_name(compilation_.type_of(p_type)) + "'");
		refused_types_.insert(compilation_.type_of(p_type));
		// What it would be held as, where that is known (a vector of a kind not held yet is an object):
		// the rest of the program then lowers without errors that only follow from this one.
		return kind == Kind::Void ? Kind::F64 : kind;
	}
	return kind;
}

bool Lowerer::is_error(const Symbol *p_class) const {
	for (const Symbol *at = p_class; at && at->klass; at = at->klass->base) {
		if (at == error_) {
			return at->unit && at->unit->is_builtin;
		}
	}
	return false;
}

bool Lowerer::class_index(const Symbol *p_class, uint32_t *r_index) {
	auto found = classes_.find(p_class);
	if (found != classes_.end()) {
		*r_index = found->second;
		return true;
	}
	if (is_dictionary(p_class)) {
		*r_index = Dynamic(*this).table_class(true);
		classes_[p_class] = *r_index;
		return true;
	}
	if (const std::optional<uint32_t> library = Library::class_of(*this, p_class)) {
		*r_index = *library;
		return true;
	}
	if (p_class && p_class == class_type_) {
		// Class: the built-in class of class values (CLASS_CODE in ir_codes.h).
		Class klass;
		klass.name = "Class";
		klass.fields = { { "class", Kind::I32 } };
		klass.built_in = true;
		module_.classes.push_back(std::move(klass));
		module_.class_class = *r_index = (uint32_t)module_.classes.size() - 1;
		classes_[p_class] = module_.class_class;
		return true;
	}
	if (p_class && p_class == function_) {
		// Function: the built-in class of function values (FUNCTION_INDEX and FUNCTION_FIRST in ir_codes.h).
		Class klass;
		klass.name = "Function";
		klass.fields = { { "function", Kind::I32 }, { "first", Kind::Obj } };
		klass.built_in = true;
		module_.classes.push_back(std::move(klass));
		module_.function_class = *r_index = (uint32_t)module_.classes.size() - 1;
		classes_[p_class] = module_.function_class;
		return true;
	}
	if (!p_class || !p_class->unit || !p_class->unit->is_builtin || !is_error(p_class)) {
		return false;
	}
	return error_class(p_class, r_index);
}

namespace {

// The constructor error_class() gives each class it makes. p_name: the class's own name, among the module's
// strings.
Function error_constructor(const std::string &p_class_name, uint32_t p_owner, uint32_t p_name) {
	Function constructor;
	constructor.name = p_class_name;
	constructor.owner = p_owner;
	constructor.parameters = 3;
	constructor.registers = { Kind::Obj, Kind::Str, Kind::I32, Kind::Str, Kind::Str };
	const auto op = [&](Op p_op, uint32_t p_a, uint32_t p_b, uint32_t p_c) {
		Instruction made;
		made.op = p_op;
		made.a = p_a;
		made.b = p_b;
		made.c = p_c;
		constructor.code.push_back(made);
	};
	op(Op::SetField, 0, ERROR_MESSAGE, 1);
	op(Op::SetField, 0, ERROR_ID, 2);
	op(Op::ConstStr, 3, p_name, 0);
	op(Op::SetField, 0, ERROR_NAME, 3);
	op(Op::ErrorStack, 4, 0, 0);
	op(Op::SetField, 0, ERROR_STACK, 4);
	op(Op::ReturnVoid, 0, 0, 0);
	return constructor;
}

} // namespace

// Error and its built-in subclasses: message, name, errorID and the stack trace (ERROR_MESSAGE and the
// rest in ir_codes.h), and a constructor that stores the message and the id, and takes the trace where it
// is made, as Flash does.
bool Lowerer::error_class(const Symbol *p_class, uint32_t *r_index) {
	uint32_t base = NONE;
	if (p_class != error_ && !class_index(p_class->klass->base, &base)) {
		return false;
	}
	Class klass;
	klass.name = class_name(p_class);
	klass.base = base;
	klass.fields = { { "message", Kind::Str, NONE, false, true }, { "name", Kind::Str, NONE, false, true }, { "errorID", Kind::I32, NONE, false, true },
		{ "stack", Kind::Str } };
	klass.built_in = true;
	Function constructor = error_constructor(klass.name, (uint32_t)module_.classes.size(), string(std::string(p_class->name)));
	klass.constructor = (uint32_t)module_.functions.size();
	module_.functions.push_back(std::move(constructor));
	module_.classes.push_back(std::move(klass));
	uint32_t index = (uint32_t)module_.classes.size() - 1;
	classes_[p_class] = index;
	if (const FunctionDecl *declared = declaration(p_class->klass->constructor)) {
		functions_[declared] = module_.classes[index].constructor;
	}
	// What AS3 reads of an Error: its message, its name, and errorID (a getter, read as the field).
	for (const auto &[name, field] : { std::pair<const char *, uint32_t>{ "message", ERROR_MESSAGE }, { "name", ERROR_NAME }, { "errorID", ERROR_ID } }) {
		auto member = error_->klass->members.find(name);
		if (member != error_->klass->members.end()) {
			fields_[member->second] = field;
		}
	}
	*r_index = index;
	return true;
}

uint32_t Lowerer::default_value(Kind p_kind, uint32_t p_position, const Type *p_type) {
	const Kind kind = p_kind == Kind::Void ? Kind::F64 : p_kind;
	uint32_t out = engine_reg(kind, p_type);
	switch (p_kind) {
		case Kind::I32:
			emit(Op::ConstI32, p_position, out, 0);
			break;
		case Kind::U8:
		case Kind::U32:
			emit(Op::ConstU32, p_position, out, 0);
			break;
		case Kind::Bool:
			emit(Op::ConstBool, p_position, out, 0);
			break;
		case Kind::I64:
			emit(Op::ConstI64, p_position, out, wide(0));
			break;
		case Kind::Str:
		case Kind::Obj:
		case Kind::Val:
		case Kind::Ref:
			emit(Op::ConstNull, p_position, out);
			break;
		case Kind::Any:
			emit(Op::AnyConst, p_position, out, 0); // undefined
			break;
		default:
			emit(Op::ConstF64, p_position, out, number(std::numeric_limits<double>::quiet_NaN()));
			break;
	}
	return out;
}

// To or from an Int64. See convert() for p_written.
uint32_t Lowerer::convert_wide(uint32_t p_reg, Kind p_to, uint32_t p_position, bool p_written) {
	Kind from = kind_at(p_reg);
	auto step = [&](Op p_op, Kind p_kind, uint32_t p_source) {
		uint32_t out = reg(p_kind);
		emit(p_op, p_position, out, p_source);
		return out;
	};
	if (from == Kind::I64) {
		switch (p_to) {
			case Kind::I32:
				return step(p_written ? Op::I64WrapI32 : Op::I64ToI32, Kind::I32, p_reg);
			case Kind::U32:
				return step(p_written ? Op::I64WrapU32 : Op::I64ToU32, Kind::U32, p_reg);
			case Kind::F64:
				return step(p_written ? Op::I64RoundF64 : Op::I64ToF64, Kind::F64, p_reg);
			case Kind::Bool:
				return step(Op::I64ToBool, Kind::Bool, p_reg);
			case Kind::Str:
				return step(Op::I64ToStr, Kind::Str, p_reg);
			default:
				refuse(p_position, is_engine(p_to) ? "passing this value where a Godot value is expected" : "converting a value to an object");
				return default_value(p_to, p_position);
		}
	}
	switch (from) {
		case Kind::I32:
			return step(Op::I32ToI64, Kind::I64, p_reg);
		case Kind::U32:
			return step(Op::U32ToI64, Kind::I64, p_reg);
		case Kind::F64:
			return step(p_written ? Op::F64ClampI64 : Op::F64ToI64, Kind::I64, p_reg);
		case Kind::Bool:
			return step(Op::F64ClampI64, Kind::I64, step(Op::BoolToF64, Kind::F64, p_reg));
		case Kind::Str:
			// Written out, cut as AS3 cuts; otherwise text that is no whole number an Int64 holds is a RangeError.
			return step(p_written ? Op::StrToI64 : Op::StrToI64Checked, Kind::I64, p_reg);
		default:
			refuse(p_position, is_engine(from) ? "converting a Godot value" : "converting an object to a number");
			return default_value(Kind::I64, p_position);
	}
}

uint32_t Lowerer::convert(uint32_t p_reg, Kind p_to, uint32_t p_position, bool p_written) {
	Kind from = kind_at(p_reg);
	if (from == p_to || p_to == Kind::Void) {
		return p_reg;
	}
	if (p_to == Kind::U8 || from == Kind::U8) {
		if (p_to == Kind::U8) {
			const bool signed_input = from == Kind::I32;
			const uint32_t whole = signed_input ? p_reg : convert(p_reg, Kind::U32, p_position, true);
			const uint32_t out = reg(Kind::U8);
			emit(signed_input ? Op::I32ToU8 : Op::U32ToU8, p_position, out, whole);
			return out;
		}
		const bool signed_output = p_to == Kind::I32;
		const uint32_t whole = reg(signed_output ? Kind::I32 : Kind::U32);
		emit(signed_output ? Op::U8ToI32 : Op::U8ToU32, p_position, whole, p_reg);
		return convert(whole, p_to, p_position, p_written);
	}
	if (p_to == Kind::F32 || from == Kind::F32) {
		if (p_to == Kind::F32) {
			const uint32_t out = reg(Kind::F32);
			if (from == Kind::Any) {
				emit(Op::FromAny, p_position, out, p_reg, NONE);
			} else if (from == Kind::I64) {
				emit(Op::I64ToF32, p_position, out, p_reg);
			} else {
				emit(Op::F64ToF32, p_position, out, convert(p_reg, Kind::F64, p_position, p_written));
			}
			return out;
		}
		const uint32_t wide = reg(Kind::F64);
		emit(Op::F32ToF64, p_position, wide, p_reg);
		return convert(wide, p_to, p_position, p_written);
	}
	if (p_to == Kind::Any) {
		uint32_t out = reg(Kind::Any);
		emit(Op::ToAny, p_position, out, p_reg);
		return out;
	}
	if (from == Kind::Any) {
		if (p_to == Kind::Obj) {
			// Which class is not known here: typed() checks it where the place's type is.
			refuse(p_position, "converting an untyped value to an object here");
			return default_value(Kind::Obj, p_position);
		}
		uint32_t out = reg(p_to);
		emit(Op::FromAny, p_position, out, p_reg, NONE);
		return out;
	}
	if (from == Kind::I64 || p_to == Kind::I64) {
		return convert_wide(p_reg, p_to, p_position, p_written);
	}
	if (is_engine(from) && p_to == Kind::Str) {
		uint32_t out = reg(Kind::Str);
		emit(Op::HostToStr, p_position, out, p_reg);
		return out;
	}
	if (from == Kind::Ref && p_to == Kind::Bool) {
		// A Godot object as a condition: is there one? (A value is not asked: is Vector2.ZERO false?)
		uint32_t out = reg(Kind::Bool);
		emit(Op::HostPresent, p_position, out, p_reg);
		return out;
	}
	if (from == Kind::Obj && p_to == Kind::Ref) {
		// A script object where one of Godot's objects is wanted: the Godot object it is attached to. One
		// with none (of a class that extends no Godot class) is AS3's TypeError as it runs.
		uint32_t out = reg(Kind::Ref);
		emit(Op::OwnerOf, p_position, out, p_reg);
		return out;
	}
	if (is_engine(from) || is_engine(p_to)) {
		refuse(p_position, is_engine(from) ? "converting a Godot value" : "passing this value where a Godot value is expected");
		return default_value(p_to, p_position);
	}
	return convert_primitive(p_reg, p_to, p_position);
}

namespace {

// The operation that makes a Boolean of a value of the kind (int, uint, Number, String, or an object).
Op to_boolean_op(Kind p_from) {
	switch (p_from) {
		case Kind::I32:
			return Op::I32ToBool;
		case Kind::U32:
			return Op::U32ToBool;
		case Kind::F64:
			return Op::F64ToBool;
		case Kind::Str:
			return Op::StrToBool;
		default:
			return Op::ObjToBool;
	}
}

// The operation that makes a String of a value of the kind (int, uint, Number, Boolean, or an object).
Op to_string_op(Kind p_from) {
	switch (p_from) {
		case Kind::I32:
			return Op::I32ToStr;
		case Kind::U32:
			return Op::U32ToStr;
		case Kind::F64:
			return Op::F64ToStr;
		case Kind::Bool:
			return Op::BoolToStr;
		default:
			return Op::ObjToStr;
	}
}

} // namespace

uint32_t Lowerer::convert_primitive(uint32_t p_reg, Kind p_to, uint32_t p_position) {
	const Kind from = kind_at(p_reg);
	auto via = [&](Op p_op, Kind p_kind, uint32_t p_source) {
		uint32_t out = reg(p_kind);
		emit(p_op, p_position, out, p_source);
		return out;
	};
	auto to_number = [&]() -> uint32_t {
		switch (from) {
			case Kind::I32:
				return via(Op::I32ToF64, Kind::F64, p_reg);
			case Kind::U32:
				return via(Op::U32ToF64, Kind::F64, p_reg);
			case Kind::Bool:
				return via(Op::BoolToF64, Kind::F64, p_reg);
			case Kind::Str:
				return via(Op::StrToF64, Kind::F64, p_reg);
			case Kind::F64:
				return p_reg;
			case Kind::Obj:
				return via(Op::ObjToF64, Kind::F64, p_reg);
			default:
				refuse(p_position, "converting an object to a number");
				return via(Op::ConstF64, Kind::F64, number(0));
		}
	};
	switch (p_to) {
		case Kind::F64:
			return to_number();
		case Kind::I32:
			if (from == Kind::U32) {
				return via(Op::U32ToI32, Kind::I32, p_reg);
			}
			return via(Op::F64ToI32, Kind::I32, to_number());
		case Kind::U32:
			if (from == Kind::I32) {
				return via(Op::I32ToU32, Kind::U32, p_reg);
			}
			return via(Op::F64ToU32, Kind::U32, to_number());
		case Kind::Bool:
			return via(to_boolean_op(from), Kind::Bool, p_reg);
		case Kind::Str:
			return via(to_string_op(from), Kind::Str, p_reg);
		case Kind::Obj:
			refuse(p_position, "converting a value to an object");
			return default_value(Kind::Obj, p_position);
		default:
			return p_reg;
	}
}

bool Lowerer::null_as(const Expr *p_expression) const {
	Kind kind = Kind::Void;
	if (!p_expression || !kind_of(p_expression->type, &kind) || !(is_numeric(kind) || kind == Kind::Bool) || kind == Kind::I64) {
		return false;
	}
	if (p_expression->kind == NodeKind::Conditional) {
		const Conditional *choice = static_cast<const Conditional *>(p_expression);
		return null_as(choice->then_value) || null_as(choice->else_value);
	}
	if (p_expression->kind != NodeKind::Binary) {
		return false;
	}
	const Binary *binary = static_cast<const Binary *>(p_expression);
	switch (binary->op) {
		case TokenKind::KwAs:
			return true;
		case TokenKind::AmpAmp:
		case TokenKind::PipePipe:
			return null_as(binary->left) || null_as(binary->right);
		case TokenKind::Comma:
			return null_as(binary->right);
		default:
			return false;
	}
}

bool Lowerer::value_kind(const Expr *p_expression, Kind *r_kind) const {
	if (!kind_of(p_expression->type, r_kind)) {
		return false;
	}
	if (null_as(p_expression) || gives_nothing(p_expression)) {
		*r_kind = Kind::Any; // What a call that gives nothing is worth: undefined.
	}
	return true;
}

Kind Lowerer::natural(Expr *p_expression) {
	Kind kind = Kind::Void;
	if (p_expression->type && p_expression->type->kind == Type::Null) {
		return Kind::Obj;
	}
	if (null_as(p_expression) || gives_nothing(p_expression)) {
		return Kind::Any;
	}
	if (!kind_of(p_expression->type, &kind)) {
		if (refused_types_.insert(p_expression->type).second) {
			refuse(p_expression->offset, "a value of type '" + compilation_.type_name(p_expression->type) + "'");
		}
		return kind == Kind::Void ? Kind::F64 : kind;
	}
	return kind;
}

bool Lowerer::kind_of_class(const Symbol *p_class, Kind *r_kind) {
	if (p_class == int_) {
		*r_kind = Kind::I32;
	} else if (p_class == uint_) {
		*r_kind = Kind::U32;
	} else if (p_class == int64_) {
		*r_kind = Kind::I64;
	} else if (p_class == byte_) {
		*r_kind = Kind::U8;
	} else if (p_class == float_) {
		*r_kind = Kind::F32;
	} else if (p_class == number_) {
		*r_kind = Kind::F64;
	} else if (p_class == boolean_) {
		*r_kind = Kind::Bool;
	} else if (p_class == string_) {
		*r_kind = Kind::Str;
	} else {
		return false;
	}
	return true;
}

bool Lowerer::is_numeric(Kind p_kind) {
	return p_kind == Kind::U8 || p_kind == Kind::I32 || p_kind == Kind::U32 || is_floating(p_kind) || p_kind == Kind::I64;
}

bool Lowerer::is_integral(Kind p_kind) {
	return p_kind == Kind::U8 || p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::I64;
}

bool Lowerer::is_string_type(const Type *p_type) const {
	return p_type && p_type->kind == Type::Instance && p_type->symbol == string_;
}

} // namespace ir
} // namespace kas3
