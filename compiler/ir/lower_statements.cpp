#include "lower_dynamic.h"

#include <algorithm>

namespace kas3 {
namespace ir {

void Lowerer::declare_locals(Node *p_body) {
	// A local that is given a value by its one declaration, written straight in the function's body,
	// needs no starting value of its own: nothing below that line can see it without one.
	std::unordered_map<const Symbol *, int> declared;
	std::unordered_set<const Symbol *> valued;
	walk(p_body, [&](Node *p_node) {
		if (p_node->kind == NodeKind::VarDecl) {
			for (Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
				declared[declarator.symbol]++;
			}
		}
	});
	if (p_body && p_body->kind == NodeKind::Block) {
		for (Stmt *statement : static_cast<Block *>(p_body)->statements) {
			if (statement && statement->kind == NodeKind::VarStmt && static_cast<VarStmt *>(statement)->decl) {
				for (Declarator &declarator : static_cast<VarStmt *>(statement)->decl->declarators) {
					if (declarator.initializer && declared[declarator.symbol] == 1) {
						valued.insert(declarator.symbol);
					}
				}
			}
		}
	}
	// One that is read above its line (it exists from the top of the function) needs its zero after all.
	if (!valued.empty()) {
		std::unordered_map<const Symbol *, uint32_t> line_of;
		walk(p_body, [&](Node *p_node) {
			if (p_node->kind == NodeKind::VarDecl) {
				for (Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
					line_of[declarator.symbol] = declarator.offset;
				}
			}
		});
		walk(p_body, [&](Node *p_node) {
			if (p_node->kind == NodeKind::Name) {
				const Symbol *symbol = static_cast<Name *>(p_node)->symbol;
				const auto found = line_of.find(symbol);
				if (found != line_of.end() && p_node->offset < found->second) {
					valued.erase(symbol);
				}
			}
		});
	}
	// A function's own locals: not those of a function written inside it, which are that one's.
	walk(p_body, [&](Node *p_node) -> bool {
		if (p_node->kind == NodeKind::FunctionExpr || p_node->kind == NodeKind::FunctionStmt) {
			return false;
		}
		if (p_node->kind == NodeKind::Try) {
			// A catch's variable: a register of the kind it is declared, untyped when it says none.
			for (CatchClause &clause : static_cast<Try *>(p_node)->catches) {
				if (!clause.symbol || locals_.count(clause.symbol)) {
					continue;
				}
				const uint32_t local = reg(clause.type ? written_kind(clause.type, clause.offset, Kind::Any) : Kind::Any);
				locals_[clause.symbol] = local;
				fn().locals.push_back({ std::string(clause.name), local });
			}
			return true;
		}
		if (p_node->kind != NodeKind::VarDecl) {
			return true;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
			if (!declarator.symbol || locals_.count(declarator.symbol)) {
				continue;
			}
			Kind kind = written_kind(declarator.type, declarator.offset, Kind::F64);
			uint32_t local = reg(kind);
			locals_[declarator.symbol] = local;
			fn().locals.push_back({ std::string(declarator.name), local });
			// A Number starts as NaN. The rest start as zero, which is what a fresh register holds.
			if (kind == Kind::F64) {
				emit(Op::ConstF64, declarator.offset, local, number(std::numeric_limits<double>::quiet_NaN()));
			} else if (starts_null_type(kind, compilation_.type_of(declarator.type))) {
				emit(Op::AnyConst, declarator.offset, local, 1); // An Object starts null; a `*` undefined.
			} else if (kind == Kind::Val && !valued.count(declarator.symbol)) {
				// One of Godot's values with parts starts at its zero.
				zero_value(declarator.type, declarator.offset, local);
			}
		}
		return true;
	});
}

void Lowerer::if_statement(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	If *branch = static_cast<If *>(p_statement);
	uint32_t test = expression(branch->condition, Kind::Bool);
	uint32_t to_else = emit(Op::JumpIfNot, position, test, 0);
	statement(branch->then_branch);
	if (branch->else_branch) {
		uint32_t to_end = emit(Op::Jump, position, 0);
		patch(to_else, here());
		statement(branch->else_branch);
		patch(to_end, here());
	} else {
		patch(to_else, here());
	}
}

void Lowerer::while_loop(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	While *loop = static_cast<While *>(p_statement);
	uint32_t top = here();
	emit(Op::Stmt, position);
	uint32_t test = expression(loop->condition, Kind::Bool);
	uint32_t out = emit(Op::JumpIfNot, position, test, 0);
	open_loop();
	statement(loop->body);
	emit(Op::Jump, position, top);
	patch(out, here());
	finish_loop(top);
}

void Lowerer::do_while_loop(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	DoWhile *loop = static_cast<DoWhile *>(p_statement);
	uint32_t top = here();
	open_loop();
	statement(loop->body);
	uint32_t test_at = here();
	emit(Op::Stmt, loop->condition ? loop->condition->offset : position);
	uint32_t test = expression(loop->condition, Kind::Bool);
	emit(Op::JumpIf, position, test, top);
	finish_loop(test_at);
}

void Lowerer::for_loop(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	For *loop = static_cast<For *>(p_statement);
	statement(loop->init);
	uint32_t top = here();
	uint32_t out = NONE;
	if (!loop->condition) {
		emit(Op::Stmt, position);
	}
	if (loop->condition) {
		emit(Op::Stmt, loop->condition->offset);
		uint32_t test = expression(loop->condition, Kind::Bool);
		out = emit(Op::JumpIfNot, position, test, 0);
	}
	open_loop();
	statement(loop->body);
	uint32_t update_at = here();
	if (loop->update) {
		expression(loop->update, Kind::Void);
	}
	emit(Op::Jump, position, top);
	if (out != NONE) {
		patch(out, here());
	}
	finish_loop(update_at);
}

void Lowerer::break_or_continue(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	bool is_break = p_statement->kind == NodeKind::Break;
	std::string_view label = is_break ? static_cast<Break *>(p_statement)->label : static_cast<Continue *>(p_statement)->label;
	// `break` leaves the nearest loop or switch, `continue` goes round the nearest loop; with a label, the
	// statement of that label (a block only for `break`).
	for (size_t i = loops_.size(); i-- > 0;) {
		const Loop &target = loops_[i];
		const bool named = std::find(target.labels.begin(), target.labels.end(), label) != target.labels.end();
		const bool fits = label.empty() ? !target.is_block && (is_break || !target.is_switch)
										: named && (is_break || (!target.is_switch && !target.is_block));
		if (fits) {
			leave_to(i, is_break, position);
			return;
		}
	}
	if (!label.empty()) {
		refuse(position, "'" + std::string(is_break ? "break " : "continue ") + std::string(label) + "' here");
	}
}

bool Lowerer::can_be_equal(Kind p_a, Kind p_b) {
	const auto family = [](Kind p_kind) {
		switch (p_kind) {
			case Kind::I32:
			case Kind::U32:
			case Kind::F64:
			case Kind::I64:
				return 0;
			case Kind::Val:
			case Kind::Ref:
				return 1;
			default:
				return 2 + static_cast<int>(p_kind);
		}
	};
	const auto nullable = [](Kind p_kind) { return is_reference(p_kind) || is_engine(p_kind) || p_kind == Kind::Any; };
	if (p_a == Kind::Any || p_b == Kind::Any) {
		return true; // What an untyped value holds is known only when it runs.
	}
	if (p_a == Kind::Void || p_b == Kind::Void) {
		return p_a == p_b || nullable(p_a == Kind::Void ? p_b : p_a);
	}
	return family(p_a) == family(p_b);
}

void Lowerer::switch_statement(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	Switch *choice = static_cast<Switch *>(p_statement);
	if (!choice->subject || is_null(choice->subject)) {
		refuse(position, "a 'switch' on null");
		return;
	}
	// The subject is worked out once, and kept where nothing a case does can change it.
	uint32_t value = expression(choice->subject, Kind::Void);
	uint32_t subject = reg(kind_at(value));
	emit(Op::Move, position, subject, value);
	evaluated_[choice->subject] = subject;
	// The tests, in the order written, each jumping to its body. Then the bodies, one after another, so
	// that one without a `break` runs on into the next.
	std::vector<uint32_t> to_body(choice->cases.count, NONE);
	size_t at = 0;
	for (SwitchCase &each : choice->cases) {
		if (each.test) {
			Kind kind = is_null(each.test) ? Kind::Void : natural(each.test);
			if (can_be_equal(kind_at(subject), kind)) {
				Binary same;
				same.kind = NodeKind::Binary;
				same.op = TokenKind::StrictEqual;
				same.offset = each.offset;
				same.left = choice->subject;
				same.right = each.test;
				to_body[at] = emit(Op::JumpIf, each.offset, equality(&same), 0);
			} else {
				expression(each.test, Kind::Void); // Never equal, but what it does is still done.
			}
		}
		at++;
	}
	evaluated_.erase(choice->subject);
	uint32_t to_default = emit(Op::Jump, position, 0);
	bool has_default = false;
	open_loop();
	loops_.back().is_switch = true;
	at = 0;
	for (SwitchCase &each : choice->cases) {
		if (to_body[at] != NONE) {
			patch(to_body[at], here());
		}
		if (!each.test) {
			patch(to_default, here());
			has_default = true;
		}
		for (Stmt *inner : each.body) {
			statement(inner);
		}
		at++;
	}
	if (!has_default) {
		patch(to_default, here());
	}
	finish_loop(here());
}

void Lowerer::return_statement(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	Return *result = static_cast<Return *>(p_statement);
	if (constructor_ || fn().result == Kind::Void) {
		if (result->value) {
			expression(result->value, Kind::Void);
		}
		leave_return(position, NONE);
	} else {
		leave_return(position, result->value ? typed(result->value, result_type_, fn().result) : default_value(fn().result, position));
	}
}

void Lowerer::throw_statement(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	emit(Op::Throw, position, any_value(static_cast<Throw *>(p_statement)->value)); // Any value at all.
}

void Lowerer::statement(Stmt *p_statement) {
	if (!p_statement) {
		return;
	}
	// The statement being lowered, for refuse(): put back when this one is done.
	struct Outer {
		const Stmt *&current;
		const Stmt *kept;
		~Outer() { current = kept; }
	} outer{ statement_, statement_ };
	if (p_statement->kind != NodeKind::Block) {
		statement_ = p_statement;
	}
	uint32_t position = p_statement->offset;
	bool loop = p_statement->kind == NodeKind::While || p_statement->kind == NodeKind::DoWhile || p_statement->kind == NodeKind::For ||
			p_statement->kind == NodeKind::ForIn;
	if (p_statement->kind != NodeKind::Block && p_statement->kind != NodeKind::Empty && !loop) {
		emit(Op::Stmt, position); // A loop marks its condition instead, once for each pass.
	}
	switch (p_statement->kind) {
		case NodeKind::Block:
			for (Stmt *inner : static_cast<Block *>(p_statement)->statements) {
				statement(inner);
			}
			break;
		case NodeKind::Empty:
		case NodeKind::ImportStmt:
			break;
		case NodeKind::ExprStmt:
			expression(static_cast<ExprStmt *>(p_statement)->expr, Kind::Void);
			break;
		case NodeKind::VarStmt:
			variables(static_cast<VarStmt *>(p_statement)->decl);
			break;
		case NodeKind::If:
			if_statement(p_statement);
			break;
		case NodeKind::While:
			while_loop(p_statement);
			break;
		case NodeKind::DoWhile:
			do_while_loop(p_statement);
			break;
		case NodeKind::For:
			for_loop(p_statement);
			break;
		case NodeKind::Break:
		case NodeKind::Continue:
			break_or_continue(p_statement);
			break;
		case NodeKind::Return:
			return_statement(p_statement);
			break;
		case NodeKind::Throw:
			throw_statement(p_statement);
			break;
		case NodeKind::Switch:
			switch_statement(p_statement);
			break;
		case NodeKind::Try:
			try_statement(static_cast<Try *>(p_statement));
			break;
		case NodeKind::ForIn:
			if (is_vector(static_cast<ForIn *>(p_statement)->collection->type) && !(array_ && static_cast<ForIn *>(p_statement)->collection->type->symbol == array_)) {
				vector_loop(static_cast<ForIn *>(p_statement));
			} else if (static_cast<ForIn *>(p_statement)->element) {
				engine_loop(static_cast<ForIn *>(p_statement));
			} else {
				Dynamic(*this).loop(static_cast<ForIn *>(p_statement)); // An Array's properties too.
			}
			break;
		case NodeKind::Labeled:
			labeled(static_cast<Labeled *>(p_statement));
			break;
		case NodeKind::FunctionStmt:
			refuse(position, "a function inside a function");
			break;
		default:
			refuse(position, "this statement");
			break;
	}
}

void Lowerer::open_loop() {
	Loop loop;
	loop.labels = std::move(next_labels_);
	loop.tries = tries_.size();
	next_labels_.clear();
	loops_.push_back(std::move(loop));
}

void Lowerer::finish_loop(uint32_t p_continue_at) {
	for (uint32_t jump : loops_.back().breaks) {
		patch(jump, here());
	}
	for (uint32_t jump : loops_.back().continues) {
		patch(jump, p_continue_at);
	}
	loops_.pop_back();
}

void Lowerer::variables(VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		auto local = locals_.find(declarator.symbol);
		if (local == locals_.end() || !declarator.initializer) {
			continue; // Without an initializer a `var` keeps what the variable holds: it is declared once.
		}
		uint32_t value = typed(declarator.initializer, compilation_.type_of(declarator.type), kind_at(local->second));
		if (std::optional<Place> shared = captured_place(declarator.symbol, declarator.offset)) {
			store(*shared, value, declarator.offset); // One a function written inside shares.
		} else if (value != local->second) {
			emit(Op::Move, declarator.offset, local->second, value);
		}
	}
}

} // namespace ir
} // namespace kas3
