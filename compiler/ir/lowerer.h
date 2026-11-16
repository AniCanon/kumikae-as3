// The lowerer: turns a checked program into the typed IR. One class; its methods are in the lower_*.cpp
// files, by what they lower. Nothing outside ir/ includes this.
#pragma once

#include "lower.h"

#include "ast_walk.h"
#include "builtins.h"
#include "lower_env.h"
#include "lower_flow.h"
#include "whole_numbers.h"
#include <algorithm>
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
	Symbol *array_ = nullptr; // Array, and the one class all Arrays are of, once made
	uint32_t array_class_ = NONE;

	std::unordered_map<const Symbol *, uint32_t> classes_;
	std::unordered_map<const Type *, uint32_t> vector_classes_; // The class made for each Vector.<T> met.
	std::unordered_map<const FunctionDecl *, uint32_t> functions_; // Every function of the program, by declaration.
	std::unordered_map<const Symbol *, uint32_t> fields_; // Instance fields: the slot.
	std::unordered_map<const Symbol *, uint32_t> globals_; // Static fields and package variables.
	std::unordered_map<const FunctionDecl *, uint32_t> slots_; // Table slots; for an interface's, its number.
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
	// The function's environment (lower_closures.cpp): its register and layout, if one written inside uses it;
	// whether it was given one (a function expression, in r0); the layouts reachable from that one, nearest
	// first; and the function expressions met, lowered once the function they are in is done.
	uint32_t own_env_reg_ = NONE;
	EnvLayout own_layout_;
	bool received_env_ = false;
	std::vector<EnvLayout> chain_;
	std::vector<PendingFunction> pending_functions_;
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

	// A built-in class that is Error or extends it.
	bool is_error(const Symbol *p_class) const;

	// The class's index, creating the built-in Error classes on first use.
	bool class_index(const Symbol *p_class, uint32_t *r_index);
	// Error, or one of its built-in subclasses, made the first time it is used (class_index()).
	bool error_class(const Symbol *p_class, uint32_t *r_index);

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
	// Between int, uint, Number, Boolean and String; an object to a Boolean or a String.
	uint32_t convert_primitive(uint32_t p_reg, Kind p_to, uint32_t p_position);

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
	// Vectors: a Vector.<T>, or an Array (a vector of untyped elements with rules of its own). The class of
	// a Vector.<T> is made the first time the type is met; nothing if its elements are of a kind not held.
	bool is_vector(const Type *p_type) const { return p_type && (p_type->kind == Type::Vector || (array_ && p_type->symbol == array_)); }
	std::optional<uint32_t> vector_class(const Type *p_type);
	// The class a declaration says its value is of: a class of the program or a vector's. NONE otherwise.
	uint32_t declared_class(TypeRef *p_type);
	// The untyped value: ir/lower_any.cpp.
	bool untyped(const Expr *p_expression) const;
	bool untyped_type(const Type *p_type) const;
	// A place of this kind and type starts null: an untyped one declared `Object` (a `*` starts undefined).
	bool starts_null_type(Kind p_kind, const Type *p_type) const { return p_kind == Kind::Any && !untyped_type(p_type); }
	// The empty value of an untyped place of this type: null for `Object`, undefined for `*`.
	uint32_t empty_untyped(const Type *p_type, uint32_t p_position);
	uint32_t any_value(Expr *p_expression);
	uint32_t any_text(Expr *p_expression);
	uint32_t any_typeof(Expr *p_operand, uint32_t p_position);
	// An expression stored where a type is declared: a value from an untyped one is checked against the
	// class the place holds.
	uint32_t typed(Expr *p_expression, const Type *p_type, Kind p_kind);
	uint32_t from_untyped(uint32_t p_any, const Type *p_type, Kind p_kind, uint32_t p_position);
	// What `===` can find equal: a number with a number, otherwise two values of one kind, and null with
	// anything that can be null.
	static bool can_be_equal(Kind p_a, Kind p_b);
	// Math's constants and functions. Nothing if the symbol is not one of them.
	std::optional<double> math_constant(const Symbol *p_symbol) const;
	std::optional<uint32_t> math_call(Call *p_call, const Symbol *p_function);
	// String's and the numbers' methods, the top-level functions, and Object's toString() (lower_library).
	friend class Library;
	friend class Members; // Getters, setters and interfaces (lower_members.cpp).
	friend class Dynamic; // Properties by name: lower_dynamic.h
	friend class EngineCalls; // Calls of Godot's methods and functions: lower_engine_calls.h
	friend class Statements; // Statements and where control goes: lower_statements.h
	friend class Operators; // Operators on two values, and `is` and `as`: lower_operators.h
	friend class Engine; // Godot's members, values and operators: lower_engine.h
	friend class Vectors; // A vector's lowering, once its class is made: lower_vectors.h
	friend class Functions; // Function expressions and function values: lower_functions.h
	friend class Classes; // Each class's place in the module: lower_classes.h
	friend class Bodies; // The body of each function: lower_bodies.h
	friend class Calls; // Calls: lower_calls.h
	friend class Expressions; // Each kind of expression: lower_expressions.h
	std::optional<uint32_t> library_call(Call *p_call, Symbol *p_function);
	Symbol * callee_symbol(Expr *p_callee);

	// Marks a use of a class from outside it: its static fields get their values the first time. Code inside
	// the class needs no mark, since it can only be running because the class was used.
	void touch(const Symbol *p_class, uint32_t p_position);

	// The object a member is reached through, checked for null unless it is `this` or `super`.
	uint32_t receiver(Expr *p_object);
	uint32_t self(uint32_t p_position, bool p_written = false); // `this`; in a function expression, the method's object (lower_closures).

	using Place = ir::Place; // lower_env.h
	using Part = ir::Part;
	// The parts of Godot values that places refer to (see Place::part): a deque, so that each stays where
	// it is as more are made. Kept for the whole lowering: places are short-lived, and parts are small.
	std::deque<Part> parts_;

	// The value a part belongs to. It is read where the place is worked out, before anything to the right
	// of it runs, as GDScript does: `position.x = f()` keeps the `y` that `position` had before `f()`.
	uint32_t whole_of(const Place &p_place, uint32_t p_position);

	bool extends_counted(const Symbol *p_class) const;

	Place place_of_symbol(Symbol *p_symbol, uint32_t p_object, uint32_t p_position);

	// Evaluates what is needed to reach the place: the object of `a.b`, once.
	Place place_of(Expr *p_target, Expr *p_later = nullptr);
	Place place_of_name(Name *p_name, Expr *p_later);
	Place place_of_member(Member *p_member, Expr *p_later);
	Place place_of_index(Index *p_index, Expr *p_later);

	uint32_t load(const Place &p_place, uint32_t p_position);

	void store(const Place &p_place, uint32_t p_value, uint32_t p_position);
	void store_engine(const Place &p_place, uint32_t p_value, uint32_t p_position);

	// The class a `new`, `is`, `as` or conversion names.
	Symbol * named_class(Expr *p_expression);

	bool kind_of_class(const Symbol *p_class, Kind *r_kind);

	// The first of the expressions from `p_from` up to `p_end` (all, by default) that can change a local, or null.
	static Expr *writer(const List<Expr *> &p_expressions, uint32_t p_from = 0, uint32_t p_end = UINT32_MAX);

	uint32_t number_literal(Expr *p_expression, Kind p_want);
	uint32_t number_constant(double p_value, Kind p_kind, Kind p_want, uint32_t p_position);
	static bool is_numeric(Kind p_kind);
	static bool is_integral(Kind p_kind);

	// An operand beside an Int64 (p_beside). A whole number written out is then an Int64 too, read digit
	// by digit, as the checker took it: `w == 9223372036854775807` compares with that very number.
	uint32_t wide_operand(Expr *p_expression, Kind p_beside);
	// The kind an operand has for an operator: an Int64 for a whole number written beside one.
	Kind operand_kind(Expr *p_expression, Expr *p_other);

	// The checker's rule for a whole number (names_types.cpp, `whole`): an int or uint, or sums,
	// differences, products and remainders of such. AS3 works `i + 1` out as a Number, but the checker
	// gave `cell * (i + 1)` the type Godot gives a whole number, so a whole number is what Godot must get.
	bool whole(const Expr *p_expression);

	bool is_string_type(const Type *p_type) const;

	void lower_unit(Unit &p_unit);

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
