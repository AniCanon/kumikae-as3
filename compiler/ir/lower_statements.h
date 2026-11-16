// Statements, and where control goes: loops, `switch`, `try`, labels, `break`, `continue`, `return` and
// `throw` (lower_statements.cpp, lower_try.cpp). A helper the lowerer makes where it needs one; what it keeps
// of the loops and `try` statements around the code (lower_flow.h) is the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Statements {
	Lowerer &l_;

	void by_kind(Stmt *p_statement);
	void if_statement(Stmt *p_statement);
	void while_loop(Stmt *p_statement);
	void do_while_loop(Stmt *p_statement);
	void for_loop(Stmt *p_statement);
	void switch_statement(Stmt *p_statement);
	void for_in_loop(ForIn *p_loop);
	void try_statement(Try *p_try);
	void finally_on_error(TryBlock &p_block, uint32_t p_start, uint32_t p_end, uint32_t p_position);
	void labeled(Labeled *p_labeled);
	// Leaves for a loop (break or continue) or the function (return), through the `finally` of every
	// try statement on the way: the innermost one runs first, and goes on from there.
	void leave_to(size_t p_loop, bool p_break, uint32_t p_position);
	void leave_return(uint32_t p_position, uint32_t p_value); // p_value NONE for a function that gives nothing.
	void route(size_t p_try, TryExit p_exit);
	void finish_finally(TryBlock &p_try, uint32_t p_error, uint32_t p_position);
	uint32_t catch_type(const CatchClause &p_clause);
	void break_or_continue(Stmt *p_statement);
	void return_statement(Stmt *p_statement);
	void throw_statement(Stmt *p_statement);

	void variables(VarDecl *p_decl);

public:
	explicit Statements(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	void declare_locals(Node *p_body);

	void statement(Stmt *p_statement);
	void open_loop(); // A loop or switch begins: it takes the label written before it, if any.
	void finish_loop(uint32_t p_continue_at);
};

} // namespace ir
} // namespace kas3
