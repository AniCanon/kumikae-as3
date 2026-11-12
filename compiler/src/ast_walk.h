// Visiting every node of a syntax tree.
#pragma once

#include "ast.h"

namespace kas3 {

// Calls p_visit(Node *) on the node and on everything below it, parents first. Null is allowed. The parts
// of a tree that are not nodes (declarators, parameters, catch clauses, switch cases, object fields) are
// passed through: the visitor reaches them from the node that holds them.
template <class F>
void walk(Node *p_node, F &&p_visit) {
	if (!p_node) {
		return;
	}
	p_visit(p_node);
	auto each = [&](const auto &p_list) {
		for (auto *item : p_list) {
			walk(item, p_visit);
		}
	};
	switch (p_node->kind) {
		case NodeKind::TypeRef:
			walk(static_cast<TypeRef *>(p_node)->argument, p_visit);
			break;
		case NodeKind::NumberLit:
		case NodeKind::StringLit:
		case NodeKind::RegexLit:
		case NodeKind::BoolLit:
		case NodeKind::NullLit:
		case NodeKind::ThisExpr:
		case NodeKind::SuperExpr:
		case NodeKind::Name:
		case NodeKind::Empty:
		case NodeKind::Break:
		case NodeKind::Continue:
		case NodeKind::Import:
			break;
		case NodeKind::ArrayLit:
			each(static_cast<ArrayLit *>(p_node)->elements);
			break;
		case NodeKind::ObjectLit:
			for (const ObjectField &field : static_cast<ObjectLit *>(p_node)->fields) {
				walk(field.value, p_visit);
			}
			break;
		case NodeKind::VectorLit:
			walk(static_cast<VectorLit *>(p_node)->element_type, p_visit);
			each(static_cast<VectorLit *>(p_node)->elements);
			break;
		case NodeKind::FunctionExpr:
			walk(static_cast<FunctionExpr *>(p_node)->function, p_visit);
			break;
		case NodeKind::Member:
			walk(static_cast<Member *>(p_node)->object, p_visit);
			break;
		case NodeKind::Index:
			walk(static_cast<Index *>(p_node)->object, p_visit);
			walk(static_cast<Index *>(p_node)->index, p_visit);
			break;
		case NodeKind::TypeApply:
			walk(static_cast<TypeApply *>(p_node)->base, p_visit);
			walk(static_cast<TypeApply *>(p_node)->argument, p_visit);
			break;
		case NodeKind::Call:
			walk(static_cast<Call *>(p_node)->callee, p_visit);
			each(static_cast<Call *>(p_node)->arguments);
			break;
		case NodeKind::New:
			walk(static_cast<New *>(p_node)->callee, p_visit);
			each(static_cast<New *>(p_node)->arguments);
			break;
		case NodeKind::Unary:
			walk(static_cast<Unary *>(p_node)->operand, p_visit);
			break;
		case NodeKind::Postfix:
			walk(static_cast<Postfix *>(p_node)->operand, p_visit);
			break;
		case NodeKind::Binary:
			walk(static_cast<Binary *>(p_node)->left, p_visit);
			walk(static_cast<Binary *>(p_node)->right, p_visit);
			break;
		case NodeKind::Assign:
			walk(static_cast<Assign *>(p_node)->target, p_visit);
			walk(static_cast<Assign *>(p_node)->value, p_visit);
			break;
		case NodeKind::Conditional:
			walk(static_cast<Conditional *>(p_node)->condition, p_visit);
			walk(static_cast<Conditional *>(p_node)->then_value, p_visit);
			walk(static_cast<Conditional *>(p_node)->else_value, p_visit);
			break;
		case NodeKind::Block:
			each(static_cast<Block *>(p_node)->statements);
			break;
		case NodeKind::ExprStmt:
			walk(static_cast<ExprStmt *>(p_node)->expr, p_visit);
			break;
		case NodeKind::VarStmt:
			walk(static_cast<VarStmt *>(p_node)->decl, p_visit);
			break;
		case NodeKind::If:
			walk(static_cast<If *>(p_node)->condition, p_visit);
			walk(static_cast<If *>(p_node)->then_branch, p_visit);
			walk(static_cast<If *>(p_node)->else_branch, p_visit);
			break;
		case NodeKind::While:
			walk(static_cast<While *>(p_node)->condition, p_visit);
			walk(static_cast<While *>(p_node)->body, p_visit);
			break;
		case NodeKind::DoWhile:
			walk(static_cast<DoWhile *>(p_node)->body, p_visit);
			walk(static_cast<DoWhile *>(p_node)->condition, p_visit);
			break;
		case NodeKind::For:
			walk(static_cast<For *>(p_node)->init, p_visit);
			walk(static_cast<For *>(p_node)->condition, p_visit);
			walk(static_cast<For *>(p_node)->update, p_visit);
			walk(static_cast<For *>(p_node)->body, p_visit);
			break;
		case NodeKind::ForIn:
			walk(static_cast<ForIn *>(p_node)->declaration, p_visit);
			walk(static_cast<ForIn *>(p_node)->target, p_visit);
			walk(static_cast<ForIn *>(p_node)->collection, p_visit);
			walk(static_cast<ForIn *>(p_node)->body, p_visit);
			break;
		case NodeKind::Switch:
			walk(static_cast<Switch *>(p_node)->subject, p_visit);
			for (const SwitchCase &item : static_cast<Switch *>(p_node)->cases) {
				walk(item.test, p_visit);
				each(item.body);
			}
			break;
		case NodeKind::Return:
			walk(static_cast<Return *>(p_node)->value, p_visit);
			break;
		case NodeKind::Throw:
			walk(static_cast<Throw *>(p_node)->value, p_visit);
			break;
		case NodeKind::Try:
			walk(static_cast<Try *>(p_node)->body, p_visit);
			for (const CatchClause &clause : static_cast<Try *>(p_node)->catches) {
				walk(clause.type, p_visit);
				walk(clause.body, p_visit);
			}
			walk(static_cast<Try *>(p_node)->finally_block, p_visit);
			break;
		case NodeKind::Labeled:
			walk(static_cast<Labeled *>(p_node)->body, p_visit);
			break;
		case NodeKind::FunctionStmt:
			walk(static_cast<FunctionStmt *>(p_node)->function, p_visit);
			break;
		case NodeKind::ImportStmt:
			walk(static_cast<ImportStmt *>(p_node)->import, p_visit);
			break;
		case NodeKind::VarDecl:
			for (const Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
				walk(declarator.type, p_visit);
				walk(declarator.initializer, p_visit);
			}
			break;
		case NodeKind::FunctionDecl:
			for (const Parameter &parameter : static_cast<FunctionDecl *>(p_node)->parameters) {
				walk(parameter.type, p_visit);
				walk(parameter.default_value, p_visit);
			}
			walk(static_cast<FunctionDecl *>(p_node)->return_type, p_visit);
			walk(static_cast<FunctionDecl *>(p_node)->body, p_visit);
			break;
		case NodeKind::ClassDecl:
			walk(static_cast<ClassDecl *>(p_node)->extends, p_visit);
			each(static_cast<ClassDecl *>(p_node)->implements);
			each(static_cast<ClassDecl *>(p_node)->members);
			break;
		case NodeKind::InterfaceDecl:
			each(static_cast<InterfaceDecl *>(p_node)->extends);
			each(static_cast<InterfaceDecl *>(p_node)->methods);
			break;
		case NodeKind::PackageDecl:
			each(static_cast<PackageDecl *>(p_node)->imports);
			each(static_cast<PackageDecl *>(p_node)->definitions);
			break;
		case NodeKind::Program:
			walk(static_cast<Program *>(p_node)->package, p_visit);
			each(static_cast<Program *>(p_node)->imports);
			each(static_cast<Program *>(p_node)->definitions);
			break;
	}
}

// True for the node kinds that are expressions.
inline bool is_expression(NodeKind p_kind) {
	return p_kind >= NodeKind::NumberLit && p_kind <= NodeKind::Conditional;
}

// Removes what name resolution and the types pass wrote onto the tree, so that it can be checked again.
void clear_annotations(Program *p_tree);

} // namespace kas3
