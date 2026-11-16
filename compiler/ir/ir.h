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
	X(Trace, "A--")

enum class Op : uint8_t {
#define X(name, shape) name,
	KAS3_IR_OPS(X)
#undef X
};

// The functions of Math that take one Number, in MathUnary's third operand.
enum class MathFunction : uint8_t { Abs, Acos, Asin, Atan, Ceil, Cos, Exp, Floor, Log, Round, Sin, Sqrt, Tan };

// The fields of a function value: the function, and what it is called with first (the object of a method,
// or the environment of a function expression), or null for a function that takes nothing first.
constexpr uint32_t FUNCTION_INDEX = 0, FUNCTION_FIRST = 1;

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
//   Sort            a function that compares two elements, or the options as a number (untyped)   -> the vector
// (forEach, map, filter, some and every are loops the lowerer writes, calling the function each time.)
//   SortOn          the field names, the options (untyped), as Array's sortOn takes them   -> as Sort's
enum class VectorMethod : uint8_t { Push, Unshift, Pop, Shift, IndexOf, LastIndexOf, Reverse, Slice, Concat, Join, InsertAt, RemoveAt, Splice, Sort, SortOn };

// String's methods and the functions on text, in StrCall's second operand. An index or a count is a
// Number as the program wrote it; text is a String, and a null one reads "null". After the string:
//   CharAt, CharCodeAt    an index                -> String, or a Number (NaN outside the string)
//   IndexOf, LastIndexOf  text, where to start    -> int
//   Substr                start, length           -> String
//   Substring, Slice      start, end              -> String
//   Concat                any number of texts     -> String
//   Upper, Lower          nothing                 -> String
//   Replace               text, replacement       -> String: the first match, the replacement as it is
//   LocaleCompare         text                    -> int: -1, 0 or 1, by UTF-16 units
// With no string first:
//   FromCharCode          any number of Numbers   -> String
//   ParseInt              text, radix (int)       -> Number
//   ParseFloat            text                    -> Number
enum class StringFunction : uint8_t {
	CharAt, CharCodeAt, IndexOf, LastIndexOf, Substr, Substring, Slice, Concat, Upper, Lower, Replace, LocaleCompare,
	FromCharCode, ParseInt, ParseFloat,
	Split, // The string, the delimiter (untyped: a RegExp, or what becomes text), the limit (a Number): an Array.
	// The text -> String. A decode of text that is not a URI's, or an encode of a half of a character
	// alone, is AS3's URIError #1052.
	Escape, Unescape, EncodeURI, EncodeURIComponent, DecodeURI, DecodeURIComponent
};

// RegExp's methods and properties, in RegExpCall's second operand. After the RegExp, or the text for
// String's methods:
//   New                   the source, the flags (each a String, or untyped: a RegExp source is copied)  -> the RegExp
//   Exec                  the text                                -> an Array, or null (untyped)
//   Test                  the text                                -> Boolean
//   Source ... Extended   nothing                                 -> String, or Boolean
//   Match                 the pattern (untyped: a RegExp, or what becomes one)   -> an Array, or null
//   Search                the pattern                             -> int
//   Replace               the pattern, the replacement (both untyped: a function is called)   -> String
enum class RegExpFunction : uint8_t { New, Exec, Test, Source, Global, IgnoreCase, Multiline, Dotall, Extended, Match, Search, Replace };
// The fields of a RegExp: the source, the flags (vm/regex.h), and lastIndex, which a program reads and
// writes as a field.
constexpr uint32_t REGEXP_SOURCE = 0, REGEXP_FLAGS = 1, REGEXP_LAST_INDEX = 2;

// Date's methods and properties, in DateCall's second operand as date_code(function, detail, utc). Every
// number is a Number. After the Date:
//   New        nothing (now), one untyped value (a time, a Date, or text to parse), or the parts from
//              the year, two to seven of them (with no Date first)   -> the Date
//   Parse      the text; UTC   the parts (with no Date first)        -> the time
//   Time, TimezoneOffset   nothing    -> Number;   SetTime   the time   -> the time
//   Get        nothing (detail: a DatePart)  -> Number
//   Set        the parts written, from the detail's on (setHours(h, m)) -> the time
//   Text       nothing (detail: a DateText)  -> String
enum class DateFunction : uint8_t { New, Parse, UTC, Time, SetTime, TimezoneOffset, Get, Set, Text };
enum class DatePart : uint8_t { FullYear, Month, Date, Day, Hours, Minutes, Seconds, Milliseconds };
enum class DateText : uint8_t { Full, Date, Time, Locale, LocaleDate, LocaleTime, UTC };
constexpr uint32_t date_code(DateFunction p_function, uint32_t p_detail = 0, bool p_utc = false) {
	return static_cast<uint32_t>(p_function) | p_detail << 8 | (p_utc ? 1u << 16 : 0u);
}
constexpr uint32_t DATE_TIME = 0; // The one field of a Date: its time, in milliseconds since 1970 in UTC.

// JSON's functions, in JsonCall's second operand:
//   Parse      the text (String), the reviver (a Function, or null)          -> untyped: null for undefined
//   Stringify  the value, the replacer, the space (each untyped)             -> String: null for nothing
//   ToJson     a Dictionary, the key (String): Dictionary's toJSON()          -> untyped
enum class JsonFunction : uint8_t { Parse, Stringify, ToJson };

// toString(radix), toFixed, toExponential and toPrecision, in NumText's second operand. The number is a
// Number, or an Int64 (its register says which), written from its own digits.
enum class NumberFormat : uint8_t { Radix, Fixed, Exponential, Precision };

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
	bool is_public = false; // A public method: Godot may call it by name.
	// The first registers are the parameters; an instance method's first parameter is `this`.
	uint32_t parameters = 0;
	uint32_t required = 0; // How many of them have no default: a caller must pass at least these.
	// `...rest`: the last parameter's register is an Array of every argument past the others. A caller
	// passes those untyped, after the others, and as many as it likes.
	bool rest = false;
	Kind result = Kind::Void;
	// For each register that takes a Godot value or object from outside (a parameter, or the result of a
	// request to the engine): Godot's name for its type ("Vector2", "CharacterBody2D"), by register. Empty
	// for the others, and the vector may be shorter than the registers. A host checks what arrives against it.
	std::vector<std::string> engine_types;
	// For each parameter that holds a script object: the class it is declared to hold, by register (NONE
	// for the others; the vector may be shorter). A call through a function value checks what it passes.
	std::vector<uint32_t> parameter_classes;
	// For each CallVirtual on an object other than `this`: the instruction, and the class that declares the
	// method called (what runs is its method or a subclass's override). Not printed: run_once_changes reads it.
	std::vector<std::pair<uint32_t, uint32_t>> virtual_receivers;
	std::vector<Kind> registers;
	std::vector<Instruction> code;
	std::vector<std::vector<uint32_t>> argument_lists;
	std::vector<Local> locals;
	std::vector<Handler> handlers;
};

// A value written in the source as a constant, for a host to be told of: `Empty` is what a field holds
// before anything is written to it; `None` that the value is not a constant.
struct Constant {
	// Value: one of Godot's values made from numbers (`new Vector2(1, 2)`), its type's Godot name in `text`.
	enum class Form : uint8_t { None, Empty, Number, Boolean, Text, Null, Value };
	Form form = Form::None;
	double number = 0; // A Number, or a Boolean as 0 or 1.
	std::string text;
	std::vector<double> parts; // A Value's numbers.
};

struct Field {
	std::string name;
	Kind kind = Kind::Void;
	// For an object: the class it is declared to hold (a class of the program, or a vector's), or NONE
	// where that is not known. A reload keeps what a field held only if it is still of that class.
	uint32_t klass = NONE;
	bool starts_null = false; // An untyped field declared `Object`: it starts null, a `*` undefined.
	bool named = false; // Public: reached by its name as the program runs (`x.name` on an untyped value).
	// What Godot may do with it by name (`node.speed`): read a public field, and write one that is not a
	// const. Godot is told of no other.
	bool is_public = false;
	// Protected: reached by name from the code of the class that declares it and of its subclasses. A field
	// neither public nor protected is reached by name only from the code of the class that declares it.
	bool is_protected = false;
	bool constant = false;
	// Godot's name for its type, when it holds one of Godot's values or objects ("Vector2", "Node").
	std::string engine_type = {};
	// [Export]: Godot's inspector shows it, and a scene's value for it is given to it.
	bool exported = false;
	// An exported field's first value, as Godot is told it, to compare a scene's value with.
	Constant first = {};
};

struct Class {
	std::string name; // "game.Player"
	uint32_t base = NONE;
	// For a Vector.<T>, what its elements are; Void for every other class. A vector class has no fields,
	// no methods and no base, and its objects are vm::Vector.
	Kind element = Kind::Void;
	// Array: the one class of every Array, a vector of untyped elements with Array's own rules (a read past
	// the end is undefined, a write past it grows the array, never fixed).
	bool array = false;
	// Object and flash.utils.Dictionary: objects that are vm::Table, keyed by text (a Dictionary also by the
	// identity of an object), with no fields.
	bool table = false;
	bool dictionary = false;
	// A class of the library (Error, Function): AS3 makes it dynamic, so a name it lacks is not one of
	// Flash's ReferenceErrors here but something the interpreter does not do yet.
	bool built_in = false;
	std::vector<Field> fields; // The base's first: a field keeps its slot in every subclass.
	std::vector<uint32_t> methods; // The method table: function indices, the base's slots first.
	// The public instance methods, by name, with their method-table slots: reached by name as the program
	// runs. The base's first; an override is listed once, under its base's slot.
	std::vector<std::pair<std::string, uint32_t>> named_methods;
	uint32_t constructor = NONE;
	uint32_t static_init = NONE; // The function that gives the class's static fields their values, if any.
	uint32_t to_string = NONE; // The class's toString(), its own or inherited: what converting to a String calls.
	// For a script class, one that extends a Godot class directly or through other script classes: Godot's
	// name for that class. Field 0 then holds the Godot object the script is attached to.
	std::string native_base;
	// A script class whose Godot object is reference-counted (it extends RefCounted, a Resource): when the
	// program lets go of an object of it, the engine is asked whether it still holds the Godot object.
	bool counted = false;
	// The engine's virtual methods the class answers: Godot's name ("_physics_process") and the function.
	std::vector<std::pair<std::string, uint32_t>> virtuals;
	// An interface: it has no objects, fields, methods or constructor; a class lists it in `interfaces`.
	bool is_interface = false; // Not `interface`: a macro in Windows's headers.
	// Every interface the class implements, those its bases implement and those they extend; for an
	// interface, those it extends. `is`, `as` and every check of an object's class read it.
	std::vector<uint32_t> interfaces;
	// By interface method (Module::interface_methods): the function the class runs for it, or NONE.
	std::vector<uint32_t> interface_methods;
	// The signals the class declares itself (`[Signal] public var hit:Signal;`), by name: Godot is told of
	// them, with the base classes' too. Each one's field holds the signal from the start.
	std::vector<std::string> signals;
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
	// The interfaces' methods by name, as AVM2 finds them: "area", "get name", "set name". One name is one
	// method, whichever interfaces declare it.
	std::vector<std::string> interface_methods;
	uint32_t entry_class = NONE; // Constructed with no arguments to start the program.
	uint32_t error_class = NONE; // Error, and TypeError: the classes of the errors the interpreter raises.
	uint32_t type_error_class = NONE;
	uint32_t range_error_class = NONE;
	uint32_t argument_error_class = NONE; // Made when a program calls a function value: too many arguments.
	// The built-in class of function values (FUNCTION_FIELDS), when the program has any.
	uint32_t function_class = NONE;
	uint32_t array_class = NONE; // The one class of every Array, when the program has any.
	uint32_t object_class = NONE; // The class of `{}` and `new Object()`, when the program makes one.
	uint32_t dictionary_class = NONE; // flash.utils.Dictionary, when the program makes one.
	// Made when a program reaches a property by name: a sealed object's missing one (#1069, #1056).
	uint32_t reference_error_class = NONE;
	// RegExp and Date, and the errors their functions and the URI functions raise, when the program uses them.
	uint32_t regexp_class = NONE;
	uint32_t date_class = NONE;
	uint32_t syntax_error_class = NONE;
	uint32_t uri_error_class = NONE;
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
// inherited), or what those call: by name, on the object itself (an override included), on another object
// through the class it is declared as (any subclass's override included) or through an interface (any
// class's that implements it). A field or method of the object a function runs on is compared by name, so
// one added before it changes nothing and one renamed does. So a `_ready` that reaches much of the game
// is changed by most edits to it. Objects that are alive ran the old version, so a
// reload of the code alone does not show such a change. A class that is new, or gone, is not listed.
std::vector<std::string> run_once_changes(const Module &p_old, const Module &p_new);
// The classes, by name, whose static fields' first values changed from one build to the next (what the
// class's static initializer runs, and what it calls). Static fields given their values keep them through a
// reload, and through the scene started again.
std::vector<std::string> static_changes(const Module &p_old, const Module &p_new);

} // namespace ir
} // namespace kas3
