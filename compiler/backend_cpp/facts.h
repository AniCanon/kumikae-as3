// What is known of every function of the module before any is written, so that a function can be written
// by what the functions it calls do.
#pragma once

#include "ir.h"

#include <map>
#include <set>
#include <utility>
#include <vector>

namespace kas3 {
namespace backend_cpp {

struct FunctionFacts {
	// Nothing in it can raise an error, and so it calls nothing of the program's (a call can always be
	// one too deep). It is written with no count of the depth, and whoever calls it looks at the depth
	// before the call in its place, and for no error after it.
	bool quiet = false;
	// What it does to the fields of objects, itself and through the functions it calls: either anything
	// at all (it runs code that is not looked into: the library, the engine, a call through a table that
	// may hold anything, or it lets go of something whose end may run a script), or only these fields,
	// read and written. Each is a slot with the class that declares the field, where the object is the
	// method's own (an object of that class, then, or of one that extends it), and with NONE where the
	// object may be any.
	bool opaque = true;
	std::set<std::pair<uint32_t, uint32_t>> reads;
	std::set<std::pair<uint32_t, uint32_t>> writes;
	// It, or a function it calls, stores an element of a vector: which may make the vector longer.
	bool stores_elements = false;
};

// What is known of a call through a class's method table (CallVirtual) whose receiver's class is known: the
// receiver is an object of that class or of one that extends it, and the program is whole, so the function
// called is one of those the classes have in the slot.
struct TableFacts {
	// The one function every such class has there, or NONE where one of them overrides it: a call of it
	// needs no table and no class compared.
	uint32_t only = ir::NONE;
	// The classes among them that the program makes objects of with `new`, each with the function its
	// table holds in the slot, in the classes' order: what the object's class is compared with before the
	// table is asked. The table answers for any other class, so nothing rests on this list being whole.
	std::vector<std::pair<uint32_t, uint32_t>> made;
	// What those functions do, taken together, as one function's facts say it: opaque if any is, quiet
	// only if every one is.
	FunctionFacts does;
};

// Whether one of the fields listed (FunctionFacts::reads, writes) may be the field in that slot of an object
// of the class or of one that extends it: one of any object's, or one a class that this class is or
// extends declares. A field a class further down declares is in a slot past this class's own.
bool may_be_field(const ir::Module &p_module, const std::set<std::pair<uint32_t, uint32_t>> &p_fields, uint32_t p_class, uint32_t p_slot);

// Whether an instruction calls a function of the program by its name or through a method table.
inline bool is_call(ir::Op p_op) {
	return p_op == ir::Op::Call || p_op == ir::Op::CallVirtual;
}

struct Facts {
	std::vector<FunctionFacts> functions;
	// For each call through a method table whose receiver's class is known: by that class and the slot.
	std::map<std::pair<uint32_t, uint32_t>, TableFacts> tables;
	// Per function: its instructions that are such calls, each with the class, in their order.
	std::vector<std::vector<std::pair<uint32_t, uint32_t>>> receivers;
	// What is known of the call through a method table at an instruction of a function, or null: the
	// receiver may then be of any class, and the call may do anything.
	const TableFacts *table(const ir::Module &p_module, uint32_t p_function, uint32_t p_pc) const;
	// What the call at an instruction does (is_call()): the function called by name, or every function a
	// table may hold there. Null where nothing is known.
	const FunctionFacts *called(const ir::Module &p_module, uint32_t p_function, uint32_t p_pc) const;
	// Per class: letting go of an object of it ends nothing but the object. It holds numbers, Booleans and
	// Strings and nothing whose end could be another's, it is the program's own (not one of Godot's or the
	// library's), and so is every class that extends it. Such an end runs no code and takes nothing out of
	// any vector.
	std::vector<bool> ends_alone;
	// Per class: its first use runs nothing: neither it nor a class it extends gives static fields values.
	std::vector<bool> starts_quietly;
	// Per global: for a static const String only ever assigned a single string literal in the module, that
	// literal's index in module.strings (or NONE).
	std::vector<uint32_t> const_strings;
};

// Whether an instruction may run code that nothing here looks into. One that cannot is arithmetic, flow,
// an element of a vector, a static field: it leaves every field of every object as it was, but for a
// GetField or a SetField (and a Call, which is as its callee is), which say themselves what they touch.
// Letting go of an object, a Godot value or an untyped value counts as running code: its end may be the
// end of a Godot object, which a script is told of.
// For a Call or a CallVirtual it answers for the call alone (a result that is let go of), not for what the
// callee does: that is asked of Facts::called(). p_in is an instruction of the function's own code.
bool may_run_code(const ir::Module &p_module, const ir::Function &p_function, const ir::Instruction &p_in);
// The same, where writing a register lets go of nothing because the register counts nothing (p_counts_nothing
// says which): what is left is what the instruction does besides.
bool may_run_code(const ir::Module &p_module, const ir::Function &p_function, const ir::Instruction &p_in,
		const std::vector<bool> &p_counts_nothing);

// The function whose parameters and result a call through a method table goes by: the one the class that
// declares the method has in that slot (every override takes and gives the same: support.cpp), or, where
// the call names no class, the one the caller's own class has there. Null where neither has one.
const ir::Function *table_signature(const ir::Module &p_module, const ir::Function &p_function, uint32_t p_pc);

// Found by writing every function once, into nothing: what can fail is what the writer writes a way out
// for, so the two cannot come apart.
Facts find_facts(const ir::Module &p_module, bool p_keeps_stack);

} // namespace backend_cpp
} // namespace kas3
