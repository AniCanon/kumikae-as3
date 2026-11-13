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
};

const char *kind_name(Kind p_kind);
inline bool is_reference(Kind p_kind) {
	return p_kind == Kind::Str || p_kind == Kind::Obj;
}
// Held by the engine: the interpreter keeps a handle that the host gave it.
inline bool is_engine(Kind p_kind) {
	return p_kind == Kind::Val || p_kind == Kind::Ref;
}

// How an instruction uses its three operands.
//   R  a register          r  a register, or NONE
//   I  a 32-bit integer    N  an index into the module's numbers     S  into its strings
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
	X(Jump, "L--") X(JumpIf, "RL-") X(JumpIfNot, "RL-") \
	X(GetField, "RRD") X(SetField, "RDR") X(GetGlobal, "RG-") X(SetGlobal, "GR-") \
	X(InitClass, "C--") /* The class is used: give its static fields their values, if this is the first time. */ \
	X(NullCheck, "R--") X(ArgMissing, "RR-") /* a: was the parameter in register b left out by the caller? */ \
	X(New, "RC-") X(Call, "rFA") X(CallVirtual, "rVA") \
	X(IsClass, "RRC") X(AsClass, "RRC") \
	/* The engine. Names are Godot's own. In HostCall the first argument is what the method is called on. */ \
	X(HostNew, "RSA") X(HostCall, "rSA") X(HostGet, "RRS") X(HostSet, "RSR") \
	/* An operator on Godot values: the operator as AS3 writes it ("+", "=="), and one or two operands. */ \
	X(HostOperate, "RSA") \
	X(HostIndexGet, "RRR") X(HostIndexSet, "RRR") /* value[key]: out, value, key; and value, key, new element. */ \
	X(HostToStr, "RR-") /* Godot's text for one of its values or objects; "null" for a missing one. */ \
	X(Return, "R--") X(ReturnVoid, "---") \
	X(Throw, "R--") X(Trace, "A--")

enum class Op : uint8_t {
#define X(name, shape) name,
	KAS3_IR_OPS(X)
#undef X
};

const char *op_name(Op p_op);
const char *op_shape(Op p_op); // Three letters from the table above.

const uint32_t NONE = 0xFFFFFFFFu;

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
};

struct Field {
	std::string name;
	Kind kind = Kind::Void;
};

struct Class {
	std::string name; // "game.Player"
	uint32_t base = NONE;
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
};

struct Module {
	std::vector<std::string> files; // Paths, for stack traces.
	std::vector<std::vector<uint32_t>> lines; // Per file: the offset each line starts at.
	std::vector<Class> classes;
	std::vector<Function> functions;
	std::vector<Global> globals; // Static fields and package-level variables.
	std::vector<std::string> strings;
	std::vector<double> numbers;
	// Functions run once, in order, before anything else: package-level variables. (A class's static fields
	// are set on the class's first use: Class::static_init.)
	std::vector<uint32_t> initializers;
	uint32_t entry_class = NONE; // Constructed with no arguments to start the program.
	uint32_t error_class = NONE; // Error, and TypeError: the classes of the errors the interpreter raises.
	uint32_t type_error_class = NONE;
};

// The module as text, for tests and for reading.
std::string print(const Module &p_module);

} // namespace ir
} // namespace kas3
