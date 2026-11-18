#include "lower_dynamic.h"
#include "lower_engine.h"
#include "lower_functions.h"
#include "lower_operators.h"
#include "lower_statements.h"
#include "lower_vectors.h"

#include <algorithm>

namespace kas3 {
namespace ir {

namespace {

// A local that is given a value by its one declaration, written straight in the function's body,
// needs no starting value of its own: nothing below that line can see it without one.
std::unordered_set<const Symbol *> valued_locals(Node *p_body) {
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
	return valued;
}

} // namespace

void Statements::declare_locals(Node *p_body) {
	const std::unordered_set<const Symbol *> valued = valued_locals(p_body);
	// A function's own locals: not those of a function written inside it, which are that one's.
	walk(p_body, [&](Node *p_node) -> bool {
		if (p_node->kind == NodeKind::FunctionStmt) {
			Functions(l_).declare_function(static_cast<FunctionStmt *>(p_node)->function);
		}
		if (p_node->kind == NodeKind::FunctionExpr || p_node->kind == NodeKind::FunctionStmt) {
			return false;
		}
		if (p_node->kind == NodeKind::Try) {
			// A catch's variable: a register of the kind it is declared, untyped when it says none.
			for (CatchClause &clause : static_cast<Try *>(p_node)->catches) {
				if (!clause.symbol || l_.locals_.count(clause.symbol)) {
					continue;
				}
				const uint32_t local = l_.reg(clause.type ? l_.written_kind(clause.type, clause.offset, Kind::Any) : Kind::Any);
				l_.locals_[clause.symbol] = local;
				l_.fn().locals.push_back({ std::string(clause.name), local });
			}
			return true;
		}
		if (p_node->kind != NodeKind::VarDecl) {
			return true;
		}
		for (Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
			if (!declarator.symbol || l_.locals_.count(declarator.symbol)) {
				continue;
			}
			Kind kind = l_.written_kind(declarator.type, declarator.offset, Kind::F64);
			uint32_t local = l_.engine_reg(kind, declarator.type ? l_.compilation_.type_of(declarator.type) : nullptr);
			l_.locals_[declarator.symbol] = local;
			l_.fn().locals.push_back({ std::string(declarator.name), local });
			// A Number starts as NaN. The rest start as zero, which is what a fresh register holds.
			if (kind == Kind::F64) {
				l_.emit(Op::ConstF64, declarator.offset, local, l_.number(std::numeric_limits<double>::quiet_NaN()));
			} else if (l_.starts_null_type(kind, l_.compilation_.type_of(declarator.type))) {
				l_.emit(Op::AnyConst, declarator.offset, local, 1); // An Object starts null; a `*` undefined.
			} else if (kind == Kind::Val && !valued.count(declarator.symbol)) {
				// One of Godot's values with parts starts at its zero.
				Engine(l_).zero_value(declarator.type, declarator.offset, local);
			}
		}
		return true;
	});
}

void Statements::if_statement(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	If *branch = static_cast<If *>(p_statement);
	uint32_t test = l_.expression(branch->condition, Kind::Bool);
	uint32_t to_else = l_.emit(Op::JumpIfNot, position, test, 0);
	statement(branch->then_branch);
	if (branch->else_branch) {
		uint32_t to_end = l_.emit(Op::Jump, position, 0);
		l_.patch(to_else, l_.here());
		statement(branch->else_branch);
		l_.patch(to_end, l_.here());
	} else {
		l_.patch(to_else, l_.here());
	}
}

void Statements::while_loop(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	While *loop = static_cast<While *>(p_statement);
	uint32_t top = l_.here();
	l_.emit(Op::Stmt, position);
	uint32_t test = l_.expression(loop->condition, Kind::Bool);
	uint32_t out = l_.emit(Op::JumpIfNot, position, test, 0);
	open_loop();
	statement(loop->body);
	l_.emit(Op::Jump, position, top);
	l_.patch(out, l_.here());
	finish_loop(top);
}

void Statements::do_while_loop(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	DoWhile *loop = static_cast<DoWhile *>(p_statement);
	uint32_t top = l_.here();
	open_loop();
	statement(loop->body);
	uint32_t test_at = l_.here();
	l_.emit(Op::Stmt, loop->condition ? loop->condition->offset : position);
	uint32_t test = l_.expression(loop->condition, Kind::Bool);
	l_.emit(Op::JumpIf, position, test, top);
	finish_loop(test_at);
}

void Statements::for_loop(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	For *loop = static_cast<For *>(p_statement);
	statement(loop->init);
	uint32_t top = l_.here();
	uint32_t out = NONE;
	if (!loop->condition) {
		l_.emit(Op::Stmt, position);
	}
	if (loop->condition) {
		l_.emit(Op::Stmt, loop->condition->offset);
		uint32_t test = l_.expression(loop->condition, Kind::Bool);
		out = l_.emit(Op::JumpIfNot, position, test, 0);
	}
	open_loop();
	statement(loop->body);
	uint32_t update_at = l_.here();
	if (loop->update) {
		l_.effect(loop->update);
	}
	l_.emit(Op::Jump, position, top);
	if (out != NONE) {
		l_.patch(out, l_.here());
	}
	finish_loop(update_at);
}

void Statements::break_or_continue(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	bool is_break = p_statement->kind == NodeKind::Break;
	std::string_view label = is_break ? static_cast<Break *>(p_statement)->label : static_cast<Continue *>(p_statement)->label;
	// `break` leaves the nearest loop or switch, `continue` goes round the nearest loop; with a label, the
	// statement of that label (a block only for `break`).
	for (size_t i = l_.loops_.size(); i-- > 0;) {
		const Loop &target = l_.loops_[i];
		const bool named = std::find(target.labels.begin(), target.labels.end(), label) != target.labels.end();
		const bool fits = label.empty() ? !target.is_block && (is_break || !target.is_switch)
										: named && (is_break || (!target.is_switch && !target.is_block));
		if (fits) {
			leave_to(i, is_break, position);
			return;
		}
	}
	if (!label.empty()) {
		l_.refuse(position, "'" + std::string(is_break ? "break " : "continue ") + std::string(label) + "' here");
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

void Statements::switch_statement(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	Switch *choice = static_cast<Switch *>(p_statement);
	if (!choice->subject || Lowerer::is_null(choice->subject)) {
		l_.refuse(position, "a 'switch' on null");
		return;
	}
	// The subject is worked out once, and kept where nothing a case does can change it.
	uint32_t value = l_.expression(choice->subject, Kind::Void);
	uint32_t subject = l_.reg(l_.kind_at(value));
	l_.emit(Op::Move, position, subject, value);
	l_.evaluated_[choice->subject] = subject;
	// The tests, in the order written, each jumping to its body. Then the bodies, one after another, so
	// that one without a `break` runs on into the next.
	std::vector<uint32_t> to_body(choice->cases.count, NONE);
	size_t at = 0;
	for (SwitchCase &each : choice->cases) {
		if (each.test) {
			Kind kind = Lowerer::is_null(each.test) ? Kind::Void : l_.natural(each.test);
			if (Lowerer::can_be_equal(l_.kind_at(subject), kind)) {
				Binary same;
				same.kind = NodeKind::Binary;
				same.op = TokenKind::StrictEqual;
				same.offset = each.offset;
				same.left = choice->subject;
				same.right = each.test;
				to_body[at] = l_.emit(Op::JumpIf, each.offset, Operators(l_).equality(&same), 0);
			} else {
				l_.expression(each.test, Kind::Void); // Never equal, but what it does is still done.
			}
		}
		at++;
	}
	l_.evaluated_.erase(choice->subject);
	uint32_t to_default = l_.emit(Op::Jump, position, 0);
	bool has_default = false;
	open_loop();
	l_.loops_.back().is_switch = true;
	at = 0;
	for (SwitchCase &each : choice->cases) {
		if (to_body[at] != NONE) {
			l_.patch(to_body[at], l_.here());
		}
		if (!each.test) {
			l_.patch(to_default, l_.here());
			has_default = true;
		}
		for (Stmt *inner : each.body) {
			statement(inner);
		}
		at++;
	}
	if (!has_default) {
		l_.patch(to_default, l_.here());
	}
	finish_loop(l_.here());
}

void Statements::return_statement(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	Return *result = static_cast<Return *>(p_statement);
	if (l_.constructor_ || l_.fn().result == Kind::Void) {
		if (result->value) {
			l_.effect(result->value);
		}
		leave_return(position, NONE);
	} else {
		leave_return(position, result->value ? l_.typed(result->value, l_.result_type_, l_.fn().result) : l_.default_value(l_.fn().result, position, l_.result_type_));
	}
}

void Statements::throw_statement(Stmt *p_statement) {
	uint32_t position = p_statement->offset;
	l_.emit(Op::Throw, position, l_.any_value(static_cast<Throw *>(p_statement)->value)); // Any value at all.
}

void Statements::statement(Stmt *p_statement) {
	if (!p_statement) {
		return;
	}
	// The statement being lowered, for refuse(): put back when this one is done.
	struct Outer {
		const Stmt *&current;
		const Stmt *kept;
		~Outer() { current = kept; }
	} outer{ l_.statement_, l_.statement_ };
	if (p_statement->kind != NodeKind::Block) {
		l_.statement_ = p_statement;
	}
	uint32_t position = p_statement->offset;
	bool loop = p_statement->kind == NodeKind::While || p_statement->kind == NodeKind::DoWhile || p_statement->kind == NodeKind::For ||
			p_statement->kind == NodeKind::ForIn;
	if (p_statement->kind != NodeKind::Block && p_statement->kind != NodeKind::Empty && !loop) {
		l_.emit(Op::Stmt, position); // A loop marks its condition instead, once for each pass.
	}
	by_kind(p_statement);
}

// The statement's own lowering, by its kind.
void Statements::by_kind(Stmt *p_statement) {
	const uint32_t position = p_statement->offset;
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
			l_.effect(static_cast<ExprStmt *>(p_statement)->expr);
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
			for_in_loop(static_cast<ForIn *>(p_statement));
			break;
		case NodeKind::Labeled:
			labeled(static_cast<Labeled *>(p_statement));
			break;
		case NodeKind::FunctionStmt:
			break; // Made as the function starts (Functions::nested_functions).
		default:
			l_.refuse(position, "this statement");
			break;
	}
}

// `for in` and `for each`: over a vector, one of Godot's collections, or anything else by its properties.
void Statements::for_in_loop(ForIn *p_loop) {
	if (l_.is_vector(p_loop->collection->type) && !(l_.array_ && p_loop->collection->type->symbol == l_.array_)) {
		Vectors(l_).vector_loop(p_loop);
	} else if (p_loop->element || Engine(l_).is_dictionary(p_loop->collection->type) || Engine(l_).is_indexed(p_loop->collection->type)) {
		Vectors(l_).engine_loop(p_loop);
	} else {
		Dynamic(l_).loop(p_loop); // An Array's properties too.
	}
}

void Statements::open_loop() {
	Loop loop;
	loop.labels = std::move(l_.next_labels_);
	loop.tries = l_.tries_.size();
	l_.next_labels_.clear();
	l_.loops_.push_back(std::move(loop));
}

void Statements::finish_loop(uint32_t p_continue_at) {
	for (uint32_t jump : l_.loops_.back().breaks) {
		l_.patch(jump, l_.here());
	}
	for (uint32_t jump : l_.loops_.back().continues) {
		l_.patch(jump, p_continue_at);
	}
	l_.loops_.pop_back();
}

void Statements::variables(VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		auto local = l_.locals_.find(declarator.symbol);
		if (local == l_.locals_.end() || !declarator.initializer) {
			continue; // Without an initializer a `var` keeps what the variable holds: it is declared once.
		}
		uint32_t value = l_.typed(declarator.initializer, l_.compilation_.type_of(declarator.type), l_.kind_at(local->second));
		if (std::optional<Place> shared = Functions(l_).captured_place(declarator.symbol, declarator.offset)) {
			l_.store(*shared, value, declarator.offset); // One a function written inside shares.
		} else if (value != local->second) {
			l_.emit(Op::Move, declarator.offset, local->second, value);
		}
	}
}

} // namespace ir
} // namespace kas3
