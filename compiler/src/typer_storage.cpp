// Local storage is a fact about the checked body. It never changes the type used for diagnostics.
#include "ast_walk.h"
#include "typer.h"

namespace kas3 {

namespace {

Symbol *local_name(Expr *p_expr) {
	return p_expr && p_expr->kind == NodeKind::Name ? static_cast<Name *>(p_expr)->symbol : nullptr;
}

const Type *stored_type(Expr *p_expr) {
	if (!p_expr || !p_expr->type || p_expr->type->kind != Type::Instance) {
		return nullptr;
	}
	switch (p_expr->kind) {
		case NodeKind::NumberLit:
		case NodeKind::New:
		case NodeKind::ArrayLit:
		case NodeKind::RegexLit:
		case NodeKind::ThisExpr:
		case NodeKind::StringLit:
		case NodeKind::BoolLit:
			return p_expr->type;
		case NodeKind::Name: {
			const Symbol *source = local_name(p_expr);
			return source && (source->kind == SymbolKind::Local || source->kind == SymbolKind::Parameter || source->kind == SymbolKind::Field || source->kind == SymbolKind::Variable) ? p_expr->type : nullptr;
		}
		case NodeKind::Call: {
			const Expr *callee = static_cast<Call *>(p_expr)->callee;
			const Symbol *target = callee->kind == NodeKind::Name ? static_cast<const Name *>(callee)->symbol
					: callee->kind == NodeKind::Member ? static_cast<const Member *>(callee)->resolved() : nullptr;
			return target && (target->kind == SymbolKind::Function || target->kind == SymbolKind::Method) && target->unit && !target->unit->is_declaration
					? p_expr->type : nullptr;
		}
		default:
			return nullptr; // A numeric 'as', for example, may also produce null.
	}
}

} // namespace

void Typer::infer_storage(Block *p_body) {
	if (!p_body) {
		return;
	}
	struct Candidate {
		const Declarator *declaration;
		Expr *value;
		Expr *target;
	};
	std::unordered_map<Symbol *, Candidate> candidates;
	for (Stmt *statement : p_body->statements) {
		if (statement->kind == NodeKind::VarStmt) {
			for (auto &decl : static_cast<VarStmt *>(statement)->decl->declarators) {
				if (decl.symbol && decl.symbol->kind == SymbolKind::Local && (!decl.type || decl.type->form == TypeRef::Any) && stored_type(decl.initializer)) {
					candidates.emplace(decl.symbol, Candidate{ &decl, decl.initializer, nullptr });
				}
			}
		} else if (statement->kind == NodeKind::ExprStmt) {
			Expr *expr = static_cast<ExprStmt *>(statement)->expr;
			if (expr && expr->kind == NodeKind::Assign) {
				auto *assign = static_cast<Assign *>(expr);
				Symbol *local = local_name(assign->target);
				if (assign->op == TokenKind::Assign && local && local->kind == SymbolKind::Local &&
						(!local->type || local->type->form == TypeRef::Any) && stored_type(assign->value)) {
					candidates.emplace(local, Candidate{ nullptr, assign->value, assign->target });
				}
			}
		}
	}
	if (candidates.empty()) {
		return;
	}
	walk(p_body, [&](Node *p_node) {
		switch (p_node->kind) {
			case NodeKind::FunctionDecl:
				walk(p_node, [&](Node *p_part) {
					if (p_part->kind == NodeKind::Name) {
						candidates.erase(static_cast<Name *>(p_part)->symbol);
					}
				});
				return false;
			case NodeKind::VarDecl:
				for (auto &decl : static_cast<VarDecl *>(p_node)->declarators) {
					auto it = candidates.find(decl.symbol);
					if (it != candidates.end() && decl.initializer && it->second.declaration != &decl) {
						candidates.erase(it);
					}
				}
				break;
			case NodeKind::Name: {
				auto it = candidates.find(static_cast<Name *>(p_node)->symbol);
				if (it != candidates.end() && p_node != it->second.target && p_node->offset < it->second.value->end) {
					candidates.erase(it);
				}
			} break;
			case NodeKind::Assign: {
				const auto *assign = static_cast<Assign *>(p_node);
				auto it = candidates.find(local_name(assign->target));
				if (it != candidates.end() && (assign->op != TokenKind::Assign || stored_type(assign->value) != it->second.value->type)) {
					candidates.erase(it);
				}
			} break;
			case NodeKind::ForIn: {
				const auto *loop = static_cast<ForIn *>(p_node);
				candidates.erase(local_name(loop->target));
				if (loop->declaration) {
					for (auto &decl : loop->declaration->declarators) {
						candidates.erase(decl.symbol);
					}
				}
			} break;
			case NodeKind::Postfix:
				candidates.erase(local_name(static_cast<Postfix *>(p_node)->operand));
				break;
			case NodeKind::Unary: {
				const auto *unary = static_cast<Unary *>(p_node);
				if (unary->op == TokenKind::PlusPlus || unary->op == TokenKind::MinusMinus) {
					candidates.erase(local_name(unary->operand));
				}
			} break;
			default:
				break;
		}
		return true;
	});
	for (const auto &[symbol, decl] : candidates) {
		symbol->storage = decl.value->type;
	}
}

} // namespace kas3
