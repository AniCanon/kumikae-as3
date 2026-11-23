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
		case NodeKind::New:
		case NodeKind::StringLit:
		case NodeKind::BoolLit:
			return p_expr->type;
		case NodeKind::Name: {
			const Symbol *source = local_name(p_expr);
			return source && (source->kind == SymbolKind::Local || source->kind == SymbolKind::Parameter) ? p_expr->type : nullptr;
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
	std::unordered_map<Symbol *, const Declarator *> candidates;
	for (Stmt *statement : p_body->statements) {
		if (statement->kind == NodeKind::VarStmt) {
			for (auto &decl : static_cast<VarStmt *>(statement)->decl->declarators) {
				if (decl.symbol && decl.symbol->kind == SymbolKind::Local && (!decl.type || decl.type->form == TypeRef::Any) && stored_type(decl.initializer)) {
					candidates.emplace(decl.symbol, &decl);
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
					if (it != candidates.end() && it->second != &decl) {
						candidates.erase(it);
					}
				}
				break;
			case NodeKind::Name: {
				auto it = candidates.find(static_cast<Name *>(p_node)->symbol);
				if (it != candidates.end() && p_node->offset < it->second->initializer->end) {
					candidates.erase(it);
				}
			} break;
			case NodeKind::Assign: {
				const auto *assign = static_cast<Assign *>(p_node);
				auto it = candidates.find(local_name(assign->target));
				if (it != candidates.end() && (assign->op != TokenKind::Assign || stored_type(assign->value) != it->second->initializer->type)) {
					candidates.erase(it);
				}
			} break;
			case NodeKind::ForIn:
				candidates.erase(local_name(static_cast<ForIn *>(p_node)->target));
				break;
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
		symbol->storage = decl->initializer->type;
	}
}

} // namespace kas3
