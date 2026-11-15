#include "lowerer.h"

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
	if (p_type->kind == Type::Vector) {
		// A vector is an object, if the interpreter can hold what is in it. Untyped elements wait for
		// Array (2c-3).
		Kind element = Kind::Void;
		*r_kind = Kind::Obj;
		return kind_of(p_type->element, &element) && element != Kind::Void && element != Kind::Any;
	}
	if (p_type->kind != Type::Instance || !p_type->symbol) {
		return false;
	}
	const Symbol *symbol = p_type->symbol;
	if (symbol == int_) {
		*r_kind = Kind::I32;
	} else if (symbol == uint_) {
		*r_kind = Kind::U32;
	} else if (symbol == int64_) {
		*r_kind = Kind::I64;
	} else if (symbol == number_) {
		*r_kind = Kind::F64;
	} else if (symbol == boolean_) {
		*r_kind = Kind::Bool;
	} else if (symbol == string_) {
		*r_kind = Kind::Str;
	} else if (symbol == object_) {
		*r_kind = Kind::Any; // An Object may hold any value, numbers too.
	} else if (classes_.count(symbol) || is_error(symbol) || (symbol == function_ && function_)) {
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

bool Lowerer::engine_name(const Symbol *p_class, std::string_view p_member, uint32_t *r_string) {
	std::string name;
	if (!p_class || !godot_name(p_class->name, p_member, &name)) {
		return false;
	}
	*r_string = string(name);
	return true;
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
	if (p_class && p_class == function_) {
		// Function: the built-in class of function values (FUNCTION_FIELDS in ir.h).
		Class klass;
		klass.name = "Function";
		klass.fields = { { "function", Kind::I32 }, { "first", Kind::Obj } };
		module_.classes.push_back(std::move(klass));
		module_.function_class = *r_index = (uint32_t)module_.classes.size() - 1;
		classes_[p_class] = module_.function_class;
		return true;
	}
	if (!p_class || !p_class->unit || !p_class->unit->is_builtin || !is_error(p_class)) {
		return false;
	}
	// Error and its built-in subclasses: message, name, errorID and the stack trace (ERROR_FIELDS in ir.h),
	// and a constructor that stores the message and the id, and takes the trace where it is made, as
	// Flash does.
	uint32_t base = NONE;
	if (p_class != error_ && !class_index(p_class->klass->base, &base)) {
		return false;
	}
	Class klass;
	klass.name = class_name(p_class);
	klass.base = base;
	klass.fields = { { "message", Kind::Str }, { "name", Kind::Str }, { "errorID", Kind::I32 }, { "stack", Kind::Str } };
	Function constructor;
	constructor.name = klass.name;
	constructor.owner = (uint32_t)module_.classes.size();
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
	op(Op::ConstStr, 3, string(std::string(p_class->name)), 0);
	op(Op::SetField, 0, ERROR_NAME, 3);
	op(Op::ErrorStack, 4, 0, 0);
	op(Op::SetField, 0, ERROR_STACK, 4);
	op(Op::ReturnVoid, 0, 0, 0);
	klass.constructor = (uint32_t)module_.functions.size();
	module_.functions.push_back(std::move(constructor));
	module_.classes.push_back(std::move(klass));
	uint32_t index = (uint32_t)module_.classes.size() - 1;
	classes_[p_class] = index;
	if (p_class->klass->constructor) {
		functions_[p_class->klass->constructor] = module_.classes[index].constructor;
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

uint32_t Lowerer::default_value(Kind p_kind, uint32_t p_position) {
	uint32_t out = reg(p_kind == Kind::Void ? Kind::F64 : p_kind);
	switch (p_kind) {
		case Kind::I32:
			emit(Op::ConstI32, p_position, out, 0);
			break;
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
			switch (from) {
				case Kind::I32:
					return via(Op::I32ToBool, Kind::Bool, p_reg);
				case Kind::U32:
					return via(Op::U32ToBool, Kind::Bool, p_reg);
				case Kind::F64:
					return via(Op::F64ToBool, Kind::Bool, p_reg);
				case Kind::Str:
					return via(Op::StrToBool, Kind::Bool, p_reg);
				default:
					return via(Op::ObjToBool, Kind::Bool, p_reg);
			}
		case Kind::Str:
			switch (from) {
				case Kind::I32:
					return via(Op::I32ToStr, Kind::Str, p_reg);
				case Kind::U32:
					return via(Op::U32ToStr, Kind::Str, p_reg);
				case Kind::F64:
					return via(Op::F64ToStr, Kind::Str, p_reg);
				case Kind::Bool:
					return via(Op::BoolToStr, Kind::Str, p_reg);
				default:
					return via(Op::ObjToStr, Kind::Str, p_reg);
			}
		case Kind::Obj:
			refuse(p_position, "converting a value to an object");
			return default_value(Kind::Obj, p_position);
		default:
			return p_reg;
	}
}

Kind Lowerer::natural(Expr *p_expression) {
	Kind kind = Kind::Void;
	if (p_expression->type && p_expression->type->kind == Type::Null) {
		return Kind::Obj;
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
	return p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::F64 || p_kind == Kind::I64;
}

bool Lowerer::is_integral(Kind p_kind) {
	return p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::I64;
}

bool Lowerer::is_string_type(const Type *p_type) const {
	return p_type && p_type->kind == Type::Instance && p_type->symbol == string_;
}

} // namespace ir
} // namespace kas3
