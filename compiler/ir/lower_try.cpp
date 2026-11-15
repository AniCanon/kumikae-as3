#include "lowerer.h"

#include <algorithm>

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
	const size_t open = tries_.size();
	for (size_t i = open; i-- > p_keep;) {
		if (tries_[i].hidden || !tries_[i].finally_block) {
			continue;
		}
		// Written as if where it stands in the source: the try statements from this one in, and the
		// loops opened inside it, are out of its reach; what it throws is outside their handlers.
		std::vector<size_t> hid_tries;
		std::vector<size_t> hid_loops;
		for (size_t j = i; j < open; j++) {
			if (!tries_[j].hidden) {
				tries_[j].hidden = true;
				hid_tries.push_back(j);
			}
		}
		for (size_t l = tries_[i].loops; l < loops_.size(); l++) {
			if (!loops_[l].hidden) {
				loops_[l].hidden = true;
				hid_loops.push_back(l);
			}
		}
		const uint32_t start = here();
		statement(tries_[i].finally_block);
		const uint32_t end = here();
		for (size_t j = i; j < open; j++) {
			tries_[j].holes.push_back({ start, end });
		}
		for (size_t j : hid_tries) {
			tries_[j].hidden = false;
		}
		for (size_t l : hid_loops) {
			loops_[l].hidden = false;
		}
	}
}

void Lowerer::protect(uint32_t p_start, uint32_t p_end, const std::vector<std::pair<uint32_t, uint32_t>> &p_holes, Handler p_handler) {
	std::vector<std::pair<uint32_t, uint32_t>> holes = p_holes;
	std::sort(holes.begin(), holes.end());
	uint32_t from = p_start;
	for (const auto &[hole_start, hole_end] : holes) {
		if (hole_end <= from || hole_start >= p_end) {
			continue;
		}
		if (hole_start > from) {
			p_handler.start = from;
			p_handler.end = hole_start;
			fn().handlers.push_back(p_handler);
		}
		from = std::max(from, hole_end);
	}
	if (from < p_end) {
		p_handler.start = from;
		p_handler.end = p_end;
		fn().handlers.push_back(p_handler);
	}
}

// What a catch takes: its class, a test of the value for String, int, uint, Number and Boolean, or
// anything (NONE) for `*`, `Object` or no type.
uint32_t Lowerer::catch_type(const CatchClause &p_clause) {
	const Type *type = p_clause.type ? compilation_.type_of(p_clause.type) : nullptr;
	const Symbol *symbol = type && type->kind == Type::Instance ? type->symbol : nullptr;
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
	uint32_t index = NONE;
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
	TryBlock block;
	block.finally_block = p_try->finally_block;
	block.loops = loops_.size();
	tries_.push_back(std::move(block));
	const uint32_t body_start = here();
	statement(p_try->body);
	const uint32_t body_end = here();
	std::vector<uint32_t> to_end = { emit(Op::Jump, position, 0) };
	std::vector<Handler> catches;
	for (CatchClause &clause : p_try->catches) {
		const auto local = locals_.find(clause.symbol);
		if (local == locals_.end()) {
			refuse(clause.offset, "this catch");
			continue;
		}
		Handler handler;
		handler.target = here();
		handler.reg = local->second;
		handler.type = catch_type(clause);
		catches.push_back(handler);
		emit(Op::Stmt, clause.offset);
		statement(clause.body);
		to_end.push_back(emit(Op::Jump, position, 0));
	}
	const uint32_t catches_end = here();
	const std::vector<std::pair<uint32_t, uint32_t>> holes = std::move(tries_.back().holes);
	tries_.pop_back();
	// The innermost first: this try's catches, in the order written, then its finally.
	for (const Handler &handler : catches) {
		protect(body_start, body_end, holes, handler);
	}
	if (p_try->finally_block) {
		// An error in the body or a catch: the finally runs, and the error goes on.
		Handler anything;
		anything.target = here();
		anything.reg = reg(Kind::Any);
		protect(body_start, catches_end, holes, anything);
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
