// The lowerer: turns a checked program into the typed IR. One class; its methods are in the lower_*.cpp
// files, by what they lower. Nothing outside ir/ includes this.
#pragma once

#include "lower.h"

#include "ast_walk.h"
#include "builtins.h"
#include "whole_numbers.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace kas3 {
namespace ir {

struct Loop {
	std::vector<uint32_t> breaks; // Jumps to patch to the loop's end.
	std::vector<uint32_t> continues; // Jumps to patch to where the loop continues.
	bool is_switch = false; // A `switch`: `break` leaves it, `continue` belongs to the loop around it.
	bool is_block = false; // A labelled block: only `break label` leaves it.
	std::vector<std::string_view> labels; // The labels written before it; one statement may have several.
	size_t tries = 0; // How many `try` statements were open when it began.
};

// A way out of a `try` that has a `finally`: the finally runs, then this.
struct TryExit {
	enum Kind { Break, Continue, Return } kind = Break;
	size_t loop = 0; // For Break and Continue: the loop it leaves or goes round.
	uint32_t position = 0;
};

// A `try` statement being lowered. Its `finally` is written once; each way out sets `exit_reg` to say
// where to go after it (0 on past the try, 1 the error goes on, 2 and up one of `exits`) and jumps there.
struct TryBlock {
	Block *finally_block = nullptr;
	uint32_t exit_reg = 0;
	// The value of a `return` that leads out through this finally, kept here so that a return cancelled
	// inside the finally (by `break`, `continue` or a caught error) does not change an outer one.
	uint32_t return_reg = NONE;
	std::vector<uint32_t> to_finally; // Jumps to the finally, patched once it is written.
	std::vector<TryExit> exits;
};

class Lowerer {
	Compilation &compilation_;
	Module &module_;
	bool failed_ = false;

	Symbol *int_ = nullptr;
	Symbol *uint_ = nullptr;
	Symbol *int64_ = nullptr;
	Symbol *number_ = nullptr;
	Symbol *math_ = nullptr;
	Symbol *boolean_ = nullptr;
	Symbol *string_ = nullptr;
	Symbol *object_ = nullptr;
	Symbol *error_ = nullptr;
	Symbol *godot_object_ = nullptr; // godot.GodotObject: what every Godot object class extends.
	Symbol *signal_ = nullptr; // godot.Signal
	Symbol *function_ = nullptr; // Function

	std::unordered_map<const Symbol *, uint32_t> classes_;
	std::unordered_map<const Type *, uint32_t> vector_classes_; // The class made for each Vector.<T> met.
	std::unordered_map<const Symbol *, uint32_t> functions_; // Methods, constructors, package functions.
	std::unordered_map<const Symbol *, uint32_t> fields_; // Instance fields: the slot.
	std::unordered_map<const Symbol *, uint32_t> globals_; // Static fields and package variables.
	std::unordered_map<const Symbol *, uint32_t> slots_; // Instance methods: the method-table slot.
	std::unordered_map<std::string, uint32_t> strings_;
	std::unordered_map<uint64_t, uint32_t> numbers_; // A Number constant by its bits.
	std::unordered_map<int64_t, uint32_t> wides_;
	std::unordered_map<uint32_t, std::unordered_map<std::string, uint32_t>> slot_names_; // Per class.
	std::unordered_map<const Unit *, uint32_t> files_;

	// The function being lowered.
	Unit *unit_ = nullptr;
	uint32_t function_index_ = 0; // By index: the list of functions can grow while a body is lowered.
	Symbol *class_ = nullptr;
	bool constructor_ = false;
	const Type *result_type_ = nullptr; // What the function being lowered says it returns, if it says.
	std::unordered_map<const Symbol *, uint32_t> locals_;
	std::vector<Loop> loops_;
	std::vector<std::string_view> next_labels_; // Labels written just before the statement being lowered.
	// Types already refused where they were written: a value of one is not refused again at each use.
	std::unordered_set<const Type *> refused_types_;
	const Stmt *statement_ = nullptr; // The statement being lowered.
	const Stmt *refused_in_ = nullptr; // The last statement something was refused in.
	bool uses_refused_type(const Stmt *p_statement);
	size_t zeroes_ = 0; // How many package variables are zeroed, first of all the initializers.
	// The `try` statements around the code being lowered, outermost first: a `break`, `continue` or
	// `return` that leaves one runs its `finally` on the way.
	std::vector<TryBlock> tries_;
	// Expressions whose value is already in a register and must not be worked out again: the subject of
	// a `switch`, which every `case` is compared with.
	std::unordered_map<const Expr *, uint32_t> evaluated_;

	Function &fn() { return module_.functions[function_index_]; }
	const Function &fn() const { return module_.functions[function_index_]; }

	void refuse(uint32_t p_offset, const std::string &p_what);

	uint32_t string(const std::string &p_text);

	uint32_t wide(int64_t p_value);

	// A whole number written out (`12`, `0xFF`, `-5`) that an Int64 holds: its exact value. The checker
	// has refused one that is written where an Int64 is expected and does not fit.
	static std::optional<int64_t> wide_literal(const Expr *p_expression);

	uint32_t number(double p_value);

	uint32_t file(const Unit *p_unit);

	std::string class_name(const Symbol *p_class) const;

	bool kind_of(const Type *p_type, Kind *r_kind) const;

	// Declared in Godot's API.
	static bool is_godot(const Symbol *p_symbol);

	// A Godot class whose objects the engine owns (a node, a resource), as against a value type.
	bool is_godot_object(const Symbol *p_class) const;

	// Godot's own name for a member or (with no member) a class it declares.
	bool engine_name(const Symbol *p_class, std::string_view p_member, uint32_t *r_string);

	// The kind a written type names; refused if the interpreter has no such values yet.
	Kind written_kind(TypeRef *p_type, uint32_t p_offset, Kind p_missing);

	// A built-in class that is Error or extends it. These are the only built-in classes with objects so far.
	bool is_error(const Symbol *p_class) const;

	// The class's index, creating the built-in Error classes on first use.
	bool class_index(const Symbol *p_class, uint32_t *r_index);

	// Gives the class its place in the module, before any class is filled in: a field may be of a class
	// declared later.
	void reserve_class(ClassDecl *p_decl);

	Class inherited_class(ClassDecl *p_decl);
	uint32_t add_function(uint32_t p_class, const std::string &p_name, uint32_t p_position, bool p_static);
	void declare_fields(uint32_t p_class, VarDecl *p_decl);
	void record_godot_virtual(uint32_t p_class, const Symbol *p_symbol, FunctionDecl *p_function, uint32_t p_made);
	void give_method_slot(uint32_t p_class, FunctionDecl *p_function, uint32_t p_made);
	void declare_method(uint32_t p_class, const Symbol *p_symbol, FunctionDecl *p_function);
	void declare_class(Unit &p_unit, ClassDecl *p_decl);

	uint32_t emit(Op p_op, uint32_t p_position, uint32_t p_a = 0, uint32_t p_b = 0, uint32_t p_c = 0);

	uint32_t here() const { return (uint32_t)fn().code.size(); }

	void patch(uint32_t p_jump, uint32_t p_target);

	uint32_t reg(Kind p_kind);

	// A register for what the engine gives back. The host checks what arrives against the type.
	uint32_t engine_reg(Kind p_kind, const Type *p_type);

	Kind kind_at(uint32_t p_reg) const;

	uint32_t arguments(std::vector<uint32_t> p_list);

	// The default value of a kind, in a new register.
	uint32_t default_value(Kind p_kind, uint32_t p_position);

	// The value in p_reg as p_to, in a new register unless it is that kind already.
	// p_written: the program wrote the conversion itself (`int(x)`). That matters for an Int64 only: a
	// conversion that is not written raises a RangeError when the value does not fit, and one that is
	// written is AS3's own, which cuts, rounds or holds at the end.
	uint32_t convert(uint32_t p_reg, Kind p_to, uint32_t p_position, bool p_written = false);
	uint32_t convert_wide(uint32_t p_reg, Kind p_to, uint32_t p_position, bool p_written);

	Kind natural(Expr *p_expression);

	static bool is_null(const Expr *p_expression);

	// True if evaluating the expression can change a local variable.
	static bool writes_locals(Expr *p_expression);

	// A value that a later operand must not be able to change: a local's own register is copied if p_later
	// can write locals.
	uint32_t settled(uint32_t p_reg, Expr *p_later, uint32_t p_position);

	// Evaluates the expression and gives the register holding its value as p_want (Void: as it comes).
	uint32_t expression(Expr *p_expression, Kind p_want);

	static double literal_value(const NumberLit *p_literal);

	// The characters a string literal stands for.
	static std::string unescape(std::string_view p_text);

	// NaN, Infinity, and the limits of the number types: constants of the built-in library.
	bool builtin_constant(const Symbol *p_symbol, uint32_t p_position, uint32_t *r_reg);
	// Vectors. The class of a Vector.<T>, made the first time the type is met; nothing if its elements
	// are of a type the interpreter does not hold yet.
	static bool is_vector(const Type *p_type) { return p_type && p_type->kind == Type::Vector; }
	std::optional<uint32_t> vector_class(const Type *p_type);
	// The class a declaration says its value is of: a class of the program or a vector's. NONE otherwise.
	uint32_t declared_class(TypeRef *p_type);
	uint32_t vector_new(New *p_made);
	uint32_t vector_literal(VectorLit *p_literal);
	uint32_t vector_call(Call *p_call);
	uint32_t godot_object_of(uint32_t p_object, Expr *p_written, uint32_t p_position);
	// The untyped value: ir/lower_any.cpp.
	bool untyped(const Expr *p_expression) const;
	bool untyped_type(const Type *p_type) const;
	// A place of this kind and type starts null: an untyped one declared `Object` (a `*` starts undefined).
	bool starts_null_type(Kind p_kind, const Type *p_type) const { return p_kind == Kind::Any && !untyped_type(p_type); }
	// The empty value of an untyped place of this type: null for `Object`, undefined for `*`.
	uint32_t empty_untyped(const Type *p_type, uint32_t p_position);
	uint32_t any_value(Expr *p_expression);
	uint32_t any_text(Expr *p_expression);
	uint32_t any_equality(Binary *p_binary);
	uint32_t any_comparison(Binary *p_binary);
	uint32_t any_add(Binary *p_binary);
	uint32_t any_typeof(Expr *p_operand, uint32_t p_position);
	std::optional<uint32_t> any_type_test(Binary *p_binary);
	// An expression stored where a type is declared: a value from an untyped one is checked against the
	// class the place holds.
	uint32_t typed(Expr *p_expression, const Type *p_type, Kind p_kind);
	// A vector where Godot takes one of its own collections: converted, element by element, to the type
	// wanted (an Array where Godot takes anything). Nothing if the value is not a vector.
	std::optional<uint32_t> vector_to_engine(Expr *p_value, const Type *p_wanted);
	void vector_loop(ForIn *p_loop);
	void engine_loop(ForIn *p_loop);
	// What `===` can find equal: a number with a number, otherwise two values of one kind, and null with
	// anything that can be null.
	static bool can_be_equal(Kind p_a, Kind p_b);
	// Math's constants and functions. Nothing if the symbol is not one of them.
	std::optional<double> math_constant(const Symbol *p_symbol) const;
	std::optional<uint32_t> math_call(Call *p_call, const Symbol *p_function);

	Symbol * callee_symbol(Expr *p_callee);

	// Marks a use of a class from outside it: its static fields get their values the first time. Code inside
	// the class needs no mark, since it can only be running because the class was used.
	void touch(const Symbol *p_class, uint32_t p_position);

	// The object a member is reached through, checked for null unless it is `this` or `super`.
	uint32_t receiver(Expr *p_object);

	struct Part;
	// A place a value can be stored: a local, a field of an object, or a global.
	struct Place {
		// Element, Length and Fixed are of a Vector: `object` holds it, and for Element `key` the index.
		enum Where { Nowhere, Register, FieldOf, GlobalSlot, Engine, Element, Length, Fixed } where = Nowhere;
		uint32_t index = 0; // The register, the field slot, the global, or the string of Godot's name.
		uint32_t object = 0; // For FieldOf and Engine: the register holding the object.
		Kind kind = Kind::F64;
		bool of_value = false; // For Engine: a member of a value type, which is a copy.
		const Type *type = nullptr; // For Engine: what the member is declared to be.
		uint32_t key = NONE; // For Engine: the place is an element (`value[key]`); the register holding the key.
		bool constant = false; // One of Godot's constants: read from a register, and never written.
		// For Engine: the script object whose Godot object `object` is, fetched when the place is read or
		// written, so that a missing one is reported after the value is worked out, as in AS3.
		uint32_t script_object = NONE;
		// For Engine: the member is one part of a Godot value that is itself stored somewhere
		// (`position.x`). Godot's values are copies, so writing the part is: read the whole value, change
		// the part, write the whole value back.
		Part *part = nullptr; // In parts_.
	};
	struct Part {
		Place whole; // Where the value is stored.
		uint32_t value = NONE; // The register it was read into, once it has been.
	};
	// The parts of Godot values that places refer to (see Place::part): a deque, so that each stays where
	// it is as more are made. Kept for the whole lowering: places are short-lived, and parts are small.
	std::deque<Part> parts_;

	// The value a part belongs to. It is read where the place is worked out, before anything to the right
	// of it runs, as GDScript does: `position.x = f()` keeps the `y` that `position` had before `f()`.
	uint32_t whole_of(const Place &p_place, uint32_t p_position);
	// An element of a vector, or its length or `fixed`, as a place. Nothing if the target is none of those.
	std::optional<Place> vector_place(Expr *p_target, Expr *p_later);

	// The engine object a Godot member is reached through: the script's own owner when nothing is
	// written before the name, or the value of what is written.
	uint32_t engine_receiver(Expr *p_object, uint32_t p_position, bool *r_of_value, uint32_t *r_script_object = nullptr);
	void owner_of(uint32_t p_script_object, uint32_t p_owner, uint32_t p_position);

	// One of Godot's values that is made of parts (a Vector2's x and y, a Color's r, g, b and a) is never
	// missing unless a program says so: a variable of it with no value written starts at the type's zero,
	// as it does in GDScript. This gives that zero in a new register, or NONE for any other type (an array,
	// a signal, a function), which starts missing as an object does.
	uint32_t zero_value(TypeRef *p_written, uint32_t p_position, uint32_t p_into = NONE);

	// One of Godot's values that stores 32-bit integers: every part of it is an `int`, or is such a value
	// (Vector2i; Rect2i, which is two of them). Godot cuts a wider integer off inside these without a
	// word, so a whole number on its way into one goes as an `int`, and says so if it does not fit.
	bool stores_int32(const Type *p_type) const;

	// A whole number in p_reg, as the `int` a 32-bit value takes: checked, never cut.
	uint32_t as_int32(uint32_t p_reg, uint32_t p_position);

	// A class Godot keeps one object of (Input): tagged [Singleton] in the declarations.
	static bool is_singleton(const Symbol *p_class);
	static bool on_object(const Expr *p_member);
	bool stored_element(const Expr *p_expression);

	// The engine's one object of a singleton class. Everything written `Input.x` is then a member of it.
	uint32_t singleton(const Symbol *p_class, uint32_t p_position);

	// One of Godot's constants. One with a value in the declarations (`Key.ESCAPE`) is that value; any
	// other (`Vector2.ZERO`) is asked of the engine.
	Place constant_place(Symbol *p_symbol, uint32_t p_position);

	// A property or field of an engine object.
	Place engine_place(Symbol *p_symbol, Expr *p_object, uint32_t p_position, Expr *p_later = nullptr);

	Place place_of_symbol(Symbol *p_symbol, uint32_t p_object, uint32_t p_position);

	// Evaluates what is needed to reach the place: the object of `a.b`, once.
	Place place_of(Expr *p_target, Expr *p_later = nullptr);

	uint32_t load(const Place &p_place, uint32_t p_position);

	void store(const Place &p_place, uint32_t p_value, uint32_t p_position);

	// The class a `new`, `is`, `as` or conversion names.
	Symbol * named_class(Expr *p_expression);

	bool kind_of_class(const Symbol *p_class, Kind *r_kind);

	// The arguments of a call as the function's parameters expect them, defaults filled in.
	void call_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list);

	// The first of the expressions that can change a local variable, or null.
	static Expr * writer(const List<Expr *> &p_expressions);

	uint32_t call_result(Symbol *p_function, uint32_t p_position, Kind *r_kind);

	uint32_t super_call(Call *p_call);
	std::optional<uint32_t> signal_call_by_name(Call *p_call);
	uint32_t cast(Call *p_call, Symbol *p_type, Kind p_want);
	uint32_t trace_call(Call *p_call);
	uint32_t engine_text(Call *p_call);
	uint32_t godot_call(Call *p_call, Symbol *p_method);
	std::string defaults_key(const Symbol *p_method) const;
	uint32_t global_call(Call *p_call, Symbol *p_function, Kind p_want);
	uint32_t script_call(Call *p_call, Symbol *p_symbol, uint32_t p_function);
	uint32_t call(Call *p_call, Kind p_want);

	// A method written where Godot takes a function (`onHit`, `this.onHit`, `other.onHit`): the method
	// bound to its object. A private method is that very function; any other is the object's own override,
	// found when the line runs.
	uint32_t bound_method(Expr *p_expression);

	// `object[name]` where a signal's member follows: the signal, by its AS3 name. NONE if this is not one.
	// The object and the name are worked out here; the register is filled by a HostMember that the caller
	// emits once the call's arguments are worked out, so that a wrong name fails after them, as a wrong
	// member does in AS3.
	uint32_t signal_by_name(Expr *p_expression, Expr *p_later, uint32_t *r_object, uint32_t *r_name);

	// Arguments for the engine: each as its parameter says, or as it comes where the parameter takes anything.
	// p_defaults: "Class.method" in Godot's names, for a method whose defaults Godot's description gives.
	void engine_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list,
			const std::string &p_defaults = std::string());

	// An instruction a call still needs once its arguments are worked out (the signal it is made on).
	struct Later {
		Op op;
		uint32_t position, a, b, c;
	};
	uint32_t engine_call(Call *p_call, Symbol *p_method, uint32_t p_receiver = NONE, std::optional<Later> p_before = std::nullopt);

	// A method of Godot's that is called on no object: `Vector2.fromAngle(a)`.
	uint32_t static_call(Call *p_call, Symbol *p_method);

	void constructor_arguments(Symbol *p_class, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list);

	uint32_t evaluate(Expr *p_expression, Kind p_want);
	uint32_t number_literal(Expr *p_expression, Kind p_want);
	uint32_t number_constant(double p_value, Kind p_kind, Kind p_want, uint32_t p_position);
	uint32_t written_value(Expr *p_value, Kind p_conversion, uint32_t p_position);
	uint32_t wide_order(Expr *p_left, Expr *p_right, uint32_t p_position);
	uint32_t number_zero(uint32_t p_position);
	uint32_t name_value(Expr *p_expression);
	uint32_t member_value(Expr *p_expression);
	uint32_t new_value(Expr *p_expression);
	uint32_t postfix(Expr *p_expression);
	uint32_t conditional(Expr *p_expression, Kind p_want);

	uint32_t unary(Unary *p_unary, Kind p_want);

	static bool is_numeric(Kind p_kind);
	static bool is_integral(Kind p_kind);

	// An operand beside an Int64 (p_beside). A whole number written out is then an Int64 too, read digit
	// by digit, as the checker took it: `w == 9223372036854775807` compares with that very number.
	uint32_t wide_operand(Expr *p_expression, Kind p_beside);
	// The kind an operand has for an operator: an Int64 for a whole number written beside one.
	Kind operand_kind(Expr *p_expression, Expr *p_other);

	// An operand for the engine, as the checker typed it: Godot tells a whole number from a fraction
	// (`Vector2i * 2` is a Vector2i), and the interpreter's own arithmetic is all in fractions.
	uint32_t operand(Expr *p_expression, const Type *p_beside = nullptr);

	// The checker's rule for a whole number (names_types.cpp, `whole`): an int or uint, or sums,
	// differences, products and remainders of such. AS3 works `i + 1` out as a Number, but the checker
	// gave `cell * (i + 1)` the type Godot gives a whole number, so a whole number is what Godot must get.
	bool whole(const Expr *p_expression);

	// An operator that Godot carries out, because an operand is one of its values. The operator goes as
	// AS3 writes it; `v += w` is `v + w`.
	uint32_t engine_operate(TokenKind p_op, std::vector<uint32_t> p_operands, Kind p_result, const Type *p_type, uint32_t p_position);

	// An arithmetic, bitwise or string operation on two values already evaluated.
	uint32_t concatenate(uint32_t p_left, uint32_t p_right, uint32_t p_position);
	uint32_t operate(TokenKind p_op, uint32_t p_left, uint32_t p_right, bool p_string, uint32_t p_position);

	bool is_string_type(const Type *p_type) const;

	uint32_t binary(Binary *p_binary, Kind p_want);
	uint32_t logical(Binary *p_binary, Kind p_want);
	uint32_t type_test(Binary *p_binary);
	uint32_t comparison(Binary *p_binary);
	uint32_t equality(Binary *p_binary);
	uint32_t arithmetic(Binary *p_binary, Kind p_want);

	uint32_t assign(Assign *p_assign);

	void declare_locals(Node *p_body);

	void statement(Stmt *p_statement);
	void if_statement(Stmt *p_statement);
	void while_loop(Stmt *p_statement);
	void do_while_loop(Stmt *p_statement);
	void for_loop(Stmt *p_statement);
	void switch_statement(Stmt *p_statement);
	void try_statement(Try *p_try);
	void labeled(Labeled *p_labeled);
	// Leaves for a loop (break or continue) or the function (return), through the `finally` of every
	// try statement on the way: the innermost one runs first, and goes on from there.
	void leave_to(size_t p_loop, bool p_break, uint32_t p_position);
	void leave_return(uint32_t p_position, uint32_t p_value); // p_value NONE for a function that gives nothing.
	void route(size_t p_try, TryExit p_exit);
	void finish_finally(TryBlock &p_try, uint32_t p_error, uint32_t p_position);
	void open_loop(); // A loop or switch begins: it takes the label written before it, if any.
	uint32_t catch_type(const CatchClause &p_clause);
	void break_or_continue(Stmt *p_statement);
	void return_statement(Stmt *p_statement);
	void throw_statement(Stmt *p_statement);

	void finish_loop(uint32_t p_continue_at);

	void variables(VarDecl *p_decl);

	void begin(uint32_t p_function, Unit &p_unit, Symbol *p_class);

	// Parameters take the first registers; an instance method's first is `this`.
	void parameters(FunctionDecl *p_function);

	void end(uint32_t p_position);

	void lower_function(Unit &p_unit, Symbol *p_class, FunctionDecl *p_decl);

	static bool calls_super(Block *p_body);

	void lower_constructor(Unit &p_unit, ClassDecl *p_class);

	// A function that gives the class's static fields their values.
	void lower_statics(Unit &p_unit, ClassDecl *p_class);

	void lower_unit(Unit &p_unit);

	void lower_package_variables(Unit &p_unit, VarDecl *p_decl);

	// Classes in an order where each comes after its base.
	void collect(Unit &p_unit, std::vector<std::pair<Unit *, ClassDecl *>> &r_classes);

public:
	Lowerer(Compilation &p_compilation, Module &p_module);

	using ClassInUnit = std::pair<Unit *, ClassDecl *>;
	void find_error_classes();
	std::vector<ClassInUnit> classes_in_order(const std::vector<Unit *> &p_units);
	void declare_package_function(Unit *p_unit, FunctionDecl *p_decl);
	void declare_package_level(Unit *p_unit, const List<Node *> &p_definitions);
	void find_entry_class(Unit *p_first);
	bool run(LowerMode p_mode);
};

} // namespace ir
} // namespace kas3
