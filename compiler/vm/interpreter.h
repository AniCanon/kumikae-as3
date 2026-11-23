// Runs the IR.
//
// The interpreter keeps its own stack of frames and never recurses in C++ for an AS3 call. Deep recursion
// is therefore an AS3 error, a debugger can walk the frames, and a function could later be paused and
// resumed.
#pragma once

#include "constructions.h"
#include "heap.h"
#include "interpreter_records.h"
#include "host.h"
#include "ir.h"
#include "library.h"
#include "native_code.h"
#include "runtime.h"
#include "stack.h"
#include "text_methods.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kas3 {
namespace vm {

namespace regex {
struct Program;
}

class Interpreter {
	friend class Reload; // Moves a running program's objects onto a newer build.
	friend class Json; // JSON.parse and JSON.stringify (vm/json_work.h).
	friend class LibraryCall; // A method of the library called by name (vm/library_calls.h).
	friend class EngineNamed; // Godot's classes and members an untyped value reaches by name (vm/engine_named.h).

public:
	// Calls may nest this deep. One more is an AS3 error.
	static const size_t MAX_FRAMES = 10000;
	// A stack trace shows this many frames, then how many more there were.
	static const size_t MAX_TRACE = 32;
	// Flash's limit on how long one run may last, in milliseconds.
	static constexpr uint32_t FLASH_TIME_LIMIT = 15000;
	// The clock is looked at once in this many statements.
	static constexpr uint32_t STATEMENTS_PER_LOOK = 4096;

	// Called at each statement, when set: where a debugger stops. Returning false ends the run.
	typedef bool (*StatementHook)(Interpreter &p_interpreter, void *p_data);

private:
	// The records it keeps: vm/interpreter_records.h.
	using Frame = vm::Frame;
	using Site = vm::Site;
	using Carried = vm::Carried;
	using Kept = vm::Kept;
	using Found = vm::Found;
	using PropertyKey = vm::PropertyKey;

	const ir::Module &module_;
	Host host_;
	Heap heap_; // Who holds what, and the cycle collector (vm/heap.h). After the module: it reads the classes.
	// What each object was constructed with, while a host asks for it to be remembered: none otherwise.
	std::unique_ptr<Constructions> constructions_;
	// What the library's functions of plain values ask of whoever runs the program (vm/library.h), answered
	// by this interpreter.
	struct Asked final : Runner, Unmoved {
		Interpreter &vm;
		explicit Asked(Interpreter &p_vm) :
				vm(p_vm) {}
		void raise(uint32_t p_class, const std::string &p_message) override { vm.raise(p_class, p_message); }
		void out_of_memory() override { vm.out_of_memory(); }
		bool failing() const override { return vm.failing_ || vm.stopped_; }
		bool run(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) override {
			return vm.call_within(p_function, p_arguments, p_count, r_result);
		}
		uint32_t carries() const override { return vm.carries_; }
		// What is still the interpreter's own (vm/interpreter_library.cpp).
		bool object_of_engine(Slot p_value, uint32_t p_class, bool *r_taken, Slot *r_out) override;
		bool engine_is(Slot p_value, uint32_t p_class) override;
		bool array_number_get(Vector *p_array, double p_number, Slot *r_out) override;
		bool array_number_set(Vector *p_array, double p_number, Slot p_value) override;
		bool sort_field(Slot p_element, Str *p_name, Slot *r_value) override;
	} asked_{ *this };
	Library library_{ heap_, asked_, asked_ };
	std::vector<Slot> stack_;
	std::vector<Frame> frames_;
	std::vector<Slot> globals_;
	std::vector<Str *> strings_; // The module's string constants.
	Str *null_text_ = nullptr; // "null"
	Str *memory_text_ = nullptr; // Error #1000's message, made before memory can run out.
	std::vector<void *> singletons_; // The engine's singletons, by the engine name of their class, once found.
	// What the host worked out once for each engine instruction, by function and instruction: see prepare().
	std::vector<std::vector<EngineStep>> prepared_;
	// An operator by its engine name, or (NONE: a program built without Godot's names) none Godot can number.
	OperatorName operator_named(uint32_t p_name, std::string_view p_spelling) const {
		return heap_.operator_named(p_name, p_spelling);
	}
	void prepare_engine();
	std::vector<Str *> type_names_; // What typeof gives, made once: see type_name().
	// The strings of one ASCII character each, made the first time charAt() gives one: a loop over a
	// string's characters makes no string for each.
	Characters characters_{};
	bool failing_ = false; // An error is on its way out: pending_ holds it.
	Slot pending_{}; // What was thrown, as an untyped value with a reference of its own.
	std::string trace_; // Its stack trace, built as frames are left.
	// Where it was thrown, as the trace's first line says it: what a host shows as the error's source.
	Site thrown_at_;
	// An error that ended a call from outside made while this program's own code was running beneath it
	// (a listener that its emit() ran): kept, to be raised again at the end of the instruction that made the
	// engine's call (run()), which stopped_ ends.
	Carried carried_;
	// How many errors have been carried: code that asked the engine for something sees whether its own
	// request carried one by this count before and after, not by what another call left held.
	uint32_t carries_ = 0;
	uint32_t direct_ = 0; // Calls of the library a host is making itself (direct_began()).
	void raise_carried();
	void report_carried(); // Reports the error carried, leaving any error on its way out as it was.
	void traced_frame(const Frame &p_frame, std::string *r_trace, size_t *r_lines, Site *r_site) const;
	std::vector<Kept> kept_traces_; // At most one for each finally: see Kept.
	void keep_trace(Kept p_kept);
	void take_trace(uint32_t p_reg); // For a Rethrow of what that register's finally caught.
	size_t trace_lines_ = 0;
	bool stopped_ = false; // Nothing more is to run.
	bool halted_ = false; // Because the statement hook said so, not because of an error.
	bool started_ = false; // The package-level initializers have run.
	std::vector<uint8_t> class_used_; // Per class: its static fields have been (or are being) given values.
	int entries_ = 0; // Calls from outside in progress.
	// A call from outside may be made while another is in progress (the engine calls a script, which calls
	// the engine; a sort calls its comparison) for as long as the thread's stack lasts: with it nearly
	// used up one more is Error #1023 (vm/stack.h).
	__attribute__((always_inline)) static bool stack_used_up() { return stack_below(vm::stack_floor()); }
	StatementHook hook_ = nullptr;
	NativeCode *native_ = nullptr;
	void native_op(const ir::Instruction &p_in);
	void *hook_data_ = nullptr;
	uint32_t time_limit_ = 0; // In milliseconds; 0 for none.
	uint32_t countdown_ = STATEMENTS_PER_LOOK; // Statements until the clock is looked at.
	double entered_ = 0; // When the call from outside that is running began, on the host's clock (Clock::elapsed).
	void look_at_clock();
	void entering(); // A call from outside begins: one that no other is running inside starts the clock.

	// Lifetime is the heap's: these only say it shorter.
	void drop(ir::Kind p_kind, Slot &p_slot) { heap_.drop(p_kind, p_slot); }
	void drop_object(Object *p_object) { heap_.drop_object(p_object); }
	std::vector<std::vector<Slot>> spare_slots_; // For SpareSlots: argument lists of calls through values.
	void drop_any(Slot p_value) { heap_.drop_any(p_value); }
	// The untyped value: vm/interpreter_any.cpp.
	Slot to_any(ir::Kind p_kind, Slot p_value) { return vm::to_any(library_, p_kind, p_value); }
	bool any_text(Slot p_value, std::string *r_text) { return vm::any_text(library_, p_value, r_text); }
	bool engine_text(void *p_handle, std::string *r_text) { return vm::engine_text(library_, p_handle, r_text); }
	bool from_any(Slot p_value, ir::Kind p_kind, uint32_t p_check, Slot *r_out) { return vm::from_any(library_, p_value, p_kind, p_check, r_out); }
	bool coercion_failed(Slot p_value, ir::Kind p_kind, uint32_t p_check) { return vm::coercion_failed(library_, p_value, p_kind, p_check); }
	bool any_is(Slot p_value, uint32_t p_type) { return vm::any_is(library_, p_value, p_type); }
	bool engine_array(Slot p_value, uint32_t p_class) const;
	Vector *array_of_engine(void *p_array, uint32_t p_class);
	bool engine_dictionary(Slot p_value, uint32_t p_class) const;
	Object *table_of_engine(void *p_dictionary, uint32_t p_class);
	// ECMAScript's ToPrimitive: an object's text, or with p_number (where a Number is wanted) a Date's time.
	bool any_primitive(Slot p_value, Slot *r_out, bool p_number = false) { return vm::any_primitive(library_, p_value, r_out, p_number); }
	Slot present_or_null(Slot p_value) { return vm::present_or_null(library_, p_value); }
	void any_op(const ir::Function &p_function, const ir::Instruction &p_in, Slot *p_registers);
	Slot type_name(Slot p_value);
	Slot any_number(double p_value) { return vm::any_number(library_, p_value); }
	Slot any_wide(int64_t p_value) { return vm::any_wide(library_, p_value); }
	void assign(ir::Kind p_kind, Slot &p_slot, void *p_value) { heap_.assign(p_kind, p_slot, p_value); }
	Slot share(ir::Kind p_kind, Slot p_value) { return heap_.share(p_kind, p_value); }
	void put(ir::Kind p_kind, Slot &p_target, Slot p_owned) { heap_.put(p_kind, p_target, p_owned); }
	void host_op(const ir::Function &p_function, const ir::Instruction &p_in);
	// The request itself, on those registers: what the engine gave and the register it goes to (NONE for
	// none), or false with an error raised if the engine refuses.
	struct EngineAnswer {
		uint32_t target;
		ir::Kind kind;
		Slot result;
	};
	bool ask_engine(const ir::Function &p_function, const ir::Instruction &p_in, Slot *p_registers, EngineAnswer *r_asked);
	void settle_asked(const EngineAnswer &p_asked, Slot &r_target);
	std::optional<std::string> godot_refuses(Slot p_value, int p_depth) const;
	std::optional<std::string> refused_by_godot(const ir::Function &p_function, const ir::Instruction &p_in, const Slot *p_registers) const;
	// Whether any operand of an instruction is untyped: only such an operand is ever refused_by_godot().
	static bool untyped_operand(const ir::Function &p_function, const ir::Instruction &p_in);
	bool push(uint32_t p_function, const Slot *p_arguments, size_t p_count, uint32_t p_result);
	void pop();
	void raise(uint32_t p_class, const std::string &p_message);
	Str *stack_text(const Object *p_error, size_t p_skip);
	void out_of_memory();
	void stack_overflow(); // AS3's Error #1023: calls, or what they put into words, nest too deep.
	void throw_new(uint32_t p_class, Str *p_message, std::string_view p_words);
	// A member of nothing was asked for: AS3's TypeError #1009.
	void raise_null();
	void convert_checked(const ir::Instruction &p_in, Slot *p_registers);
	// Parts of the loop in run() that are more than a line or two.
	void at_statement();
	void make(uint32_t p_class, Slot &r_out);
	void enter(const ir::Function &p_function, const ir::Instruction &p_in, const Slot *p_registers);
	void trace(const std::vector<uint32_t> &p_list, const Slot *p_registers);
	void leave(const ir::Function &p_function, const ir::Instruction &p_in, const Slot *p_registers, size_t p_depth, Slot *r_result);
	void object_to_text(const ir::Instruction &p_in, Slot *p_registers);
	void object_to_number(const ir::Instruction &p_in, Slot *p_registers);
	// The standard library (vm/string_methods.cpp): StrCall and NumText.
	void string_call(const ir::Function &p_function, const ir::Instruction &p_in, Slot *p_registers);
	void number_text(const ir::Function &p_function, const ir::Instruction &p_in, Slot *p_registers);
	// RegExp (vm/regexp_methods.cpp, vm/regexp_strings.cpp). A pattern is read once for each source and
	// flags, and kept while anything uses it: a replacement function may make the cache forget it.
	std::unordered_map<std::string, std::shared_ptr<const regex::Program>> patterns_;
	std::shared_ptr<const regex::Program> pattern(std::string_view p_source, uint8_t p_flags);
	std::shared_ptr<const regex::Program> pattern_of(const Object *p_regexp);
	bool is_regexp(Slot p_any) const;
	void regexp_call(const ir::Function &p_function, const ir::Instruction &p_in);
	void regexp_new(const ir::Function &p_function, const ir::Instruction &p_in);
	void regexp_exec(const ir::Function &p_function, const ir::Instruction &p_in);
	// exec() (untyped: the match's Array, or null) or test() (a Boolean) on p_text, not null. False if it raised.
	bool regexp_run(Object *p_regexp, Str *p_text, bool p_test, Slot *r_out);
	// The last text exec() or test() searched, held, and its UTF-16 units: a loop over a global RegExp's
	// matches converts its text once, not once a match. A String never changes, so the one held is the key.
	Str *subject_ = nullptr;
	std::shared_ptr<const std::u16string> subject_units_;
	std::shared_ptr<const std::u16string> subject_units(Str *p_text);
	void forget_subject();
	Slot regexp_read(Object *p_regexp, ir::RegExpFunction p_property); // source (a new reference), or a flag
	// RegExp's and Date's members by name on a value held untyped (vm/library_members.cpp): nothing if the
	// object is neither, or has no member of that name; false if it raised. A call's result goes to p_out.
	std::optional<bool> library_get(Object *p_object, std::string_view p_name, Slot *r_value);
	std::optional<bool> library_set(Object *p_object, std::string_view p_name, Slot p_value);
	std::optional<bool> library_call(Object *p_object, std::string_view p_name, const std::vector<Slot> &p_given, uint32_t p_out);
	bool library_has(const Object *p_object, std::string_view p_name) const;
	std::optional<Found> find_match(const regex::Program &p_program, std::string_view p_source, const std::u16string &p_text, uint32_t p_from);
	// p_input is the text searched (null for "null"), which the Array's `input` shares.
	Vector *match_array(const Found &p_found, const std::u16string &p_text, const regex::Program &p_program, Str *p_input);
	std::shared_ptr<const regex::Program> pattern_argument(Slot p_pattern, Object **r_regexp, std::string *r_source);
	void string_match(const ir::Function &p_function, const ir::Instruction &p_in);
	void string_search(const ir::Function &p_function, const ir::Instruction &p_in);
	void string_replace(const ir::Function &p_function, const ir::Instruction &p_in);
	std::optional<std::vector<Found>> places_to_replace(const regex::Program *p_program, const std::string &p_needle, const std::u16string &p_units);
	// Every match a global pattern finds, one after another as match() finds them; the first only otherwise.
	std::optional<std::vector<Found>> all_matches(const regex::Program &p_program, std::string_view p_source, const std::u16string &p_text);
	bool replacement(Object *p_function, const std::u16string &p_with, bool p_expand, const Found &p_found, const std::u16string &p_text,
			std::u16string *r_out);
	Vector *split_by_pattern(Object *p_regexp, std::string_view p_text, uint32_t p_most);
	// Date (vm/date_methods.cpp).
	void date_call(const ir::Function &p_function, const ir::Instruction &p_in);
	void date_new(const ir::Function &p_function, const ir::Instruction &p_in);
	double date_from_parts(const std::vector<uint32_t> &p_list, size_t p_first, bool p_local);
	double date_set(double p_time, uint32_t p_code, const double *p_given, size_t p_count);
	bool date_apply(Object *p_date, uint32_t p_code, const double *p_given, size_t p_count, Slot *r_out);
	double local_time(double p_time);
	double utc_time(double p_local);
	std::string date_text(const Object *p_date, ir::DateText p_which) const { return vm::date_text(library_, p_date, p_which); }
	void json_call(const ir::Function &p_function, const ir::Instruction &p_in); // vm/json_methods.cpp
	// AnyNullCheck: TypeError #1009 for null, #1010 for undefined.
	void any_null_check(Slot p_value);
	// Vectors. An index outside the vector is AS3's RangeError #1125.
	void raise_range(double p_index, uint32_t p_length) { vm::raise_range(library_, p_index, p_length); }
	bool resize_vector(Vector *p_vector, uint32_t p_length) { return vm::resize_vector(library_, p_vector, p_length); }
	bool fill_zeroes(Vector *p_vector, uint32_t p_from) { return vm::fill_zeroes(library_, p_vector, p_from); }
	bool length_is_fixed(const Vector *p_vector) { return vm::length_is_fixed(library_, p_vector); }
	Vector *fresh_vector(uint32_t p_class, uint64_t p_length) { return library_.fresh_vector(p_class, p_length); }
	// Function values (interpreter_functions.cpp).
	Object *make_function(uint32_t p_function, Object *p_first) { return vm::make_function(library_, p_function, p_first); }
	void function_op(const ir::Function &p_function, const ir::Instruction &p_in);
	void callback_this(Slot p_callback, Slot p_this);
	Object *function_in(ir::Kind p_kind, Slot p_value) { return vm::function_in(library_, p_kind, p_value); }
	bool argument_as(const ir::Function &p_callee, uint32_t p_register, Slot p_value, Slot *r_out) {
		return vm::argument_as(library_, p_callee, p_register, p_value, r_out);
	}
	bool function_arguments(Object *p_value, size_t p_count, const Slot *p_values, std::vector<Slot> *r_values);
	bool function_arguments(uint32_t p_target, Object *p_first, size_t p_count, const Slot *p_values, std::vector<Slot> *r_values);
	// Properties by name (interpreter_properties.cpp, interpreter_property_reads.cpp, interpreter_property_calls.cpp).
	Object *held_object(Slot p_value) const { return vm::held_object(p_value); }
	std::string holder_name(Slot p_holder) const;
	void raise_unsupported(const std::string &p_what);
	void raise_not_found(const PropertyKey &p_key, Slot p_holder);
	bool refuse_write(const char *p_number, const char *p_words, std::string_view p_name, Slot p_holder); // False.
	Object *property_object(Slot p_holder) const; // A script object, including one reached through its Godot node.
	bool property_key(Slot p_holder, Slot p_key, PropertyKey *r_key);
	std::optional<uint32_t> entry_of(const Entries *p_entries, const PropertyKey &p_key) const;
	bool set_entry(Entries *p_entries, const PropertyKey &p_key, Slot p_value);
	std::optional<uint32_t> named_method(const ir::Class &p_class, std::string_view p_name, std::string_view p_prefix = {}) const;
	bool get_property(Slot p_holder, const PropertyKey &p_key, Slot *r_value);
	bool vector_property(Vector *p_vector, const PropertyKey &p_key, Slot p_holder, Slot *r_value);
	bool sealed_property(Object *p_object, const PropertyKey &p_key, Slot p_holder, Slot *r_value);
	bool call_getter(Object *p_object, uint32_t p_function, Slot *r_value);
	bool primitive_property(Slot p_holder, const PropertyKey &p_key, Slot *r_value);
	bool set_property(Slot p_holder, const PropertyKey &p_key, Slot p_value);
	bool set_sealed_property(Object *p_object, const PropertyKey &p_key, Slot p_value, Slot p_holder);
	bool call_setter(Object *p_object, uint32_t p_function, Slot p_value);
	bool set_element(Vector *p_vector, const PropertyKey &p_key, Slot p_value);
	void array_number_get(Vector *p_array, double p_number, Slot &r_out);
	void array_number_set(Vector *p_array, double p_number, Slot p_value);
	bool delete_property(Slot p_holder, const PropertyKey &p_key, bool *r_done);
	bool has_property(Slot p_holder, const PropertyKey &p_key, bool p_prototype, bool *r_has);
	void dynamic_op(const ir::Function &p_function, const ir::Instruction &p_in);
	void property_op(const ir::Function &p_function, const ir::Instruction &p_in);
	void table_new(const ir::Function &p_function, const ir::Instruction &p_in);
	void call_with(uint32_t p_target, Object *p_first, const std::vector<Slot> &p_given, uint32_t p_out);
	bool object_method(Slot p_holder, const PropertyKey &p_key, const std::vector<Slot> &p_given, uint32_t p_out);
	// A method of a String, an Array, a Vector or a Function, called by name: true if the name is one.
	bool library_call(Slot p_holder, std::string_view p_name, const std::vector<Slot> &p_given, uint32_t p_out);
	bool library_name(Slot p_holder, std::string_view p_name) const; // Whether the name is such a method.
	// A method of the library read and not called, as a function bound to the value. CallLibrary runs it.
	bool method_value(Slot p_holder, std::string_view p_name, Slot *r_value);
	void call_library(const ir::Instruction &p_in);
	void arguments_array(const ir::Instruction &p_in);
	// Classes as values (vm/class_values.cpp).
	std::unordered_map<uint32_t, Object *> class_values_; // The one object for each class, kept until the interpreter goes.
	Object *class_value(uint32_t p_code);
	std::optional<uint32_t> class_in(Slot p_class, std::string_view p_error); // The class an untyped value holds, or raised.
	void class_op(const ir::Function &p_function, const ir::Instruction &p_in);
	void class_new(uint32_t p_code, const std::vector<Slot> &p_given, uint32_t p_out);
	void class_call(uint32_t p_code, const std::vector<Slot> &p_given, uint32_t p_out);
	// A class value's public static members by name (class_statics.cpp); nothing where it has none of that name.
	std::unordered_map<std::string, uint32_t> statics_;
	bool statics_listed_ = false;
	std::optional<uint32_t> static_member(uint32_t p_class, char p_joint, std::string_view p_prefix, std::string_view p_name);
	std::optional<uint32_t> statics_of(const Object *p_object, bool *r_raised);
	std::optional<bool> static_get(Object *p_object, const PropertyKey &p_key, Slot p_holder, Slot *r_value);
	std::optional<bool> static_set(Object *p_object, const PropertyKey &p_key, Slot p_holder, Slot p_value);
	bool static_has(const Object *p_object, const PropertyKey &p_key);
	bool static_call(Object *p_object, const PropertyKey &p_key, const std::vector<Slot> &p_given, uint32_t p_out);
	bool set_enumerable(Slot p_holder, const std::vector<Slot> &p_given);
	int32_t function_length(Object *p_function) const { return vm::function_length(module_, p_function); }
	void call_named(Slot p_holder, const PropertyKey &p_key, const std::vector<Slot> &p_given, uint32_t p_out);
	void property_call(const ir::Function &p_function, const ir::Instruction &p_in);
	Entries *loop_entries(Slot p_holder) const;
	void property_loop(const ir::Function &p_function, const ir::Instruction &p_in);
	void let_go_arguments(const ir::Function &p_callee, std::vector<Slot> *p_values);
	void typed_call(const ir::Function &p_function, const ir::Instruction &p_in);
	void call_value(const ir::Function &p_function, const ir::Instruction &p_in);
	std::optional<Slot> call_function_within(Object *p_value, const Slot *p_arguments, size_t p_count);
	void apply_value(const ir::Function &p_function, const ir::Instruction &p_in);
	void to_callable(const ir::Instruction &p_in);
	bool same_function(const Object *p_a, const Object *p_b) const { return vm::same_function(const_cast<Library &>(library_), p_a, p_b); }
	void vector_op(const ir::Function &p_function, const ir::Instruction &p_in, Slot *p_registers);
	void vector_call(const ir::Function &p_function, const ir::Instruction &p_in);
	bool sort_field(Slot p_element, Str *p_name, Slot *r_value);
	bool vector_text(Vector *p_vector, std::string_view p_separator, std::string *r_text, bool p_locale = false) {
		return vm::vector_text(library_, p_vector, p_separator, r_text, p_locale);
	}
	void vector_from(const ir::Instruction &p_in);
	void leave_failed_frame(); // A frame the error leaves: its line goes into the trace.
	// Catches the error in a frame above p_depth that has a handler for it, innermost first: true, and
	// that frame carries on at the handler. Otherwise the frames are left, each line put in the trace.
	bool catch_error(size_t p_depth);
	std::string error_text(); // The error on its way out, as its report begins.
	bool run(size_t p_depth, Slot *r_result);
	bool call(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result = nullptr);
	// A call made from inside running code that is not an instruction of its own (an element's
	// toString() while a vector is put into words). If it fails the error is left on its way out, for
	// the code that is running to carry on unwinding: it is not reported here, as call() reports one.
	bool call_within(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result);
	void report();
	void forget_class(uint32_t p_class); // Its static fields were not all set: it is not used yet, after all.
	void use_class(uint32_t p_class); // From running code: pushes what must run first.
	bool use_class_now(uint32_t p_class); // From outside: runs it.
	std::string site(const Frame &p_frame) const;
	std::string object_text(Object *p_object) const { return vm::object_text(library_, p_object); }
	// "Name: message", as Error's toString() gives it.
	std::string error_heading(const Object *p_error) const { return vm::error_heading(module_, p_error); }
	bool is_class(const Object *p_object, uint32_t p_class) const { return vm::is_class(module_, p_object, p_class); }
	// The function the object's class runs for an interface method; NONE, with the error raised, if the
	// object is missing or its class has none.
	uint32_t interface_function(const Object *p_object, uint32_t p_method);
	Str *text(std::string_view p_text);

public:
	Interpreter(const ir::Module &p_module, Host p_host);
	~Interpreter();
	// Empties every static field and package variable: what they held is let go. For a host that is
	// shutting down and must let go of the engine's objects while the engine can still free them.
	void clear_globals();
	Interpreter(const Interpreter &) = delete;
	Interpreter &operator=(const Interpreter &) = delete;

	// Who carries out the Native instructions of a module whose functions are native code. Without one,
	// such an instruction raises an error.
	void set_native(NativeCode *p_native) { native_ = p_native; }
	// From here on, what each object of the program's own classes is constructed with is remembered
	// (vm/constructions.h), so that a reload can update the objects alive in place (Reload::update_in_place).
	// For a host whose program can be reloaded; asked before the program makes anything. Asked once an
	// object has been made, nothing is remembered; nor of a program with native code, unless that code
	// tells what its objects are made with (p_native_tells), through constructing(): the constructor, by
	// its number, and the arguments it was called with, the object first.
	void remember_constructions(bool p_native_tells = false);
	bool remembers() const { return constructions_ != nullptr; }
	size_t remembered() const { return constructions_ ? constructions_->size() : 0; } // How many objects are.
	void constructing(uint32_t p_function, const Slot *p_arguments, size_t p_count);
	void set_statement_hook(StatementHook p_hook, void *p_data) { hook_ = p_hook, hook_data_ = p_data; }
	// A call from outside that runs, with all it calls, for longer than this (in milliseconds) is stopped
	// with Flash's Error #1502, which no catch or finally stops: an endless loop ends. 0, as an interpreter
	// starts, for no limit. Time a debugger's hook keeps the program stopped is not counted.
	void set_time_limit(uint32_t p_milliseconds) { time_limit_ = p_milliseconds; }
	uint32_t time_limit() const { return time_limit_; }
	// Runs the static initializers, then constructs the entry class. False if an error reached the top:
	// the host has been given it.
	bool run_program();

	// For a host that keeps the interpreter and calls into it. Each of these is an entry from outside. An
	// error that nothing catches ends that entry only: the host is given it, and the interpreter is fit for
	// the next call. An entry may be made while another is in progress.
	// Runs the static initializers, the first time it is called.
	bool start();
	// A new object of the class, its constructor run with no arguments. One reference, the caller's: give
	// it back with release_object(). Null if the constructor failed.
	// For a script class, p_owner is a handle on the Godot object it is attached to. The handle is handed
	// over: the script object drops it when it is freed, and so does this call if it fails. The host
	// decides what such a handle is; it should not keep the Godot object alive.
	Object *construct(uint32_t p_class, void *p_owner = nullptr);
	// A field by its AS3 name: the value as it is held (no new reference) and its kind. False if there is
	// no such field.
	bool get_field(Object *p_object, std::string_view p_name, Slot *r_value, ir::Kind *r_kind = nullptr) const;
	// Writes the field in that slot of the object's class, giving up what it held. The value's reference
	// is handed over.
	void set_field(Object *p_object, uint32_t p_field, Slot p_owned);
	const ir::Module &module() const { return module_; }
	// Calls the object's method of that name. The arguments are in the method's parameter kinds, and
	// parameters left out take their defaults. A reference returned in r_result is the caller's to release.
	// False if there is no such method, or it failed.
	bool call_method(Object *p_object, std::string_view p_name, const Slot *p_arguments, size_t p_count, Slot *r_result);
	// Calls a function the host has already found. The first argument of a method is its object.
	bool call_function(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) {
		return p_function < module_.functions.size() && call(p_function, p_arguments, p_count, r_result);
	}
	// Gives up a value the host was holding for the interpreter: a result, or an argument it made.
	void discard(ir::Kind p_kind, Slot p_value) { drop(p_kind, p_value); }
	// A new Array of the elements of one of Godot's Arrays (a handle), for a host passing one to a script:
	// one reference, the caller's. Null if the engine could not give them (the error is reported).
	Object *copy_engine_array(void *p_array, uint32_t p_class);
	// The same for one of Godot's Dictionaries, where a flash.utils.Dictionary is wanted: its keys as a
	// Dictionary keeps them (a number by its text), each value untyped.
	Object *copy_engine_dictionary(void *p_dictionary, uint32_t p_class);
	// The method's function, or NONE: for a host that looks a name up once and keeps the answer.
	uint32_t find_method(uint32_t p_class, std::string_view p_name) const { return heap_.find_method(p_class, p_name); }
	void release_object(Object *p_object) { heap_.release_object(p_object); }
	// An object the engine kept (EngineHandles::keep()), with no reference left, freed now that the engine
	// let go of it too.
	void free_kept(Object *p_object) { drop_object(p_object); }
	// The heap its values live on, for whatever else runs the program over them (native code's host).
	Heap &heap() { return heap_; }
	// And what its library still asks of the interpreter itself (vm/library.h, Unmoved).
	Unmoved &unmoved() { return asked_; }
	using Left = LeftError; // An error one of those left on its way out (vm/native_code.h).
	bool left_error() const { return failing_ || stopped_; }
	// With p_carried, an error carried back from a call the engine made beneath the host's own call of the
	// library (carries() moved on meanwhile) is taken as well: there is no instruction of this
	// interpreter's to raise it at.
	Left take_error(bool p_carried);
	uint32_t carries() const { return carries_; }
	// The host is calling the library with none of this interpreter's code running: an error of a script
	// the engine runs meanwhile is carried back to that call, as to an instruction.
	void direct_began() { direct_++; }
	void direct_ended() { direct_--; }
	// For a host whose native code is the program (native_code.h says what each is for).
	void engine_instruction(uint32_t p_function, Slot *p_registers);
	bool idle(uint32_t p_direct = 0) const { return frames_.empty() && direct_ == p_direct && !failing_ && !stopped_ && !halted_ && !carried_.held; }
	// p_timed: the host keeps the call's time itself, and knows it began (native code's, for the engine).
	void entered(bool p_timed = false) {
		// The clock is started by the first call from outside alone: one made inside another asks nothing.
		if (entries_ == 0 && !p_timed) {
			entering();
		}
		entries_++;
	}
	void left() { entries_--; }
	bool in_call() const { return entries_ > 0; } // A call from outside is in progress, the interpreter's or native code's.
	void report_native(NativeError p_error);

	// The cycle collector (vm/collector.h), which frees objects that refer to each other in a loop. A host
	// that keeps the interpreter calls collect_step() often (Godot's side, once a frame) with a budget of
	// work, about as many objects and references to look at; it does nothing while a call from outside is
	// in progress. True if a collection ended. collect() makes a whole collection at once: between calls, or
	// at a statement (from the statement hook). When the interpreter goes, it collects once more.
	bool collect_step(size_t p_work) { return entries_ == 0 && heap_.collector.step(p_work); }
	void collect() { heap_.collector.collect(); }
	// Another interpreter has taken over the program (a reload): the engine's objects hold its script
	// objects now, not this one's, so this one's collector no longer looks through them.
	void superseded() { heap_.collector.ignore_engine(); }

	// How many traces finallies are keeping for errors they will throw again: one at most for each.
	size_t kept_traces() const { return kept_traces_.size(); }
	// For a debugger: the frames, innermost last.
	size_t depth() const { return frames_.size(); }
	// For a host whose native code called the engine itself, with no instruction of this interpreter's
	// around the call: the error a listener left for that call (carried, or as the running instruction's
	// own), given up to the host with its trace. False if there is none, or it is one that stops the run.
	bool take_carried(NativeError *r_error);
	const ir::Function &function_at(size_t p_frame) const { return module_.functions[frames_[p_frame].function]; }
	// The line (from 1) the frame is at.
	int line_at(size_t p_frame) const;
	// A local variable or parameter of the frame by name: its value as held (no new reference) and its
	// kind. False if the frame's function has no such local.
	bool local_at(size_t p_frame, std::string_view p_name, Slot *r_value, ir::Kind *r_kind) const;
};

// Flash's words for a run stopped after that many milliseconds.
std::string timeout_message(uint32_t p_milliseconds);

} // namespace vm
} // namespace kas3
