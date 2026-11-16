#include "ast_walk.h"

namespace kas3 {

namespace {

void clear_node(Node *p_node) {
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
		case NodeKind::ForIn:
			static_cast<ForIn *>(p_node)->element = nullptr;
			break;
		default:
			break;
	}
}

void clear(Node *p_node) {
	walk(p_node, clear_node);
}

} // namespace

void clear_annotations(Program *p_tree) {
	clear(p_tree);
}

// What the bodies pass and the types pass write: inside function bodies, default values and initializers,
// and the parameters' own symbols. What the declarations and the signature pass wrote stays.
void clear_body_annotations(Program *p_tree) {
	const auto function = [](FunctionDecl *p_function) {
		for (Parameter &parameter : p_function->parameters) {
			parameter.symbol = nullptr;
			clear(parameter.default_value);
		}
		clear(p_function->body);
	};
	const auto variables = [](VarDecl *p_decl) {
		for (Declarator &declarator : p_decl->declarators) {
			clear(declarator.initializer);
		}
	};
	const auto definition = [&](Node *p_node) {
		if (p_node->kind == NodeKind::FunctionDecl) {
			function(static_cast<FunctionDecl *>(p_node));
		} else if (p_node->kind == NodeKind::VarDecl) {
			variables(static_cast<VarDecl *>(p_node));
		} else if (p_node->kind == NodeKind::InterfaceDecl) {
			for (FunctionDecl *method : static_cast<InterfaceDecl *>(p_node)->methods) {
				function(method);
			}
		} else if (p_node->kind == NodeKind::ClassDecl) {
			for (Node *member : static_cast<ClassDecl *>(p_node)->members) {
				if (member->kind == NodeKind::FunctionDecl) {
					function(static_cast<FunctionDecl *>(member));
				} else if (member->kind == NodeKind::VarDecl) {
					variables(static_cast<VarDecl *>(member));
				}
			}
		}
	};
	if (p_tree->package) {
		for (Node *node : p_tree->package->definitions) {
			definition(node);
		}
	}
	for (Node *node : p_tree->definitions) {
		definition(node);
	}
}

} // namespace kas3
