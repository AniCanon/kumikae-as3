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
			return source && (source->kind == SymbolKind::Local || source->kind == SymbolKind::Parameter || source->kind == SymbolKind::Field || source->kind == SymbolKind::Property || source->kind == SymbolKind::Variable) ? p_expr->type : nullptr;
		}
		case NodeKind::Member: {
			const Symbol *source = static_cast<Member *>(p_expr)->resolved();
			return source && (source->kind == SymbolKind::Field || source->kind == SymbolKind::Property) ? p_expr->type : nullptr;
		}
		case NodeKind::Call: {
			const Expr *callee = static_cast<Call *>(p_expr)->callee;
			if (callee->type && callee->type->kind == Type::Callback) {
				return p_expr->type;
			}
			const Symbol *target = callee->kind == NodeKind::Name ? static_cast<const Name *>(callee)->symbol
					: callee->kind == NodeKind::Member ? static_cast<const Member *>(callee)->resolved() : nullptr;
			return target && (target->kind == SymbolKind::Function || target->kind == SymbolKind::Method || target->kind == SymbolKind::LocalFunction)
					? p_expr->type : nullptr;
		}
		default:
			return nullptr; // A numeric 'as', for example, may also produce null.
	}
}

struct Candidate {
	const Declarator *declaration;
	Expr *value;
	Expr *target;
	const Block *region;
};
using Candidates = std::unordered_map<Symbol *, Candidate>;

void find_initial_writes(Block *p_block, Block *p_region, Candidates &r_candidates) {
	for (Stmt *statement : p_block->statements) {
		if (statement->kind == NodeKind::Block) {
			find_initial_writes(static_cast<Block *>(statement), p_region, r_candidates);
		} else if (statement->kind == NodeKind::VarStmt) {
			for (auto &decl : static_cast<VarStmt *>(statement)->decl->declarators) {
				if (decl.symbol && decl.symbol->kind == SymbolKind::Local && (!decl.type || decl.type->form == TypeRef::Any) && stored_type(decl.initializer)) {
					r_candidates.emplace(decl.symbol, Candidate{ &decl, decl.initializer, nullptr, p_region });
				}
			}
		} else if (statement->kind == NodeKind::ExprStmt) {
			Expr *expr = static_cast<ExprStmt *>(statement)->expr;
			if (expr && expr->kind == NodeKind::Assign) {
				auto *assign = static_cast<Assign *>(expr);
				Symbol *local = local_name(assign->target);
				if (assign->op == TokenKind::Assign && local && local->kind == SymbolKind::Local &&
						(!local->type || local->type->form == TypeRef::Any) && stored_type(assign->value)) {
					r_candidates.emplace(local, Candidate{ nullptr, assign->value, assign->target, p_region });
				}
			}
		} else {
			walk(statement, [&](Node *node) {
				if (node->kind == NodeKind::Block) {
					auto *block = static_cast<Block *>(node);
					find_initial_writes(block, block, r_candidates);
					return false;
				}
				return node->kind != NodeKind::FunctionDecl;
			});
		}
	}
}

bool validate_use(Node *p_node, Candidates &r_candidates) {
	switch (p_node->kind) {
		case NodeKind::FunctionDecl:
			walk(p_node, [&](Node *p_part) {
				if (p_part->kind == NodeKind::Name) {
					r_candidates.erase(static_cast<Name *>(p_part)->symbol);
				}
			});
			return false;
		case NodeKind::VarDecl:
			for (auto &decl : static_cast<VarDecl *>(p_node)->declarators) {
				auto it = r_candidates.find(decl.symbol);
				if (it != r_candidates.end() && decl.initializer && it->second.declaration != &decl) {
					r_candidates.erase(it);
				}
			}
			break;
		case NodeKind::Name: {
			auto it = r_candidates.find(static_cast<Name *>(p_node)->symbol);
			if (it != r_candidates.end() && p_node != it->second.target && (p_node->offset < it->second.value->end || p_node->end > it->second.region->end)) {
				r_candidates.erase(it);
			}
		} break;
		case NodeKind::Assign: {
			const auto *assign = static_cast<Assign *>(p_node);
			auto it = r_candidates.find(local_name(assign->target));
			if (it != r_candidates.end() && (assign->op != TokenKind::Assign || stored_type(assign->value) != it->second.value->type)) {
				r_candidates.erase(it);
			}
		} break;
		case NodeKind::ForIn: {
			const auto *loop = static_cast<ForIn *>(p_node);
			r_candidates.erase(local_name(loop->target));
			if (loop->declaration) {
				for (auto &decl : loop->declaration->declarators) {
					r_candidates.erase(decl.symbol);
				}
			}
		} break;
		case NodeKind::Postfix:
			r_candidates.erase(local_name(static_cast<Postfix *>(p_node)->operand));
			break;
		case NodeKind::Unary: {
			const auto *unary = static_cast<Unary *>(p_node);
			if (unary->op == TokenKind::PlusPlus || unary->op == TokenKind::MinusMinus) {
				r_candidates.erase(local_name(unary->operand));
			}
		} break;
		default:
			break;
	}
	return true;
}

} // namespace

void Typer::infer_storage(Block *p_body) {
	if (!p_body || unit_.diagnostics.has_errors()) {
		return;
	}
	Candidates candidates;
	find_initial_writes(p_body, p_body, candidates);
	if (candidates.empty()) {
		return;
	}
	walk(p_body, [&](Node *node) { return validate_use(node, candidates); });
	std::unordered_map<Symbol *, Expr *> initial;
	for (const auto &[symbol, decl] : candidates) {
		initial.emplace(symbol, decl.value);
	}
	prove_local_uses(p_body, initial);
}

} // namespace kas3
