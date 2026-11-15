#include "lowerer.h"

namespace kas3 {
namespace ir {

// A statement with labels: a loop or a switch takes them all; any other statement is a block that
// `break label` leaves. `a: b: for ...` gives the loop both.
void Lowerer::labeled(Labeled *p_labeled) {
	next_labels_.push_back(p_labeled->label);
	Stmt *body = p_labeled->body;
	const bool takes_labels = body && (body->kind == NodeKind::While || body->kind == NodeKind::DoWhile || body->kind == NodeKind::For ||
											  body->kind == NodeKind::ForIn || body->kind == NodeKind::Switch || body->kind == NodeKind::Labeled);
	if (takes_labels) {
		statement(body);
		next_labels_.clear();
		return;
	}
	open_loop();
	loops_.back().is_block = true;
	statement(body);
	finish_loop(here());
}

void Lowerer::route(size_t p_try, TryExit p_exit) {
	TryBlock &block = tries_[p_try];
	const uint32_t code = static_cast<uint32_t>(block.exits.size()) + 2;
	block.exits.push_back(p_exit);
	emit(Op::ConstI32, p_exit.position, block.exit_reg, code);
	block.to_finally.push_back(emit(Op::Jump, p_exit.position, 0));
}

void Lowerer::leave_to(size_t p_loop, bool p_break, uint32_t p_position) {
	// The innermost try with a finally that the loop is outside of: its finally runs first.
	for (size_t i = tries_.size(); i-- > loops_[p_loop].tries;) {
		if (tries_[i].finally_block) {
			route(i, TryExit{ p_break ? TryExit::Break : TryExit::Continue, p_loop, p_position });
			return;
		}
	}
	uint32_t jump = emit(Op::Jump, p_position, 0);
	(p_break ? loops_[p_loop].breaks : loops_[p_loop].continues).push_back(jump);
}

// The value is worked out first and kept by the innermost finally; then the finallies run, and cannot
// change it.
void Lowerer::leave_return(uint32_t p_position, uint32_t p_value) {
	for (size_t i = tries_.size(); i-- > 0;) {
		if (tries_[i].finally_block) {
			if (p_value != NONE) {
				if (tries_[i].return_reg == NONE) {
					tries_[i].return_reg = reg(fn().result);
				}
				emit(Op::Move, p_position, tries_[i].return_reg, p_value);
			}
			route(i, TryExit{ TryExit::Return, 0, p_position });
			return;
		}
	}
	if (p_value == NONE) {
		emit(Op::ReturnVoid, p_position);
	} else {
		emit(Op::Return, p_position, p_value);
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

// The finally, written once, then where to go after it: on past the try, the error on its way, or the
// way out that led there.
void Lowerer::finish_finally(TryBlock &p_try, uint32_t p_error, uint32_t p_position) {
	const uint32_t start = here();
	for (uint32_t jump : p_try.to_finally) {
		patch(jump, start);
	}
	statement(p_try.finally_block);
	const uint32_t exit = convert(p_try.exit_reg, Kind::F64, p_position);
	const auto is = [&](uint32_t p_code) {
		uint32_t code = reg(Kind::F64);
		emit(Op::ConstF64, p_position, code, number(p_code));
		uint32_t same = reg(Kind::Bool);
		emit(Op::EqF64, p_position, same, exit, code);
		return same;
	};
	const uint32_t to_past = emit(Op::JumpIf, p_position, is(0), 0);
	const uint32_t not_error = emit(Op::JumpIfNot, p_position, is(1), 0);
	emit(Op::Rethrow, p_position, p_error);
	patch(not_error, here());
	for (size_t k = 0; k < p_try.exits.size(); k++) {
		const TryExit way = p_try.exits[k];
		const uint32_t other = emit(Op::JumpIfNot, way.position, is(static_cast<uint32_t>(k) + 2), 0);
		if (way.kind == TryExit::Return) {
			leave_return(way.position, p_try.return_reg);
		} else {
			leave_to(way.loop, way.kind == TryExit::Break, way.position);
		}
		patch(other, here());
	}
	patch(to_past, here());
}

// try { body } catch (e:T) { ... } ... finally { ... }
//
// Laid out as: the body; each catch; for a finally, a catch of anything that keeps the error; then the
// finally, once, which every way out of the body and the catches goes through.
void Lowerer::try_statement(Try *p_try) {
	const uint32_t position = p_try->offset;
	TryBlock opened;
	opened.finally_block = p_try->finally_block;
	if (p_try->finally_block) {
		opened.exit_reg = reg(Kind::I32);
	}
	tries_.push_back(std::move(opened));
	std::vector<uint32_t> to_end;
	const auto on_past = [&]() {
		if (p_try->finally_block) {
			emit(Op::ConstI32, position, tries_.back().exit_reg, 0);
			tries_.back().to_finally.push_back(emit(Op::Jump, position, 0));
		} else {
			to_end.push_back(emit(Op::Jump, position, 0));
		}
	};
	const uint32_t body_start = here();
	statement(p_try->body);
	const uint32_t body_end = here();
	on_past();
	std::vector<Handler> catches;
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
		catches.push_back(handler);
		emit(Op::Stmt, clause.offset);
		statement(clause.body);
		on_past();
	}
	const uint32_t catches_end = here();
	TryBlock block = std::move(tries_.back());
	tries_.pop_back();
	// The innermost first: this try's catches, in the order written, then its finally.
	fn().handlers.insert(fn().handlers.end(), catches.begin(), catches.end());
	if (p_try->finally_block) {
		// An error in the body or a catch: kept, the finally runs, and it goes on.
		Handler anything;
		anything.start = body_start;
		anything.end = catches_end;
		anything.target = here();
		anything.reg = reg(Kind::Any);
		anything.rethrows = true;
		fn().handlers.push_back(anything);
		emit(Op::ConstI32, position, block.exit_reg, 1);
		block.to_finally.push_back(emit(Op::Jump, position, 0));
		finish_finally(block, anything.reg, position);
	}
	for (uint32_t jump : to_end) {
		patch(jump, here());
	}
}

} // namespace ir
} // namespace kas3
