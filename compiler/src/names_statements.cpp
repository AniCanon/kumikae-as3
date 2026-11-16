// The bodies pass: statements. Labels, `break` and `continue`, and the scope of each catch variable.
#include "names_resolver.h"

namespace kas3 {

namespace {

bool is_loop(const Stmt *p_statement) {
	// `a: b: for (;;)` labels the loop twice.
	while (p_statement->kind == NodeKind::Labeled && static_cast<const Labeled *>(p_statement)->body) {
		p_statement = static_cast<const Labeled *>(p_statement)->body;
	}
	switch (p_statement->kind) {
		case NodeKind::While:
		case NodeKind::DoWhile:
		case NodeKind::For:
		case NodeKind::ForIn:
			return true;
		default:
			return false;
	}
}

} // namespace

const Label *Resolver::find_label(std::string_view p_name) const {
	for (size_t i = labels_.size(); i-- > 0;) {
		if (labels_[i].name == p_name) {
			return &labels_[i];
		}
	}
	return nullptr;
}

void Resolver::loop_body(Stmt *p_body) {
	loops_++;
	statement(p_body);
	loops_--;
}

// Each `catch` has a scope of its own, holding the variable it catches into.
void Resolver::try_statement(Try *p_try) {
	statement(p_try->body);
	for (CatchClause &clause : p_try->catches) {
		type(clause.type);
		Scope transient;
		Scope &scope = retain_scope(transient, clause.body);
		scope.parent = scope_;
		if (!clause.name.empty()) {
			Symbol *symbol = compilation_.new_symbol(SymbolKind::CatchVariable, clause.name, &unit_, clause.name_offset);
			symbol->type = clause.type;
			clause.symbol = symbol;
			scope.names.emplace(clause.name, symbol);
		}
		scope_ = &scope;
		statement(clause.body);
		scope_ = scope.parent;
	}
	statement(p_try->finally_block);
}

void Resolver::loop_statement(Stmt *p_statement) {
	switch (p_statement->kind) {
		case NodeKind::While: {
			While *loop = static_cast<While *>(p_statement);
			expression(loop->condition);
			loop_body(loop->body);
		} break;
		case NodeKind::DoWhile: {
			DoWhile *loop = static_cast<DoWhile *>(p_statement);
			loop_body(loop->body);
			expression(loop->condition);
		} break;
		case NodeKind::For: {
			For *loop = static_cast<For *>(p_statement);
			statement(loop->init);
			expression(loop->condition);
			expression(loop->update);
			loop_body(loop->body);
		} break;
		case NodeKind::ForIn: {
			ForIn *loop = static_cast<ForIn *>(p_statement);
			if (loop->target) {
				assignable(loop->target);
				expression(loop->target);
			}
			expression(loop->collection);
			loop_body(loop->body);
		} break;
		default:
			break;
	}
}

void Resolver::switch_statement(Switch *p_switch) {
	expression(p_switch->subject);
	switches_++;
	for (SwitchCase &branch : p_switch->cases) {
		expression(branch.test);
		for (Stmt *inner : branch.body) {
			statement(inner);
		}
	}
	switches_--;
}

void Resolver::break_statement(Break *p_break) {
	if (p_break->label.empty()) {
		if (!loops_ && !switches_) {
			error(p_break->offset, "'break' is only allowed in a loop or switch");
		}
	} else if (!find_label(p_break->label)) {
		error(p_break->label_offset, "undefined label " + quoted(p_break->label));
	}
}

void Resolver::continue_statement(Continue *p_continue) {
	if (p_continue->label.empty()) {
		if (!loops_) {
			error(p_continue->offset, "'continue' is only allowed in a loop");
		}
	} else if (const Label *label = find_label(p_continue->label)) {
		if (!label->loop) {
			error(p_continue->label_offset, quoted(p_continue->label) + " does not label a loop");
		}
	} else {
		error(p_continue->label_offset, "undefined label " + quoted(p_continue->label));
	}
}

void Resolver::labeled_statement(Labeled *p_labeled) {
	labels_.push_back({ p_labeled->label, p_labeled->body && is_loop(p_labeled->body) });
	statement(p_labeled->body);
	labels_.pop_back();
}

void Resolver::statement(Stmt *p_statement) {
	if (!p_statement) {
		return;
	}
	switch (p_statement->kind) {
		case NodeKind::Block:
			for (Stmt *inner : static_cast<Block *>(p_statement)->statements) {
				statement(inner);
			}
			break;
		case NodeKind::ExprStmt:
			expression(static_cast<ExprStmt *>(p_statement)->expr);
			break;
		case NodeKind::VarStmt:
			// The names and types were handled when the function was entered.
			for (Declarator &declarator : static_cast<VarStmt *>(p_statement)->decl->declarators) {
				expression(declarator.initializer);
			}
			break;
		case NodeKind::If: {
			If *branch = static_cast<If *>(p_statement);
			expression(branch->condition);
			statement(branch->then_branch);
			statement(branch->else_branch);
		} break;
		case NodeKind::While:
		case NodeKind::DoWhile:
		case NodeKind::For:
		case NodeKind::ForIn:
			loop_statement(p_statement);
			break;
		case NodeKind::Switch:
			switch_statement(static_cast<Switch *>(p_statement));
			break;
		case NodeKind::Break:
			break_statement(static_cast<Break *>(p_statement));
			break;
		case NodeKind::Continue:
			continue_statement(static_cast<Continue *>(p_statement));
			break;
		case NodeKind::Return:
			expression(static_cast<Return *>(p_statement)->value);
			break;
		case NodeKind::Throw:
			expression(static_cast<Throw *>(p_statement)->value);
			break;
		case NodeKind::Try:
			try_statement(static_cast<Try *>(p_statement));
			break;
		case NodeKind::Labeled:
			labeled_statement(static_cast<Labeled *>(p_statement));
			break;
		case NodeKind::FunctionStmt:
			function(static_cast<FunctionStmt *>(p_statement)->function, false);
			break;
		default:
			break;
	}
}

} // namespace kas3
