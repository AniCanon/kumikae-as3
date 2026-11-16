// The signature pass: every type written outside a function body, resolved before any body. Typing one file
// reads the declarations of others (a field's type, a method's parameters), so these must be settled
// everywhere first; then each file's bodies can be checked on their own, in any order.
#include "names_resolver.h"

namespace kas3 {

// The imports a declared function's body writes are in force for its parameters and return type too.
void Resolver::signature(FunctionDecl *p_function) {
	ImportScope *outer = context_.imports;
	std::vector<Import *> written;
	hoist(p_function->body, &written);
	if (!written.empty()) {
		ImportScope &scope = unit_.function_imports[p_function];
		scope.parent = context_.imports;
		List<Import *> list;
		list.items = written.data();
		list.count = (uint32_t)written.size();
		compilation_.resolve_imports(unit_, list, scope);
		context_.imports = &scope;
	}
	for (Parameter &parameter : p_function->parameters) {
		type(parameter.type);
	}
	type(p_function->return_type);
	context_.imports = outer;
}

void Resolver::signatures(Node *p_node, bool p_in_block) {
	auto variables = [this](VarDecl *p_decl) {
		for (Declarator &declarator : p_decl->declarators) {
			type(declarator.type);
		}
	};
	switch (p_node->kind) {
		case NodeKind::ClassDecl: {
			ClassDecl *decl = static_cast<ClassDecl *>(p_node);
			context_ = compilation_.context_of(decl->symbol);
			for (Node *member : decl->members) {
				if (member->kind == NodeKind::VarDecl) {
					variables(static_cast<VarDecl *>(member));
				} else if (member->kind == NodeKind::FunctionDecl) {
					signature(static_cast<FunctionDecl *>(member));
				}
			}
		} break;
		case NodeKind::InterfaceDecl: {
			InterfaceDecl *decl = static_cast<InterfaceDecl *>(p_node);
			context_ = compilation_.context_of(decl->symbol);
			for (FunctionDecl *method : decl->methods) {
				signature(method);
			}
		} break;
		case NodeKind::FunctionDecl:
			context_ = compilation_.context_of(unit_, p_in_block);
			signature(static_cast<FunctionDecl *>(p_node));
			break;
		case NodeKind::VarDecl:
			context_ = compilation_.context_of(unit_, p_in_block);
			variables(static_cast<VarDecl *>(p_node));
			break;
		default:
			break;
	}
}

void Resolver::run_signatures() {
	Program *tree = unit_.tree;
	if (tree->package) {
		for (Node *node : tree->package->definitions) {
			signatures(node, true);
		}
	}
	for (Node *node : tree->definitions) {
		signatures(node, false);
	}
}

void resolve_unit_signatures(Compilation &p_compilation, Unit &p_unit) {
	Resolver(p_compilation, p_unit).run_signatures();
}

void resolve_signatures(Compilation &p_compilation) {
	for (Unit &unit : p_compilation.units()) {
		resolve_unit_signatures(p_compilation, unit);
	}
}

} // namespace kas3
