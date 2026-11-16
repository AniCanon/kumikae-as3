// The pass that gives every expression its type and reports a value that does not fit where it is used.
// One class; its methods are in the typer_*.cpp files, by what they check. Nothing outside src/ includes
// this.
#pragma once

#include "names.h"
#include "whole_numbers.h"
#include <map>
#include <unordered_set>
#include <algorithm>
#include <cstdlib>

namespace kas3 {

class Typer {
	Compilation &compilation_;
	Unit &unit_;
	Symbol *class_ = nullptr; // The class whose member is being typed, if any.
	bool static_ = false;
	int closures_ = 0; // How many function expressions or nested functions enclose the current code.
	FunctionDecl *current_ = nullptr; // The innermost function being typed.
	mutable std::map<std::pair<Symbol *, Symbol *>, std::pair<bool, bool>> related_; // is_a's answers.
	bool leaves_ = false; // It has a `return <value>` or a `throw` somewhere in its own body.
	Expr *assigned_ = nullptr; // The target of the `=` being typed: written, not read.

	const Type *any_;
	const Type *unknown_;
	const Type *int_;
	const Type *uint_;
	const Type *number_;
	const Type *int64_;
	const Type *boolean_;
	const Type *string_;
	const Type *array_;
	const Type *object_;
	const Type *dictionary_ = nullptr; // flash.utils.Dictionary
	Package *godot_ = nullptr; // Godot's API, if the program has it.
	const Type *function_;
	const Type *regexp_;
	const Type *class_type_;

	void error(uint32_t p_offset, std::string p_message) { unit_.diagnostics.error(p_offset, std::move(p_message)); }
	void warning(uint32_t p_offset, std::string p_message) { unit_.diagnostics.warning(p_offset, std::move(p_message)); }
	void no_type(uint32_t p_offset, std::string_view p_what);



	bool numeric(const Type *p_type) const { return p_type == int_ || p_type == uint_ || p_type == number_ || p_type == int64_; }
	// The types that hold whole numbers only.
	bool integral(const Type *p_type) const { return p_type == int_ || p_type == uint_ || p_type == int64_; }
	// The types that AS3's arithmetic is defined on without calling into a class.
	bool primitive(const Type *p_type) const;

	// The one type that both fit, for `a ? b : c`, `a || b` and `a && b`.
	const Type *join(const Type *p_a, const Type *p_b) const;

	enum class Fit {
		Yes,
		Cast, // The value may be of the type, but only a cast can say so.
		No,
	};

	// Whether an object of p_class is always one of p_target: the class, a base, or an interface that it,
	// a base, or one of those interfaces declares. *r_unsure is set if part of the hierarchy is not known.
	bool is_a(Symbol *p_class, Symbol *p_target, bool *r_unsure) const;

	bool walk_is_a(Symbol *p_class, Symbol *p_target, bool *r_unsure) const;

	// May a value of type p_from be used where p_to is expected, with no cast written?
	// True if the type, or an element type inside it, did not resolve.
	static bool unresolved(const Type *p_type);

	// AS3's own collections are converted when they are handed to the engine: an Array or a Vector becomes
	// a Godot array, a Vector becomes the packed array of its element type, and an Object or a
	// flash.utils.Dictionary becomes a Godot dictionary. Nothing converts on the way back.
	bool converts_to_engine(const Type *p_from, const Type *p_to) const;

	Fit fits(const Type *p_from, const Type *p_to) const;

	// Reports a value that does not fit where it is stored or passed.
	void store(Expr *p_value, const Type *p_to);

	// Reports a write to something that can't be written: `x = ...`, `x += ...`, `x++`.
	void written(Expr *p_target);
	// Reports a read of a property that has no getter: `x`, `o.x`, and the read in `x += 1` or `x++`.
	void read(Expr *p_expression);
	// The property a name or a member names, the interface or class it is reached through, and where.
	Symbol *property_of(Expr *p_expression, Symbol **r_through, uint32_t *r_offset);

	const Type *symbol_type(Symbol *p_symbol, const Type *p_element);

	bool derives_from(Symbol *p_class, Symbol *p_base) const;

	// May the code being typed name this member through `.`? Reports and returns false if not.
	bool accessible(Symbol *p_member, uint32_t p_offset);

	// The member of that name that an object of the class or interface has, whatever its visibility.
	Symbol *instance_member(Symbol *p_class, std::string_view p_name);

	bool members_known(Symbol *p_class);

	const Type *member_type(const Type *p_object, Member *p_member);

	const Type *number_literal(const NumberLit *p_literal) const;

	// The value of a literal written as a whole number, if it is small enough to matter here.
	bool whole_value(const NumberLit *p_literal, unsigned long long *r_value) const;

	void list(const List<Expr *> &p_list);

	static std::string counted(size_t p_count, const char *p_noun);

	// Checks the arguments of a call against the function's parameters: how many, then each one's type.
	void arguments(FunctionDecl *p_function, std::string_view p_name, const List<Expr *> &p_arguments, uint32_t p_offset, const Type *p_element);

	// A class in a declaration file that is tagged [Abstract]: one the engine does not let a program create.
	static bool abstract(const Type *p_instance);

	// The arguments of `new C(...)` or `super(...)`.
	void construct(const Type *p_instance, const List<Expr *> &p_arguments, uint32_t p_offset);

	// Where the name of what is being called or constructed is written.
	static uint32_t name_offset(const Expr *p_callee);

	const Type *call(Call *p_call);

	// A value that may be a function or a class, known only when the program runs.
	bool dynamic_value(const Type *p_type) const;

	//
	// A class in a declaration file may carry [Operator(op, right or left, returns)] and [Indexed(element,
	// keyed)] tags. Godot's value types do: its API description lists every operator. The tags are read once
	// per class.

	struct OperatorRule {
		std::string_view op;
		enum Side : uint8_t { Right, Left, Unary } side = Right; // Where the other operand is.
		const Type *other = nullptr; // Null for a unary operator.
		const Type *result = nullptr;
	};
	struct Described {
		std::vector<OperatorRule> operators;
		const Type *element = nullptr; // What indexing gives; null if the class is not indexed.
		bool keyed = false; // Indexed by any key, not only by a number.
		bool complete = false; // The operators listed are all it has: another use is an error.
		// The forms of its constructor, each its parameters' types, when it has more than one. The
		// declared constructor merges them, so it takes calls that none of them takes.
		std::vector<std::vector<const Type *>> forms;
	};
	std::unordered_map<Symbol *, Described> described_;

	// The type a tag names: `*`, or a class or interface in the tagged class's package, in the top-level
	// package, or written with its package. An unknown name is reported where the tag is.
	const Type *tag_type(Symbol *p_class, const Metadata &p_tag, std::string_view p_name);

	// What the class's declaration says about operators and indexing, or null if it says nothing.
	const Described *described(const Type *p_type);

	// Whether the class's declaration lists its operators, so that one it does not list is an error.
	bool has_operators(const Type *p_type);

	// An expression whose value is a whole number: an int or uint, or sums, differences, products and
	// remainders of such. AS3 computes `i + 1` as a Number, but Godot gives Vector2i * int a Vector2i and
	// Vector2i * float a Vector2, and `cell * (i + 1)` must stay on the grid.
	bool whole(const Expr *p_expression) const;

	// Whether an operand of type p_type is what a rule asks for. *r_exact is cleared when a whole number
	// meets a rule for Number, or the reverse: such a rule is used only if no other matches.
	bool operand_matches(const Expr *p_operand, const Type *p_type, const Type *p_wanted, bool *r_exact) const;

	// An operand that could be anything: nothing is said about an operator on it.
	bool open_ended(const Type *p_type) const;

	// The type of `left op right` where a declaration describes operators for one of the operands. Null
	// if neither class describes any: the caller then does what it did before. Reports an operator that
	// the classes don't have.
	const Type *described_binary(TokenKind p_op, Expr *p_left, Expr *p_right, uint32_t p_offset);
	// A `new` of a class whose constructor has forms: one of them must take the arguments.
	void constructor_form(const Type *p_instance, const List<Expr *> &p_arguments, uint32_t p_offset);

	// The same for `-x` and `+x`.
	const Type *described_unary(TokenKind p_op, const Type *p_operand, uint32_t p_offset);

	// For the operators no class describes (`++`, `--`, `~`, and the bit assignments): a type whose
	// operators are all known has none of them. True if that was reported.
	bool never_operator(TokenKind p_op, const Type *p_operand, uint32_t p_offset);

	// The plain operator of a compound assignment, or Error if it has none that a class could describe.
	static TokenKind compound_operator(TokenKind p_op);

	const Type *unary(Unary *p_unary);

	const Type *arithmetic(TokenKind p_op, const Type *p_left, const Type *p_right);

	const Type *binary(Binary *p_binary);

	const Type *compute(Expr *p_expression);
	const Type *member_expression_type(Expr *p_expression);
	const Type *index_type(Expr *p_expression);
	const Type *new_type(Expr *p_expression);
	const Type *assign_type(Expr *p_expression);

	const Type *expression(Expr *p_expression);

	// An expression whose type `--dump-types` shows.
	void shown(Expr *p_expression);

	void declarators(VarDecl *p_decl);
	void signal_field(VarDecl *p_decl);
	void export_field(VarDecl *p_decl);
	bool godot_storable(const Type *p_type, bool p_in_node) const;
	bool godot_value(const Type *p_type) const;

	void loop_statement(Stmt *p_statement);
	void for_in(ForIn *p_loop);
	void switch_statement(Switch *p_switch);
	void try_statement(Try *p_try);
	void statement(Stmt *p_statement);

	void returned(Return *p_return);

	void function(FunctionDecl *p_function);

	void definition(Node *p_node);

	const Type *builtin(const char *p_name);

public:
	Typer(Compilation &p_compilation, Unit &p_unit);

	void run();
};

} // namespace kas3
