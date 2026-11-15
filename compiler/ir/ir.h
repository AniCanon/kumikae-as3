// The typed IR: what a checked program means, as plain data. Both backends read it and nothing else.
// Everything refers to everything else by index, never by pointer and never to the syntax tree, so that a
// module can be written to a file and run without the front end.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
//   -  unused
#define KAS3_IR_OPS(X) \
	X(Stmt, "---") /* A statement starts here: where a debugger may stop. */ \
	X(ConstI32, "RI-") X(ConstU32, "RI-") X(ConstF64, "RN-") X(ConstBool, "RI-") X(ConstStr, "RS-") X(ConstNull, "R--") \
	X(Move, "RR-") \
	X(I32ToF64, "RR-") X(U32ToF64, "RR-") X(BoolToF64, "RR-") X(StrToF64, "RR-") \
	X(F64ToI32, "RR-") X(F64ToU32, "RR-") X(I32ToU32, "RR-") X(U32ToI32, "RR-") \
	X(I32ToBool, "RR-") X(U32ToBool, "RR-") X(F64ToBool, "RR-") X(StrToBool, "RR-") X(ObjToBool, "RR-") \
	X(I32ToStr, "RR-") X(U32ToStr, "RR-") X(F64ToStr, "RR-") X(BoolToStr, "RR-") X(ObjToStr, "RR-") X(StrToStr, "RR-") \
	X(AddF64, "RRR") X(SubF64, "RRR") X(MulF64, "RRR") X(DivF64, "RRR") X(ModF64, "RRR") X(NegF64, "RR-") \
	X(BitAnd, "RRR") X(BitOr, "RRR") X(BitXor, "RRR") X(Shl, "RRR") X(Shr, "RRR") X(UShr, "RRR") X(BitNot, "RR-") \
	X(LtF64, "RRR") X(LeF64, "RRR") X(EqF64, "RRR") X(LtStr, "RRR") X(LeStr, "RRR") X(EqStr, "RRR") \
	X(EqBool, "RRR") X(EqRef, "RRR") X(Not, "RR-") \
	X(Concat, "RRR") X(StrLength, "RR-") \
	/* Math. MathUnary: out, the Number, and which function (a MathFunction). */ \
	X(MathUnary, "RRI") X(Atan2, "RRR") X(Pow, "RRR") X(MinF64, "RRR") X(MaxF64, "RRR") X(Random, "R--") \
	X(Jump, "L--") X(JumpIf, "RL-") X(JumpIfNot, "RL-") \
	X(GetField, "RRD") X(SetField, "RDR") X(GetGlobal, "RG-") X(SetGlobal, "GR-") \
	X(InitClass, "C--") /* The class is used: give its static fields their values, if this is the first time. */ \
	X(NullCheck, "R--") X(ArgMissing, "RR-") /* a: was the parameter in register b left out by the caller? */ \
	X(New, "RC-") X(Call, "rFA") X(CallVirtual, "rVA") \
	/* A Vector.<T>. VecNew: out, its class, [length, fixed]. An index is an int, a uint or a Number, \
	   read as its register is declared; what is read or written is of the class's element kind. */ \
	X(VecNew, "RCA") X(VecGet, "RRR") X(VecSet, "RRR") \
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
	X(Rethrow, "R--") /* The error a `finally` caught, thrown again with the trace it had. */ \
	X(Trace, "A--")

enum class Op : uint8_t {
#define X(name, shape) name,
	KAS3_IR_OPS(X)
#undef X
};

// The functions of Math that take one Number, in MathUnary's third operand.
enum class MathFunction : uint8_t { Abs, Acos, Asin, Atan, Ceil, Cos, Exp, Floor, Log, Round, Sin, Sqrt, Tan };

// The fields of Error and of its built-in subclasses, which the interpreter fills in for an error it raises.
constexpr uint32_t ERROR_MESSAGE = 0, ERROR_NAME = 1, ERROR_ID = 2, ERROR_STACK = 3;

// A vector's methods, in VecCall's second operand. After the vector, the arguments are:
//   Push, Unshift   elements                         -> the new length (uint)
//   Pop, Shift      nothing                          -> the element
//   IndexOf, LastIndexOf   an element, where to start (int)   -> the index (int), or -1
//   Reverse         nothing                          -> the vector itself
//   Slice           start, end (int)                 -> a new vector
//   Concat          vectors of the same class        -> a new vector
//   Join            the separator (String)           -> a String
//   InsertAt        index (int), an element          -> nothing
//   RemoveAt        index (int)                      -> the element
//   Splice          start (int), how many (uint), elements to put in   -> a new vector of what was removed
enum class VectorMethod : uint8_t { Push, Unshift, Pop, Shift, IndexOf, LastIndexOf, Reverse, Slice, Concat, Join, InsertAt, RemoveAt, Splice };

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

// A local variable or parameter by name, for the debugger.
struct Local {
	std::string name;
	uint32_t reg = 0;
};

// Where an error raised inside [start, end) of a function's code is caught: the error goes to register
// `reg` and the function carries on at `target`. `type` is what the catch takes: a class, one of the
// ANY_IS_ codes, or NONE for anything. A function's handlers are in order, the innermost first.
struct Handler {
	uint32_t start = 0;
	uint32_t end = 0;
	uint32_t target = 0;
	uint32_t reg = 0;
	uint32_t type = NONE;
	// The catch of a `finally`, which throws the error again: the trace it had is kept for that.
	bool rethrows = false;
};

struct Function {
	std::string name; // "Main/update", "Main" for a constructor, "helper" for a package function.
	uint32_t file = 0; // An index into Module::files.
	uint32_t position = 0; // Of its name.
	uint32_t owner = NONE; // The class, for a method or constructor.
	bool is_static = false; // No receiver: a static method or a package function.
	// The first registers are the parameters; an instance method's first parameter is `this`.
	uint32_t parameters = 0;
	uint32_t required = 0; // How many of them have no default: a caller must pass at least these.
	Kind result = Kind::Void;
	// For each register that takes a Godot value or object from outside (a parameter, or the result of a
	// request to the engine): Godot's name for its type ("Vector2", "CharacterBody2D"), by register. Empty
	// for the others, and the vector may be shorter than the registers. A host checks what arrives against it.
	std::vector<std::string> engine_types;
	std::vector<Kind> registers;
	std::vector<Instruction> code;
	std::vector<std::vector<uint32_t>> argument_lists;
	std::vector<Local> locals;
	std::vector<Handler> handlers;
};

struct Field {
	std::string name;
	Kind kind = Kind::Void;
	// For an object: the class it is declared to hold (a class of the program, or a vector's), or NONE
	// where that is not known. A reload keeps what a field held only if it is still of that class.
	uint32_t klass = NONE;
	bool starts_null = false; // An untyped field declared `Object`: it starts null, a `*` undefined.
};

struct Class {
	std::string name; // "game.Player"
	uint32_t base = NONE;
	// For a Vector.<T>, what its elements are; Void for every other class. A vector class has no fields,
	// no methods and no base, and its objects are vm::Vector.
	Kind element = Kind::Void;
	std::vector<Field> fields; // The base's first: a field keeps its slot in every subclass.
	std::vector<uint32_t> methods; // The method table: function indices, the base's slots first.
	uint32_t constructor = NONE;
	uint32_t static_init = NONE; // The function that gives the class's static fields their values, if any.
	uint32_t to_string = NONE; // The class's toString(), its own or inherited: what converting to a String calls.
	// For a script class, one that extends a Godot class directly or through other script classes: Godot's
	// name for that class. Field 0 then holds the Godot object the script is attached to.
	std::string native_base;
	// The engine's virtual methods the class answers: Godot's name ("_physics_process") and the function.
	std::vector<std::pair<std::string, uint32_t>> virtuals;
};

struct Global {
	std::string name; // "Main.count"
	Kind kind = Kind::Void;
	uint32_t klass = NONE; // As for a field.
	bool starts_null = false; // As for a field.
};

struct Module {
	std::vector<std::string> files; // Paths, for stack traces.
	std::vector<std::vector<uint32_t>> lines; // Per file: the offset each line starts at.
	std::vector<Class> classes;
	std::vector<Function> functions;
	std::vector<Global> globals; // Static fields and package-level variables.
	std::vector<std::string> strings;
	std::vector<double> numbers;
	std::vector<int64_t> wides; // Int64 constants.
	// Functions run once, in order, before anything else: package-level variables. (A class's static fields
	// are set on the class's first use: Class::static_init.)
	std::vector<uint32_t> initializers;
	uint32_t entry_class = NONE; // Constructed with no arguments to start the program.
	uint32_t error_class = NONE; // Error, and TypeError: the classes of the errors the interpreter raises.
	uint32_t type_error_class = NONE;
	uint32_t range_error_class = NONE;
};

// The module as text, for tests and for reading.
std::string print(const Module &p_module);
// One function as text. Names stand for what operands point at, and no position is shown: the same code
// moved down a file prints the same.
std::string print(const Module &p_module, const Function &p_function);
// One operand of one of the function's instructions, by its shape letter (see KAS3_IR_OPS).
std::string print_operand(const Module &p_module, const Function &p_function, char p_shape, uint32_t p_value);

// The classes, by name, whose code that runs once for each object changed from one build to the next: a
// constructor (which also gives fields their first values), `_ready`, `_enter_tree` or `_init` (its own or
// inherited), or what those call by name or on the object itself (an override included). A field or
// method of the object is compared by name, so one added before it changes nothing and one renamed does.
// A method called on another object is not followed. Objects that are alive ran the old version, so a
// reload of the code alone does not show such a change. A class that is new, or gone, is not listed.
std::vector<std::string> run_once_changes(const Module &p_old, const Module &p_new);

} // namespace ir
} // namespace kas3
