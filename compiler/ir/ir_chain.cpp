// A class's constructor chain (ir.h, ConstructorChain): whether it can be run again on a throwaway object
// with the same arguments, why not, and which constants it reads.
#include "ir.h"

#include "ir_reach.h"

#include <algorithm>
#include <unordered_set>

namespace kas3 {
namespace ir {

namespace {

// Operations that work a value out of the values they are given and nothing else: run again with the same
// operands they give the same, and nothing outside the function's registers can tell that they ran. The
// list is of what is known to be so; an operation added to the table is refused until it is added here.
constexpr bool repeatable(Op p_op) {
	switch (p_op) {
		case Op::Stmt: case Op::ConstI32: case Op::ConstU32: case Op::ConstF64: case Op::ConstBool: case Op::ConstStr: case Op::ConstNull:
		case Op::I32ToU8: case Op::U8ToI32: case Op::U32ToU8: case Op::U8ToU32:
		case Op::I64ToF32: case Op::F32ToF64: case Op::F64ToF32: case Op::AddF32: case Op::SubF32: case Op::MulF32: case Op::DivF32: case Op::ModF32:
		case Op::Move: case Op::I32ToF64: case Op::U32ToF64: case Op::BoolToF64: case Op::StrToF64: case Op::F64ToI32: case Op::F64ToU32:
		case Op::I32ToU32: case Op::U32ToI32: case Op::I32ToBool: case Op::U32ToBool: case Op::F64ToBool: case Op::StrToBool:
		case Op::ObjToBool: case Op::I32ToStr: case Op::U32ToStr: case Op::F64ToStr: case Op::BoolToStr: case Op::StrToStr:
		case Op::AddF64: case Op::SubF64: case Op::MulF64: case Op::DivF64: case Op::ModF64: case Op::NegF64:
		case Op::BitAnd: case Op::BitOr: case Op::BitXor: case Op::Shl: case Op::Shr: case Op::UShr: case Op::BitNot:
		case Op::LtF64: case Op::LeF64: case Op::EqF64: case Op::LtStr: case Op::LeStr: case Op::EqStr: case Op::EqBool: case Op::EqRef:
		case Op::Not: case Op::Concat: case Op::StrLength: case Op::StrCall: case Op::NumText:
		case Op::MathUnary: case Op::Atan2: case Op::Pow: case Op::MinF64: case Op::MaxF64:
		case Op::Jump: case Op::JumpIf: case Op::JumpIfNot: case Op::GetField: case Op::SetField: case Op::GetGlobal:
		// InitClass: a class used for the first time is given its static fields here as it would be
		// anywhere, and one already used is left alone.
		case Op::InitClass: case Op::NullCheck: case Op::ArgMissing: case Op::Call: case Op::CallVirtual:
		case Op::IsClass: case Op::AsClass: case Op::ConstI64:
		case Op::AddI64: case Op::SubI64: case Op::MulI64: case Op::ModI64: case Op::NegI64:
		case Op::BitAndI64: case Op::BitOrI64: case Op::BitXorI64: case Op::BitNotI64: case Op::ShlI64: case Op::ShrI64: case Op::UShrI64:
		case Op::LtI64: case Op::LeI64: case Op::EqI64: case Op::CmpI64F64: case Op::CmpI64Str:
		case Op::I32ToI64: case Op::U32ToI64: case Op::I64ToBool: case Op::I64ToStr:
		case Op::I64ToI32: case Op::I64ToU32: case Op::I64ToF64: case Op::F64ToI64:
		case Op::I64WrapI32: case Op::I64WrapU32: case Op::I64RoundF64: case Op::F64ClampI64: case Op::StrToI64: case Op::StrToI64Checked:
		case Op::AnyConst: case Op::ToAny: case Op::Return: case Op::ReturnVoid:
			return true;
		default:
			return false;
	}
}

// What an operation that is not repeatable does, in words; nothing for one with no words of its own.
constexpr const char *does(Op p_op) {
	switch (p_op) {
		case Op::New: case Op::VecNew: case Op::ArrayNew: case Op::TableNew: case Op::VecFrom: case Op::ClassNew:
		case Op::MakeFunction: case Op::MakeMethod: case Op::MakeInterfaceMethod:
			return "makes an object";
		case Op::HostNew: case Op::HostCall: case Op::HostGet: case Op::HostSet: case Op::HostOperate: case Op::HostIndexGet:
		case Op::HostIndexSet: case Op::HostBind: case Op::HostMember: case Op::HostToStr: case Op::HostPresent: case Op::HostIsClass:
		case Op::HostAsClass: case Op::HostDefault: case Op::HostSingleton: case Op::HostConstant: case Op::HostCallStatic:
		case Op::HostCollection: case Op::OwnerOf: case Op::FunctionToCallable: case Op::HostBindMethod:
			return "asks Godot";
		case Op::Random:
			return "asks for a random number";
		case Op::DateCall:
			return "asks for the date";
		case Op::Trace:
			return "prints";
		case Op::CallTypedFunction:
		case Op::CallFunction:
			return "calls a function value";
		case Op::CallInterface:
			return "calls a method of another object";
		default:
			return nullptr;
	}
}

bool plain_value(Kind p_kind) {
	return p_kind == Kind::I32 || p_kind == Kind::U32 || is_floating(p_kind) || p_kind == Kind::Bool || p_kind == Kind::Str || p_kind == Kind::I64;
}

// Whether the instruction uses the object the function runs on (register 0) as anything but the holder of
// a field read or written, or the object one of its own methods or constructors is called on.
bool hands_itself_on(const Module &p_module, const Function &p_function, const Instruction &p_in) {
	const char *shape = op_shape(p_in.op);
	const uint32_t values[3] = { p_in.a, p_in.b, p_in.c };
	for (int n = 0; n < 3; n++) {
		if ((shape[n] == 'R' || shape[n] == 'r') && values[n] == 0) {
			const bool holder = (p_in.op == Op::GetField && n == 1) || (p_in.op == Op::SetField && n == 0) || (p_in.op == Op::NullCheck && n == 0);
			if (!holder) {
				return true;
			}
		}
		if (shape[n] != 'A' || values[n] >= p_function.argument_lists.size()) {
			continue;
		}
		const std::vector<uint32_t> &list = p_function.argument_lists[values[n]];
		const bool method = p_in.op == Op::CallVirtual ||
				(p_in.op == Op::Call && p_in.b < p_module.functions.size() && !p_module.functions[p_in.b].is_static);
		for (size_t i = 0; i < list.size(); i++) {
			if (list[i] == 0 && !(i == 0 && method)) {
				return true;
			}
		}
	}
	return false;
}

// Why one instruction can't be done again, in words that follow the function's name; nothing if it can.
// A call it makes on the object itself, or of a static function, is left to be followed.
std::optional<std::string> refused(const Module &p_module, const Function &p_function, const Instruction &p_in) {
	const bool own = !p_function.is_static;
	if (own && hands_itself_on(p_module, p_function, p_in)) {
		return "hands the object on";
	}
	const auto global = [&](uint32_t p_global) { return p_global < p_module.globals.size() ? p_module.globals[p_global].name : std::string("?"); };
	switch (p_in.op) {
		case Op::GetField:
			return own && p_in.b == 0 ? std::nullopt : std::optional<std::string>("reads a field of another object");
		case Op::SetField:
			return own && p_in.a == 0 ? std::nullopt : std::optional<std::string>("writes a field of another object");
		case Op::SetGlobal:
			return "writes the static variable " + global(p_in.a);
		case Op::GetGlobal:
			if (p_in.b >= p_module.globals.size() || !p_module.globals[p_in.b].is_const) {
				return "reads the static variable " + global(p_in.b);
			}
			return plain_value(p_module.globals[p_in.b].kind) ? std::nullopt
															  : std::optional<std::string>("reads the constant " + global(p_in.b) + ", which holds an object");
		case Op::Call:
			if (p_in.b >= p_module.functions.size() || (!p_module.functions[p_in.b].is_static && !on_itself(p_function, p_in))) {
				return "calls a method of another object";
			}
			return std::nullopt;
		case Op::CallVirtual:
			return on_itself(p_function, p_in) ? std::nullopt : std::optional<std::string>("calls a method of another object");
		case Op::StrCall:
			// split() makes an Array and may run a delimiter's toString().
			if (reads_text_only(static_cast<StringFunction>(p_in.b))) {
				return std::nullopt;
			}
			break;
		default:
			if (repeatable(p_in.op)) {
				return std::nullopt;
			}
			break;
	}
	const char *words = does(p_in.op);
	return words ? std::string(words) : "does what can't be done again (" + std::string(op_name(p_in.op)) + ")";
}

// The first reason the chain from the class's constructor can't be run again, each function named as the
// module names it; nothing if every function it reaches can.
std::optional<std::string> chain_refused(const Module &p_module, uint32_t p_class) {
	std::vector<uint32_t> pending = { p_module.classes[p_class].constructor };
	std::unordered_set<uint32_t> seen;
	for (size_t next = 0; next < pending.size(); next++) {
		if (pending[next] >= p_module.functions.size() || !seen.insert(pending[next]).second) {
			continue;
		}
		const Function &function = p_module.functions[pending[next]];
		if (function.rest) {
			return function.name + "() takes any number of arguments"; // They come as an Array, made for the call.
		}
		for (uint32_t at = 0; at < function.code.size(); at++) {
			if (std::optional<std::string> why = refused(p_module, function, function.code[at])) {
				return function.name + "() " + *why;
			}
			// Only calls on the object itself and of static functions are left: each runs on this class's object.
			each_called(p_module, function, at, p_class, [&](uint32_t p_called, uint32_t, bool) { pending.push_back(p_called); });
		}
	}
	return std::nullopt;
}

// The class, if it is one whose objects a constructor makes.
const Class *constructed_class(const Module &p_module, uint32_t p_class) {
	const Class *klass = p_class < p_module.classes.size() ? &p_module.classes[p_class] : nullptr;
	return klass && !klass->is_interface && klass->element == Kind::Void && !klass->table && klass->constructor != NONE ? klass : nullptr;
}

} // namespace

std::optional<std::string> constructor_refusal(const Module &p_module, uint32_t p_class) {
	const Class *klass = constructed_class(p_module, p_class);
	if (!klass) {
		return "it is not a class with a constructor";
	}
	if (klass->built_in) {
		return "it is a class of the library";
	}
	if (!klass->native_base.empty()) {
		return "it extends Godot's " + klass->native_base;
	}
	return chain_refused(p_module, p_class);
}

ConstructorChain constructor_chain(const Module &p_module, uint32_t p_class) {
	ConstructorChain chain;
	chain.refusal = constructor_refusal(p_module, p_class);
	const Class *klass = constructed_class(p_module, p_class);
	if (!klass) {
		return chain;
	}
	std::vector<uint32_t> reached;
	chain.text = reached_text(p_module, { { klass->constructor, p_class } }, &reached);
	for (uint32_t index : reached) {
		for (const Instruction &in : p_module.functions[index].code) {
			if (in.op == Op::GetGlobal && in.b < p_module.globals.size() && p_module.globals[in.b].is_const &&
					std::find(chain.constants.begin(), chain.constants.end(), p_module.globals[in.b].name) == chain.constants.end()) {
				chain.constants.push_back(p_module.globals[in.b].name);
			}
		}
	}
	return chain;
}

} // namespace ir
} // namespace kas3
