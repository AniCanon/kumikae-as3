// The kinds of register and the instruction set: the table of operations, each with the shape of its three
// operands, and one instruction.
#pragma once

#include <cstdint>

namespace kas3 {
namespace ir {

// What a register, a field or a parameter holds. There are no tags at run time: the instruction says what
// it is working on.
enum class Kind : uint8_t {
	Void,
	I32, // int
	U32, // uint
	F64, // Number
	Bool,
	Str, // String, or null
	Obj, // An object of a class, or null
	Val, // A value of one of Godot's value types (Vector2, Color, Signal, ...): copied when assigned
	Ref, // A Godot object (a node, a resource), or null
	I64, // Int64: Godot's own integer
	Any, // `*` or `Object`: any value, with what it is beside it (vm/any.h)
};

const char *kind_name(Kind p_kind);
inline bool is_reference(Kind p_kind) {
	return p_kind == Kind::Str || p_kind == Kind::Obj;
}
// Held by the engine: the interpreter keeps a handle that the host gave it.
inline bool is_engine(Kind p_kind) {
	return p_kind == Kind::Val || p_kind == Kind::Ref;
}
// Holds something that must be let go: a counted string or object, a handle, or an untyped value.
inline bool is_held(Kind p_kind) {
	return is_reference(p_kind) || is_engine(p_kind) || p_kind == Kind::Any;
}

// How an instruction uses its three operands.
//   R  a register          r  a register, or NONE
//   I  a 32-bit integer    N  an index into the module's numbers     S  into its strings
//   W  an index into the module's wide numbers (Int64 constants)
//   L  an instruction index in the same function                     A  into the function's argument lists
//   F  a function index    C  a class index    D  a field slot       G  a global slot    V  a method-table slot
//   M  an interface method: an index into the module's interface_methods
//   -  unused
#define KAS3_IR_OPS(X) \
	X(Stmt, "---") /* A statement starts here: where a debugger may stop. */ \
	X(ConstI32, "RI-") X(ConstU32, "RI-") X(ConstF64, "RN-") X(ConstBool, "RI-") X(ConstStr, "RS-") X(ConstNull, "R--") \
	X(Move, "RR-") \
	X(I32ToF64, "RR-") X(U32ToF64, "RR-") X(BoolToF64, "RR-") X(StrToF64, "RR-") \
	X(ObjToF64, "RR-") /* An object as a Number, as ECMAScript makes one: a Date its time, any other its text. */ \
	X(F64ToI32, "RR-") X(F64ToU32, "RR-") X(I32ToU32, "RR-") X(U32ToI32, "RR-") \
	X(I32ToBool, "RR-") X(U32ToBool, "RR-") X(F64ToBool, "RR-") X(StrToBool, "RR-") X(ObjToBool, "RR-") \
	X(I32ToStr, "RR-") X(U32ToStr, "RR-") X(F64ToStr, "RR-") X(BoolToStr, "RR-") X(ObjToStr, "RR-") X(StrToStr, "RR-") \
	X(AddF64, "RRR") X(SubF64, "RRR") X(MulF64, "RRR") X(DivF64, "RRR") X(ModF64, "RRR") X(NegF64, "RR-") \
	X(BitAnd, "RRR") X(BitOr, "RRR") X(BitXor, "RRR") X(Shl, "RRR") X(Shr, "RRR") X(UShr, "RRR") X(BitNot, "RR-") \
	X(LtF64, "RRR") X(LeF64, "RRR") X(EqF64, "RRR") X(LtStr, "RRR") X(LeStr, "RRR") X(EqStr, "RRR") \
	X(EqBool, "RRR") X(EqRef, "RRR") X(Not, "RR-") \
	X(Concat, "RRR") X(StrLength, "RR-") \
	/* String's methods and the top-level functions on text: out, which (a StringFunction), [its arguments]. \
	   A number as text: out, which (a NumberFormat), [the number, the radix or digits as a Number]. */ \
	X(StrCall, "RIA") X(NumText, "RIA") \
	/* RegExp and Date: out, which (a RegExpFunction, or a date_code()), [its arguments]. Either may run \
	   the program: a replacement function, or a value's text. A missing RegExp or Date is TypeError #1009. */ \
	X(RegExpCall, "RIA") X(DateCall, "RIA") \
	/* JSON: out, which (a JsonFunction), [its arguments]. It may run the program: a reviver, a replacer, \
	   a toJSON() or a getter. */ \
	X(JsonCall, "RIA") \
	/* Math. MathUnary: out, the Number, and which function (a MathFunction). */ \
	X(MathUnary, "RRI") X(Atan2, "RRR") X(Pow, "RRR") X(MinF64, "RRR") X(MaxF64, "RRR") X(Random, "R--") \
	X(Jump, "L--") X(JumpIf, "RL-") X(JumpIfNot, "RL-") \
	X(GetField, "RRD") X(SetField, "RDR") X(GetGlobal, "RG-") X(SetGlobal, "GR-") \
	X(InitClass, "C--") /* The class is used: give its static fields their values, if this is the first time. */ \
	X(NullCheck, "R--") X(ArgMissing, "RR-") /* a: was the parameter in register b left out by the caller? */ \
	X(AnyNullCheck, "R--") /* An untyped value that is null (TypeError #1009) or undefined (#1010) has no members. */ \
	X(New, "RC-") X(Call, "rFA") X(CallVirtual, "rVA") \
	/* A method of an interface, found in the table of the class the object (the first argument) is of. */ \
	X(CallInterface, "rMA") \
	/* A Vector.<T>. VecNew: out, its class, [length, fixed]. An index is an int, a uint or a Number, \
	   read as its register is declared; what is read or written is of the class's element kind. */ \
	X(VecNew, "RCA") X(ArrayNew, "RCA") X(VecGet, "RRR") X(VecSet, "RRR") \
	X(VecLength, "RR-") X(VecSetLength, "RR-") X(VecFixed, "RR-") X(VecSetFixed, "RR-") \
	/* A method of a vector: out, which one (a VectorMethod), [the vector, then what the method takes]. */ \
	X(VecCall, "rIA") \
	X(IsClass, "RRC") X(AsClass, "RRC") \
	/* The engine. Names are Godot's own. In HostCall the first argument is what the method is called on. */ \
	/* Int64. The constant is an index into the module's wide numbers. Arithmetic wraps at 64 bits. */ \
	X(ConstI64, "RW-") \
	X(AddI64, "RRR") X(SubI64, "RRR") X(MulI64, "RRR") X(ModI64, "RRR") X(NegI64, "RR-") \
	X(BitAndI64, "RRR") X(BitOrI64, "RRR") X(BitXorI64, "RRR") X(BitNotI64, "RR-") X(ShlI64, "RRR") X(ShrI64, "RRR") X(UShrI64, "RRR") \
	X(LtI64, "RRR") X(LeI64, "RRR") X(EqI64, "RRR") \
	/* An Int64 against a Number, exactly: -1, 0 or 1 as a Number, or NaN when the Number is NaN. */ \
	X(CmpI64F64, "RRR") X(CmpI64Str, "RRR") \
	X(I32ToI64, "RR-") X(U32ToI64, "RR-") X(I64ToBool, "RR-") X(I64ToStr, "RR-") \
	/* Conversions the program did not write: a RangeError if the value does not fit. */ \
	X(I64ToI32, "RR-") X(I64ToU32, "RR-") X(I64ToF64, "RR-") X(F64ToI64, "RR-") \
	/* Conversions the program wrote, int(x) and the rest: AS3's own, which cut, round or hold at the end. */ \
	X(I64WrapI32, "RR-") X(I64WrapU32, "RR-") X(I64RoundF64, "RR-") X(F64ClampI64, "RR-") X(StrToI64, "RR-") X(StrToI64Checked, "RR-") \
	X(HostNew, "RSA") X(HostCall, "rSA") X(HostGet, "RRS") X(HostSet, "RSR") \
	/* An operator on Godot values: the operator as AS3 writes it ("+", "=="), and one or two operands. */ \
	X(HostOperate, "RSA") \
	X(HostIndexGet, "RRR") X(HostIndexSet, "RRR") /* value[key]: out, value, key; and value, key, new element. */ \
	/* A method bound to its object, as a function Godot can call: out, the object, and the function, or \
	   with the top bit set a method-table slot to look up in the object's own class. */ \
	X(HostBind, "RRI") \
	X(HostMember, "RRR") /* object[name], the name being AS3's and known only now: out, object, name. */ \
	X(HostToStr, "RR-") /* Godot's text for one of its values or objects; "null" for a missing one. */ \
	X(HostPresent, "RR-") /* Is there an engine value here? Not if it is null, or an object Godot has freed. */ \
	/* `is` and `as` with one of Godot's classes: out, the object, Godot's name for the class. HostAsClass \
	   gives the object, or null if it is not of that class. */ \
	X(HostIsClass, "RRS") X(HostAsClass, "RRS") \
	X(HostDefault, "RS-") /* The value of a default Godot declares, from Godot's own text for it. */ \
	X(HostSingleton, "RS-") /* One of the engine's singletons (Input), by Godot's name for it. */ \
	X(HostConstant, "RSS") /* A constant Godot keeps: out, the type, the constant (Vector2, ZERO). */ \
	X(HostCallStatic, "rSA") /* A method called on no object: "Vector2.from_angle", and its arguments. */ \
	/* A vector as one of Godot's collections: out, the vector, Godot's name for the type wanted. */ \
	X(HostCollection, "RRS") \
	/* The untyped value. AnyConst: 0 undefined, 1 null. ToAny takes any kind; FromAny converts to the \
	   out register's kind as a store does (AS3's coercion), checking a class (an object), or Godot's \
	   name for a type (a Godot object) when the third operand is not NONE; into an untyped register, \
	   any third operand but NONE means an `Object`, where undefined is null. AnyText is String(x). \
	   AnyIs: the third operand is a class, or one of the ANY_IS_ codes. AnyIsGodot: whether it holds an \
	   object of the Godot class of that name, or of one that extends it. */ \
	X(AnyConst, "RI-") X(ToAny, "RR-") X(FromAny, "RRI") X(AnyText, "RR-") X(AnyTypeof, "RR-") \
	X(AnyEq, "RRR") X(AnyStrictEq, "RRR") X(AnyAdd, "RRR") X(AnyLt, "RRR") X(AnyLe, "RRR") X(AnyGt, "RRR") X(AnyGe, "RRR") \
	X(AnyIs, "RRI") X(AnyIsGodot, "RRS") \
	X(Return, "R--") X(ReturnVoid, "---") \
	X(Throw, "R--") /* An untyped value: AS3 throws any value. */ \
	X(OwnerOf, "RR-") /* A script object as the Godot object it is attached to; null for null. */ \
	X(ErrorStack, "RR-") /* What an Error's getStackTrace() gives, for the error in b being made now. */ \
	/* Function values. MakeFunction: out, the function, what it is called with first (a register, or \
	   NONE). MakeMethod: out, a method-table slot, the object: its class's method, bound to it. \
	   CallFunction: out (untyped, or NONE), the function value (a Function or an untyped value), its \
	   arguments, each untyped. */ \
	X(MakeFunction, "RFr") X(MakeMethod, "RVR") X(CallFunction, "rRA") \
	X(MakeInterfaceMethod, "RMR") /* out, an interface method, the object: its class's, bound to it. */ \
	X(FunctionToCallable, "RR-") /* A function value as a Godot Callable that calls it; null for null. */ \
	/* A vector method's callback and the object to call it on (both untyped): a method with an object \
	   other than null is AS3's TypeError #1510. */ \
	X(CallbackThis, "RR-") \
	/* f.apply(thisArg, array): out (untyped, or NONE), the function value, the Array (untyped: null or \
	   undefined is no arguments, anything else not an Array is AS3's TypeError #1116). f.length: out \
	   (int), the function value: how many parameters it declares, the rest parameter not counted. */ \
	X(ApplyFunction, "rRR") X(FunctionLength, "RR-") \
	X(ObjectText, "RR-") /* Object's own toString(), or Error's, whatever the class says: what super.toString() reaches. */ \
	/* Objects, Dictionaries (vm::Table) and properties by name. TableNew: out (an object), its class (Object's \
	   or Dictionary's), whether its keys are to be weak (a Boolean register, or NONE). The rest take what \
	   holds the property as an untyped or an object register, and the key as a String or untyped one: \
	   DynGet out (untyped), holder, key; DynSet holder, key, value (untyped); DynDelete \
	   and DynIn (`in`: its own, or its prototype's): out (Boolean), holder, key. DynCall: out (untyped, or NONE), \
	   the holder, [the key, then the arguments, untyped], the property called once the arguments are \
	   worked out. A loop over the properties: DynKeys out (the keys there are now, or null), the holder; \
	   DynNext out (Boolean: one more), the cursor (a Number, moved on), [holder, keys]; DynKey out \
	   (untyped: the one DynNext found), the cursor, [holder, keys]. */ \
	X(TableNew, "RCr") X(DynGet, "RRR") X(DynSet, "RRR") X(DynDelete, "RRR") X(DynIn, "RRR") \
	X(DynCall, "rRA") X(DynKeys, "RR-") X(DynNext, "RRA") X(DynKey, "RRA") \
	X(Rethrow, "R--") /* The error a `finally` caught, thrown again with the trace it had. */ \
	X(Trace, "A--") \
	/* Vector.<T>(x): out (an object), x (untyped), the vector's class. x itself if it is of that class; a \
	   new vector of an Array's or another Vector's elements, each converted as a store into the vector \
	   converts it; AS3's TypeError #1034 for anything else, null included. */ \
	X(VecFrom, "RRC")

enum class Op : uint8_t {
#define X(name, shape) name,
	KAS3_IR_OPS(X)
#undef X
};

const char *op_name(Op p_op);
const char *op_shape(Op p_op); // Three letters from the table above.

const uint32_t NONE = 0xFFFFFFFFu;
// AnyIs with a type that is not a class: what AS3 asks of the value itself.
const uint32_t ANY_IS_INT = 0xFFFFFFF0u;
const uint32_t ANY_IS_UINT = 0xFFFFFFF1u;
const uint32_t ANY_IS_NUMBER = 0xFFFFFFF2u;
const uint32_t ANY_IS_STRING = 0xFFFFFFF3u;
const uint32_t ANY_IS_BOOLEAN = 0xFFFFFFF4u;
const uint32_t BIND_SLOT = 0x80000000u; // In HostBind: the operand is a method-table slot.

struct Instruction {
	Op op = Op::Stmt;
	uint32_t a = 0;
	uint32_t b = 0;
	uint32_t c = 0;
	uint32_t position = 0; // A byte offset in the function's file.
};

} // namespace ir
} // namespace kas3
