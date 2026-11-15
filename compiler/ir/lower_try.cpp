#include "lowerer.h"

namespace kas3 {
namespace ir {

// A statement with a label: a loop or a switch takes the label; any other statement is a block that
// `break label` leaves.
void Lowerer::labeled(Labeled *p_labeled) {
	Stmt *body = p_labeled->body;
	const bool loop = body && (body->kind == NodeKind::While || body->kind == NodeKind::DoWhile || body->kind == NodeKind::For ||
									  body->kind == NodeKind::ForIn || body->kind == NodeKind::Switch);
	if (loop) {
		next_label_ = p_labeled->label;
		statement(body);
		next_label_ = std::string_view();
		return;
	}
	open_loop();
	loops_.back().is_block = true;
	loops_.back().label = p_labeled->label;
	statement(body);
	finish_loop(here());
}

void Lowerer::run_finallies(size_t p_keep) {
	// Each runs with the ones outside it still open: an error in it is caught around it.
	std::vector<Block *> saved = finallies_;
	while (finallies_.size() > p_keep) {
		Block *block = finallies_.back();
		finallies_.pop_back();
		statement(block);
	}
	finallies_ = std::move(saved);
}

// What a catch takes: its class, a test of the value for String, int, uint, Number and Boolean, or
// anything (NONE) for `*`, `Object` or no type.
uint32_t Lowerer::catch_type(const CatchClause &p_clause) {
	const Type *type = p_clause.type ? compilation_.type_of(p_clause.type) : nullptr;
	const Symbol *symbol = type && type->kind == Type::Instance ? type->symbol : nullptr;
	uint32_t index = NONE;
	if (!symbol || symbol == object_) {
		return NONE;
	}
	if (symbol == string_) {
		return ANY_IS_STRING;
	}
	if (symbol == int_) {
		return ANY_IS_INT;
	}
	if (symbol == uint_) {
		return ANY_IS_UINT;
	}
	if (symbol == number_) {
		return ANY_IS_NUMBER;
	}
	if (symbol == boolean_) {
		return ANY_IS_BOOLEAN;
	}
	if (!class_index(symbol, &index)) {
		refuse(p_clause.offset, "catching a '" + compilation_.type_name(type) + "'");
		return NONE;
	}
	return index;
}

// try { body } catch (e:T) { ... } ... finally { ... }
//
// Laid out as: the body, then a jump to the end; each catch, then a jump to the end; for a finally,
// a catch of anything that runs it and throws again; and at the end, the finally for every way out
// that is not an error. A break, continue or return inside runs the finally before it leaves.
void Lowerer::try_statement(Try *p_try) {
	const uint32_t position = p_try->offset;
	if (p_try->finally_block) {
		finallies_.push_back(p_try->finally_block);
	}
	const uint32_t body_start = here();
	statement(p_try->body);
	const uint32_t body_end = here();
	std::vector<uint32_t> to_end = { emit(Op::Jump, position, 0) };
	std::vector<Handler> handlers;
	for (CatchClause &clause : p_try->catches) {
		const auto local = locals_.find(clause.symbol);
		if (local == locals_.end()) {
			refuse(clause.offset, "this catch");
			continue;
		}
		Handler handler;
		handler.start = body_start;
		handler.end = body_end;
		handler.target = here();
		handler.reg = local->second;
		handler.type = catch_type(clause);
		handlers.push_back(handler);
		emit(Op::Stmt, clause.offset);
		statement(clause.body);
		to_end.push_back(emit(Op::Jump, position, 0));
	}
	const uint32_t catches_end = here();
	fn().handlers.insert(fn().handlers.end(), handlers.begin(), handlers.end());
	if (p_try->finally_block) {
		finallies_.pop_back();
		// An error in the body or a catch: the finally runs, and the error goes on.
		Handler anything;
		anything.start = body_start;
		anything.end = catches_end;
		anything.target = here();
		anything.reg = reg(Kind::Any);
		fn().handlers.push_back(anything);
		statement(p_try->finally_block);
		emit(Op::Throw, position, anything.reg);
	}
	for (uint32_t jump : to_end) {
		patch(jump, here());
	}
	if (p_try->finally_block) {
		statement(p_try->finally_block);
	}
}

} // namespace ir
} // namespace kas3
