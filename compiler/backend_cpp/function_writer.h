// One function of the module written as a C++ function, by the fixed shape of generated code:
//
//   - `Context *c` first, then the parameters in their kinds, then `n` (how many the caller gave) for a
//     function that has defaults;
//   - a register is a local; one that holds a String, an object or an untyped value owns a reference,
//     but for a parameter that is never written, which borrows its caller's;
//   - a result is the caller's to release;
//   - an error is `c->failing`, tested after each operation that can raise one: the function then leaves
//     through its handlers, or by `failed`, which puts its line into the trace.
#pragma once

#include "adjacent_uses.h"
#include "facts.h"
#include "ir.h"
#include "thunks.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kas3 {
namespace backend_cpp {

// A function's C++ type and parameters, for its definition, its declaration and a call through a pointer.
struct Signature {
	std::string result; // "double"
	std::vector<std::string> parameters; // "Context *", "Object *", "double", "uint32_t"
	bool counted = false; // The last parameter is how many arguments the caller gave.
};
Signature signature_of(const ir::Function &p_function);
// Whether callers say how many arguments they give: the function has parameters with defaults.
bool takes_count(const ir::Function &p_function);
// Where an instruction jumps to, or NONE.
inline uint32_t jump_target(const ir::Instruction &p_in) {
	return p_in.op == ir::Op::Jump ? p_in.a : p_in.op == ir::Op::JumpIf || p_in.op == ir::Op::JumpIfNot ? p_in.b : ir::NONE;
}

class FunctionWriter {
	const ir::Module &module_;
	const uint32_t index_;
	const ir::Function &function_;
	ThunkTable &thunks_;
	// The program reads the stack of an error nothing threw: each function keeps a StackFrame, and says
	// which line it is on before anything that may run code.
	const bool keeps_stack_;
	const bool remembers_;
	std::string told() const; // What tells the host of a construction, in a constructor that does.
	const Facts *const facts_; // Null while the facts themselves are being found: nothing is known of any callee.
	std::string body_;
	std::optional<size_t> repeated_read_;
	std::vector<bool> targets_; // Per instruction: a jump lands on it.
	std::vector<bool> written_; // Per register: an instruction writes it.
	// The handlers an instruction's error is offered to, innermost first, as numbers of
	// function_.handlers. Instructions with the same list leave by the same label.
	std::vector<std::vector<uint32_t>> chains_;
	bool fails_ = false; // Something in the function can raise an error.
	// A Number register that holds a sum of whole numbers, as the same sum in 32-bit arithmetic
	// (function_whole.cpp): what it reads, so that it is forgotten when one of those is written.
	struct Whole {
		std::string expression; // Empty: nothing is known of the register.
		std::string exact; // The same sum as a 64-bit integer, which holds it whole: what a comparison reads.
		std::vector<uint32_t> sources;
		uint32_t terms = 0;
	};
	std::vector<Whole> whole_;
	// A String register that holds a constant of the program, by the string's number (NONE otherwise): what
	// Godot takes as a name is then one the host built once. `text_exact_` is false when the string arrived
	// from a static const global (guarded by a pointer comparison in case of circular static init).
	std::vector<uint32_t> text_;
	std::vector<bool> text_exact_;
	// What stands for a register while the instruction being written is one the library runs: a value
	// held inline is boxed into a handle for it (function_engine.cpp). Empty: the register itself.
	std::vector<std::string> shadow_;
	// A register that holds one of Godot's objects and only ever the script's own, or one singleton: it
	// borrows a handle that outlives the function, and owns nothing.
	enum class Alias : uint8_t { None, Owner, Singleton };
	std::vector<Alias> alias_;
	std::vector<uint32_t> alias_name_; // For a singleton: its engine name.
	std::vector<uint32_t> singleton_use_; // And the use the host resolves it by, once written.
	bool has_owner_alias_ = false;
	// Per register: it holds one of Godot's objects that a property is read or written of or a method is
	// called on, so that the object behind its handle is asked for. A field read into such a register again
	// leaves the handle it has where the field still holds the same object (store_again, runtime/engine.h).
	std::vector<bool> asked_;
	// Per register: it holds a value with parts inline. Only one that every instruction writing it gives a
	// value to (what Godot makes, computes or gives for a property of that type): a register that may be
	// given null (a field read, a call's result, an untyped value) stays a handle, which can say so.
	std::vector<bool> inline_;
	void find_inline();
	// function_fields.cpp: the fields of the method's own object that are locals while it runs.
	struct Kept {
		uint32_t slot = 0;
		bool written = false; // The function writes it: the local is written back where memory must be right.
	};
	std::vector<Kept> kept_;
	// What an instruction may do to a field of the slot other than through `this` in this function.
	enum class Touch : uint8_t { None, Reads, Writes };
	Touch touch(uint32_t p_pc, uint32_t p_slot) const;
	void find_kept();
	std::string own_field(uint32_t p_slot) const; // The field of `this` in memory.
	// function_classes.cpp. Per register: the class of the program every object it is given is of (that
	// class or one that extends it), or NONE where nothing says. A field of such an object is written
	// through the class's struct (field_of), any other by its number.
	std::vector<uint32_t> class_of_;
	// Per register: every object it is given is of a class whose objects end alone (facts.h): letting go
	// of what it held runs no code and takes nothing out of any vector.
	std::vector<bool> ends_alone_;
	// Whether the register holds a vector of objects of such a class: what a place of it held ends alone.
	bool elements_end_alone(uint32_t p_vector) const;
	uint32_t class_written(const ir::Instruction &p_in, const std::vector<uint32_t> &p_known) const;
	void find_classes();
	std::string field_of(uint32_t p_object, uint32_t p_slot, ir::Kind p_kind) const; // A field of an object, in memory.
	// Whether the register's object may be the method's own: false only where its class and the method's
	// are known and neither extends the other, so that a field written through it is never a field of `this`.
	bool may_be_this(uint32_t p_register) const;
	std::optional<std::string> kept_local(uint32_t p_object, uint32_t p_slot) const; // The local, if the field is kept.
	std::string kept_declared() const; // The locals, each given its field's value.
	// Statements, on one line: the locals written back before an instruction that may see their fields, and
	// read again after one that may change them. For NONE, all of them (the function's end, a handler).
	std::string kept_written_back(uint32_t p_pc) const;
	std::string kept_read_again(uint32_t p_pc) const;
	// function_borrowed.cpp. Per register: the slot of the field of `this` it is read from and borrows (it
	// counts no reference of its own), or NONE.
	std::vector<uint32_t> borrowed_;
	// For a register that is read from a field of `this` only to emit the signal there: the field's slot,
	// or NONE. Such a register is never given the handle (find_lent_signals).
	std::vector<uint32_t> signal_from_;
	// For a register that is read from a field of `this` only to be a value with parts one of Godot's
	// operators, methods or parts is asked of: the field's slot, or NONE. Such a register is never given
	// the handle either: the value is copied out of the field's own (bare_operand), and the library, where
	// the instruction is left to it, is given a handle of its own for as long as it runs (generic).
	std::vector<uint32_t> value_from_;
	void find_borrowed();
	void settle_reads(bool p_at_once, const std::vector<bool> &p_lands);
	// Per register: it holds an object read out of a vector (or copied from such a register) where nothing
	// can take the object out of the vector while the register is used: in a function that lets go of
	// nothing, or one in which every use of the register follows the read at once. It counts no reference
	// of its own (rule 2).
	std::vector<bool> lent_;
	std::vector<bool> lent_candidates() const;
	// The static fields of numbers and Booleans that are locals for the whole function (`g4`), each read
	// once as it begins: those it reads and never writes, where nothing in it runs code that could write
	// one (rule 12, for statics), and the constants of its own class whatever it runs. Sorted.
	std::vector<uint32_t> still_;
	void find_still();
	bool own_constant(uint32_t p_global) const;
	void read_global(const ir::Instruction &p_in);
	std::string still_declared() const;
	// GetField written: from the local a kept field is in, with no reference counted into a register that
	// borrows, or as any other read.
	void read_field(const ir::Instruction &p_in, const std::string &p_field);
	// may_run_code(), but that a field read into a register that borrows lets go of nothing, a register of a
	// class whose objects end alone lets go of nothing but its object, and a class with nothing to set up
	// and an object of such a class are used and made with nothing run.
	bool runs_code(const ir::Instruction &p_in) const;
	// Per instruction: it copies a register that nothing reads afterwards, so the reference is handed over
	// and not shared (a Move, or a field or static field set).
	std::vector<bool> hands_over_;
	void find_last_uses();
	// Stores a register's value where the instruction says; the register gives up its reference if this
	// is its last use.
	void keep(const std::string &p_place, uint32_t p_register, bool p_is_slot);
	// function_loops.cpp: a loop over vectors of numbers, or of objects it only reads, written twice.
	struct Twice {
		uint32_t from = 0; // The instruction its jump back goes to, and that jump.
		uint32_t to = 0;
		std::vector<uint32_t> vectors; // The registers whose vectors' length and elements are locals in the first copy.
	};
	std::vector<Twice> twice_;
	const Twice *fast_ = nullptr; // The loop whose copy with locals is being written, if one is.
	std::vector<bool> retried_; // Per instruction: the copy with locals leaves for it in the other (label S).
	std::vector<uint32_t> loop_vectors(uint32_t p_from, uint32_t p_to) const;
	bool reads_in_place(uint32_t p_register) const;
	ir::Kind vector_storage(uint32_t p_vector) const;
	void find_twice();
	bool fast_vector(const ir::Instruction &p_in);
	std::string label(uint32_t p_target) const; // "L12", or "V12" for a jump inside the copy with locals.
	std::string fast_copy(const Twice &p_loop, const std::vector<bool> &p_landed);
	std::string twice_declared() const;
	uint32_t pc_ = 0;

	std::string reg(uint32_t p_register) const;
	ir::Kind kind(uint32_t p_register) const { return function_.registers[p_register]; }
	bool owns(uint32_t p_register) const; // Whether the register holds a reference of its own.
	bool is_this(uint32_t p_register) const; // The object of the method being written: never null.
	void line(const std::string &p_text);
	// Stores an owned value in a register, giving up what was there.
	void put(uint32_t p_register, const std::string &p_owned);
	// Stores a value someone else holds: a reference is taken first.
	void share(uint32_t p_register, const std::string &p_borrowed);
	std::string fail(); // "line = 12; goto failed;": where an error at this instruction goes.
	// function_rounds.cpp: where a loop goes round, the host's alarm is looked at once in many rounds.
	std::string jump(uint32_t p_target); // A jump, with what counts the round if it goes back.
	std::vector<bool> rounds_; // Per instruction: a jump goes back to it.
	bool rounds_counted(uint32_t p_pc) const; // Whether such a jump is counted in the function's local.
	// A loop counted by one whole-number register against a bound that stays as it is.
	struct Counted {
		uint32_t from = 0; // The instruction its one jump back goes to, and that jump.
		uint32_t to = 0;
		uint32_t test = 0; // The comparison of the counter and the bound; the jump out follows it.
		uint32_t counter = 0;
		bool up = true; // The counter goes up to the bound, or down to it.
		bool inclusive = false; // `<=`.
		uint32_t bound = 0; // The Number register the counter is held against.
		uint32_t step = 0; // The Number register added to the counter, or taken from it.
		uint32_t stepped = 0; // The instruction that adds or takes it.
		uint32_t moved = 0; // The instruction that writes the counter.
	};
	std::vector<Counted> counted_;
	std::set<uint32_t> loop_binds_; // Immutable method binds read by a counted loop in this function.
	// How such a loop is written in the copy being written.
	struct Counts {
		const Counted *loop = nullptr;
		bool index = false; // The stretch's end is held as an index of the loop's vectors.
		std::string start; // The label where a stretch begins.
		std::string label; // Where the jump back goes: past the stretch's start.
		std::string end; // The local that holds the stretch's end.
		std::string within; // The test that the counter is short of its stretch's end.
		std::string left; // How far the counter is from the loop's end: none or less, and the loop ends.
		std::string begin; // The statement that begins a stretch at the counter.
		std::string none; // What the stretch's end is where there is no stretch; empty where there always is one.
		// For an index: the test that there is no stretch from where the counter is.
		std::string leaves;
	};
	void find_counted();
	bool counted_shape(Counted *r_loop) const;
	bool counted_step(Counted *r_loop) const;
	std::vector<uint32_t> writes_in(uint32_t p_register, uint32_t p_from, uint32_t p_to) const;
	uint32_t written_once(const Counted &p_loop, uint32_t p_register, uint32_t p_low, uint32_t p_high) const;
	std::optional<std::string> still_whole(const Counted &p_loop, uint32_t p_register, uint32_t p_low, uint32_t p_before, uint32_t p_parts) const;
	uint32_t parts_begin(const Counted &p_loop, uint32_t p_register, uint32_t p_low, uint32_t p_before, uint32_t p_parts) const;
	std::optional<Counts> counts(uint32_t p_from) const;
	std::vector<uint32_t> indexed(const Counted &p_loop) const;
	bool int_bound(const Counted &p_loop) const;
	// Whether the element the instruction being written reads or stores is known to be one of the vector's,
	// by the stretch its index is held to: nothing more is asked of the index there.
	bool index_held(uint32_t p_vector, uint32_t p_index) const;
	std::string counted_declared() const;
	bool counted(const ir::Instruction &p_in);
	void check(); // If an error is on its way out, go where fail() says.
	void mark(); // Before what may run other code: the line this function is on, where frames are kept.
	void null_check(uint32_t p_register);
	uint32_t chain_of(uint32_t p_pc);

	void instruction(const ir::Instruction &p_in);
	bool simple(const ir::Instruction &p_in); // function_simple.cpp: arithmetic, conversions, comparisons.
	bool constant(const ir::Instruction &p_in);
	bool object(const ir::Instruction &p_in); // function_objects.cpp: fields, globals, classes.
	bool flow(const ir::Instruction &p_in); // Jumps, returns, throws.
	bool call(const ir::Instruction &p_in); // function_calls.cpp
	// One of the functions a call may be of: when it is the one (a condition on `k`, the receiver's class;
	// empty for the last, which answers for the rest), and what is called.
	struct Target {
		std::string when;
		std::string function;
	};
	// A call written for p_callee's parameters and result, of the first target whose condition holds.
	// p_looks_first: the depth is looked at here, for a callee that may not count it itself. p_may_fail:
	// an error is looked for afterwards.
	void call_function(const ir::Instruction &p_in, const ir::Function &p_callee, const std::vector<Target> &p_targets, bool p_looks_first, bool p_may_fail);
	void call_typed(const ir::Instruction &p_in);
	void call_through_table(const ir::Instruction &p_in);
	// What the call at an instruction does, itself and through what it calls: the function called by its
	// name, or every function a method table may hold there. Null where nothing is known (and while the
	// facts themselves are being found): the call may then do anything. Asked only of a call (is_call()).
	const FunctionFacts *called(uint32_t p_pc) const;
	void by_thunk(const ir::Instruction &p_in);
	void raw_thunk(const ir::Instruction &p_in); // The instruction's thunk, whatever the library has for it.
	bool by_library(const ir::Instruction &p_in); // function_library.cpp: the library's function, called direct.
	std::string_view parts(uint32_t p_register) const; // The value with parts the register holds inline, if it does.
	void dispatch(const ir::Instruction &p_in); // The instruction written by whichever part owns it.
	void generic(const ir::Instruction &p_in); // dispatch(), with the values held inline boxed for it.
	const ir::Function *callee_of(const ir::Instruction &p_in) const;
	// function_engine.cpp
	void find_aliases();
	bool holds(uint32_t p_register, std::string_view p_type) const;
	// Whether a register holds a handle on one of Godot's values or objects of exactly that type.
	bool holds_handle(uint32_t p_register, std::string_view p_type) const;
	std::string object_behind(uint32_t p_register);
	std::optional<std::string> bare_argument(uint32_t p_register, std::string_view p_type, size_t p_at, std::string *r_guard, std::string *r_inside);
	bool engine(const ir::Instruction &p_in);
	bool engine_alias(const ir::Instruction &p_in);
	bool engine_inline(const ir::Instruction &p_in);
	bool engine_method(const ir::Instruction &p_in);
	bool engine_bound(const ir::Instruction &p_in);
	bool emits_bare(const ir::Instruction &p_in) const; // Whether an instruction is an emit() engine_emit() writes.
	bool engine_emit(const ir::Instruction &p_in); // A signal's emit() on values held bare.
	bool only_values(const ir::Instruction &p_in) const;
	void find_lent_signals();
	// function_values.cpp: Godot's own code for a value type, called on bare values.
	std::string_view value_type(uint32_t p_register) const;
	std::optional<std::string> bare_operand(uint32_t p_register, size_t p_at, std::string *r_guard, std::string *r_inside);
	bool value_call(const ir::Instruction &p_in, uint32_t p_out, std::string_view p_result, const std::string &p_guard, const std::string &p_inside,
			const std::string &p_call);
	void settle_use(const ir::Instruction &p_in, size_t p_from);
	bool value_made(const ir::Instruction &p_in);
	bool value_part(const ir::Instruction &p_in);
	bool value_operation(const ir::Instruction &p_in);
	bool kept_method(const ir::Instruction &p_in);
	bool value_keyed(const ir::Instruction &p_in);
	// The instruction that was written with the one before it (value_keyed), for which nothing more is.
	uint32_t joined_ = ir::NONE;
	bool value_constant(const ir::Instruction &p_in);
	bool engine_value(const ir::Instruction &p_in);
	void forget_whole(uint32_t p_register);
	bool whole_conversion(const ir::Instruction &p_in);
	bool whole_comparison(const ir::Instruction &p_in);
	void remember_whole(const ir::Instruction &p_in);
	// function_fast.cpp: an inline path for the usual case, and the thunk for the rest.
	bool fast_object(const ir::Instruction &p_in); // A new object, a vector's elements and length.
	// A vector's place that holds one of Godot's values whole, read into a register that holds it inline or
	// written from one, with no handle between.
	bool fast_place(const ir::Instruction &p_in);
	std::string vector_for(uint32_t p_vector, uint32_t p_element) const;
	bool fast_text(const ir::Instruction &p_in);
	std::optional<ir::AdjacentUses> adjacent_;
	bool fast_named(const ir::Instruction &p_in);
	bool fast_untyped(const ir::Instruction &p_in);
	bool fast_call(const ir::Instruction &p_in);
	std::string arguments(const ir::Function &p_callee, const std::vector<uint32_t> &p_list);
	const ir::Function *virtual_callee(const ir::Instruction &p_in) const;

	void analyse();
	std::string prologue() const;
	std::string epilogue() const;
	std::string handlers() const;

public:
	// p_remembers: a constructor tells the host what its object is constructed with (generate.h, Options).
	FunctionWriter(const ir::Module &p_module, uint32_t p_function, ThunkTable &r_thunks, bool p_keeps_stack, const Facts *p_facts,
			bool p_remembers = false);
	// The function's definition, and after it its entry (the function called with slots).
	std::string write();
	// Once written: whether anything in it can raise an error.
	bool fails() const { return fails_; }
};

} // namespace backend_cpp
} // namespace kas3
