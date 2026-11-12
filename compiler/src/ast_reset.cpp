#include "ast_walk.h"

namespace kas3 {

void clear_annotations(Program *p_tree) {
	walk(p_tree, [](Node *p_node) {
		if (is_expression(p_node->kind)) {
			static_cast<Expr *>(p_node)->type = nullptr;
		}
		switch (p_node->kind) {
			case NodeKind::TypeRef:
				static_cast<TypeRef *>(p_node)->symbol = nullptr;
				break;
			case NodeKind::Name:
				static_cast<Name *>(p_node)->symbol = nullptr;
				break;
			case NodeKind::Member:
				static_cast<Member *>(p_node)->symbol = nullptr;
				static_cast<Member *>(p_node)->member = nullptr;
				break;
			case NodeKind::VarDecl:
				for (Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
					declarator.symbol = nullptr;
				}
				break;
			case NodeKind::FunctionDecl:
				static_cast<FunctionDecl *>(p_node)->symbol = nullptr;
				for (Parameter &parameter : static_cast<FunctionDecl *>(p_node)->parameters) {
					parameter.symbol = nullptr;
				}
				break;
			case NodeKind::Try:
				for (CatchClause &clause : static_cast<Try *>(p_node)->catches) {
					clause.symbol = nullptr;
				}
				break;
			case NodeKind::ClassDecl:
				static_cast<ClassDecl *>(p_node)->symbol = nullptr;
				break;
			case NodeKind::InterfaceDecl:
				static_cast<InterfaceDecl *>(p_node)->symbol = nullptr;
				break;
			case NodeKind::Import:
				static_cast<Import *>(p_node)->symbol = nullptr;
				break;
			default:
				break;
		}
	});
}

} // namespace kas3
