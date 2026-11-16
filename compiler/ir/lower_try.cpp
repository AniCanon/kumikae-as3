#include "lower_statements.h"

namespace kas3 {
namespace ir {

// A statement with labels: a loop or a switch takes them all; any other statement is a block that
// `break label` leaves. `a: b: for ...` gives the loop both.
void Statements::labeled(Labeled *p_labeled) {
	l_.next_labels_.push_back(p_labeled->label);
	Stmt *body = p_labeled->body;
	const bool takes_labels = body && (body->kind == NodeKind::While || body->kind == NodeKind::DoWhile || body->kind == NodeKind::For ||
											  body->kind == NodeKind::ForIn || body->kind == NodeKind::Switch || body->kind == NodeKind::Labeled);
	if (takes_labels) {
		statement(body);
		l_.next_labels_.clear();
		return;
	}
	open_loop();
	l_.loops_.back().is_block = true;
	statement(body);
	finish_loop(l_.here());
}

void Statements::route(size_t p_try, TryExit p_exit) {
	TryBlock &block = l_.tries_[p_try];
	const uint32_t code = static_cast<uint32_t>(block.exits.size()) + 2;
	block.exits.push_back(p_exit);
	l_.emit(Op::ConstI32, p_exit.position, block.exit_reg, code);
	block.to_finally.push_back(l_.emit(Op::Jump, p_exit.position, 0));
}

void Statements::leave_to(size_t p_loop, bool p_break, uint32_t p_position) {
	// The innermost try with a finally that the loop is outside of: its finally runs first.
	for (size_t i = l_.tries_.size(); i-- > l_.loops_[p_loop].tries;) {
		if (l_.tries_[i].finally_block) {
			route(i, TryExit{ p_break ? TryExit::Break : TryExit::Continue, p_loop, p_position });
			return;
		}
	}
	uint32_t jump = l_.emit(Op::Jump, p_position, 0);
	(p_break ? l_.loops_[p_loop].breaks : l_.loops_[p_loop].continues).push_back(jump);
}

// The value is worked out first and kept by the innermost finally; then the finallies run, and cannot
// change it.
void Statements::leave_return(uint32_t p_position, uint32_t p_value) {
	for (size_t i = l_.tries_.size(); i-- > 0;) {
		if (l_.tries_[i].finally_block) {
			if (p_value != NONE) {
				if (l_.tries_[i].return_reg == NONE) {
					l_.tries_[i].return_reg = l_.reg(l_.fn().result);
				}
				l_.emit(Op::Move, p_position, l_.tries_[i].return_reg, p_value);
			}
			route(i, TryExit{ TryExit::Return, 0, p_position });
			return;
		}
	}
	if (p_value == NONE) {
		l_.emit(Op::ReturnVoid, p_position);
	} else {
		l_.emit(Op::Return, p_position, p_value);
	}
}

// What a catch takes: its class, a test of the value for String, int, uint, Number and Boolean, or
// anything (NONE) for `*`, `Object` or no type.
uint32_t Statements::catch_type(const CatchClause &p_clause) {
	const Type *type = p_clause.type ? l_.compilation_.type_of(p_clause.type) : nullptr;
	const Symbol *symbol = type && type->kind == Type::Instance ? type->symbol : nullptr;
	if (!symbol || symbol == l_.object_) {
		return NONE;
	}
	if (symbol == l_.string_) {
		return ANY_IS_STRING;
	}
	if (symbol == l_.int_) {
		return ANY_IS_INT;
	}
	if (symbol == l_.uint_) {
		return ANY_IS_UINT;
	}
	if (symbol == l_.number_) {
		return ANY_IS_NUMBER;
	}
	if (symbol == l_.boolean_) {
		return ANY_IS_BOOLEAN;
	}
	uint32_t index = NONE;
	if (!l_.class_index(symbol, &index)) {
		l_.refuse(p_clause.offset, "catching a '" + l_.compilation_.type_name(type) + "'");
		return NONE;
	}
	return index;
}

// The finally, written once, then where to go after it: on past the try, the error on its way, or the
// way out that led there.
void Statements::finish_finally(TryBlock &p_try, uint32_t p_error, uint32_t p_position) {
	const uint32_t start = l_.here();
	for (uint32_t jump : p_try.to_finally) {
		l_.patch(jump, start);
	}
	statement(p_try.finally_block);
	const uint32_t exit = l_.convert(p_try.exit_reg, Kind::F64, p_position);
	const auto is = [&](uint32_t p_code) {
		uint32_t code = l_.reg(Kind::F64);
		l_.emit(Op::ConstF64, p_position, code, l_.number(p_code));
		uint32_t same = l_.reg(Kind::Bool);
		l_.emit(Op::EqF64, p_position, same, exit, code);
		return same;
	};
	const uint32_t to_past = l_.emit(Op::JumpIf, p_position, is(0), 0);
	const uint32_t not_error = l_.emit(Op::JumpIfNot, p_position, is(1), 0);
	l_.emit(Op::Rethrow, p_position, p_error);
	l_.patch(not_error, l_.here());
	for (size_t k = 0; k < p_try.exits.size(); k++) {
		const TryExit way = p_try.exits[k];
		const uint32_t other = l_.emit(Op::JumpIfNot, way.position, is(static_cast<uint32_t>(k) + 2), 0);
		if (way.kind == TryExit::Return) {
			leave_return(way.position, p_try.return_reg);
		} else {
			leave_to(way.loop, way.kind == TryExit::Break, way.position);
		}
		l_.patch(other, l_.here());
	}
	l_.patch(to_past, l_.here());
}

// try { body } catch (e:T) { ... } ... finally { ... }
//
// Laid out as: the body; each catch; for a finally, a catch of anything that keeps the error; then the
// finally, once, which every way out of the body and the catches goes through.
void Statements::try_statement(Try *p_try) {
	const uint32_t position = p_try->offset;
	TryBlock opened;
	opened.finally_block = p_try->finally_block;
	if (p_try->finally_block) {
		opened.exit_reg = l_.reg(Kind::I32);
	}
	l_.tries_.push_back(std::move(opened));
	std::vector<uint32_t> to_end;
	const auto on_past = [&]() {
		if (p_try->finally_block) {
			l_.emit(Op::ConstI32, position, l_.tries_.back().exit_reg, 0);
			l_.tries_.back().to_finally.push_back(l_.emit(Op::Jump, position, 0));
		} else {
			to_end.push_back(l_.emit(Op::Jump, position, 0));
		}
	};
	const uint32_t body_start = l_.here();
	statement(p_try->body);
	const uint32_t body_end = l_.here();
	on_past();
	std::vector<Handler> catches;
	for (CatchClause &clause : p_try->catches) {
		const auto local = l_.locals_.find(clause.symbol);
		if (local == l_.locals_.end()) {
			l_.refuse(clause.offset, "this catch");
			continue;
		}
		Handler handler;
		handler.start = body_start;
		handler.end = body_end;
		handler.target = l_.here();
		handler.reg = local->second;
		handler.type = catch_type(clause);
		catches.push_back(handler);
		l_.emit(Op::Stmt, clause.offset);
		statement(clause.body);
		on_past();
	}
	const uint32_t catches_end = l_.here();
	TryBlock block = std::move(l_.tries_.back());
	l_.tries_.pop_back();
	// The innermost first: this try's catches, in the order written, then its finally.
	l_.fn().handlers.insert(l_.fn().handlers.end(), catches.begin(), catches.end());
	if (p_try->finally_block) {
		// An error in the body or a catch: kept, the finally runs, and it goes on.
		Handler anything;
		anything.start = body_start;
		anything.end = catches_end;
		anything.target = l_.here();
		anything.reg = l_.reg(Kind::Any);
		anything.rethrows = true;
		l_.fn().handlers.push_back(anything);
		l_.emit(Op::ConstI32, position, block.exit_reg, 1);
		block.to_finally.push_back(l_.emit(Op::Jump, position, 0));
		finish_finally(block, anything.reg, position);
	}
	for (uint32_t jump : to_end) {
		l_.patch(jump, l_.here());
	}
}

} // namespace ir
} // namespace kas3
