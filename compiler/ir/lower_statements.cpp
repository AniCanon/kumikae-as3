#include "lowerer.h"

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
	walk(p_body, [&](Node *p_node) {
		if (p_node->kind != NodeKind::VarDecl) {
			return;
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
			} else if (kind == Kind::Val && !valued.count(declarator.symbol)) {
				// One of Godot's values with parts starts at its zero.
				zero_value(declarator.type, declarator.offset, local);
			}
		}
	});
}

void Lowerer::statement(Stmt *p_statement) {
	if (!p_statement) {
		return;
	}
	uint32_t position = p_statement->offset;
	bool loop = p_statement->kind == NodeKind::While || p_statement->kind == NodeKind::DoWhile || p_statement->kind == NodeKind::For;
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
		case NodeKind::If: {
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
			break;
		}
		case NodeKind::While: {
			While *loop = static_cast<While *>(p_statement);
			uint32_t top = here();
			emit(Op::Stmt, position);
			uint32_t test = expression(loop->condition, Kind::Bool);
			uint32_t out = emit(Op::JumpIfNot, position, test, 0);
			loops_.emplace_back();
			statement(loop->body);
			emit(Op::Jump, position, top);
			patch(out, here());
			finish_loop(top);
			break;
		}
		case NodeKind::DoWhile: {
			DoWhile *loop = static_cast<DoWhile *>(p_statement);
			uint32_t top = here();
			loops_.emplace_back();
			statement(loop->body);
			uint32_t test_at = here();
			emit(Op::Stmt, loop->condition ? loop->condition->offset : position);
			uint32_t test = expression(loop->condition, Kind::Bool);
			emit(Op::JumpIf, position, test, top);
			finish_loop(test_at);
			break;
		}
		case NodeKind::For: {
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
			loops_.emplace_back();
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
			break;
		}
		case NodeKind::Break:
		case NodeKind::Continue: {
			bool is_break = p_statement->kind == NodeKind::Break;
			std::string_view label = is_break ? static_cast<Break *>(p_statement)->label : static_cast<Continue *>(p_statement)->label;
			if (!label.empty()) {
				refuse(position, "a labelled 'break' or 'continue'");
			} else if (!loops_.empty()) {
				uint32_t jump = emit(Op::Jump, position, 0);
				(is_break ? loops_.back().breaks : loops_.back().continues).push_back(jump);
			}
			break;
		}
		case NodeKind::Return: {
			Return *result = static_cast<Return *>(p_statement);
			if (constructor_ || fn().result == Kind::Void) {
				if (result->value) {
					expression(result->value, Kind::Void);
				}
				emit(Op::ReturnVoid, position);
			} else {
				uint32_t value = result->value ? expression(result->value, fn().result) : default_value(fn().result, position);
				emit(Op::Return, position, value);
			}
			break;
		}
		case NodeKind::Throw: {
			uint32_t value = expression(static_cast<Throw *>(p_statement)->value, Kind::Void);
			if (kind_at(value) != Kind::Obj) {
				refuse(position, "throwing a value that is not an object");
			} else {
				emit(Op::Throw, position, value);
			}
			break;
		}
		case NodeKind::Switch:
			refuse(position, "'switch'");
			break;
		case NodeKind::Try:
			refuse(position, "'try'");
			break;
		case NodeKind::ForIn:
			refuse(position, static_cast<ForIn *>(p_statement)->each ? "'for each'" : "'for in'");
			break;
		case NodeKind::Labeled:
			refuse(position, "a label");
			break;
		case NodeKind::FunctionStmt:
			refuse(position, "a function inside a function");
			break;
		default:
			refuse(position, "this statement");
			break;
	}
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
		uint32_t value = expression(declarator.initializer, kind_at(local->second));
		if (value != local->second) {
			emit(Op::Move, declarator.offset, local->second, value);
		}
	}
}

} // namespace ir
} // namespace kas3
