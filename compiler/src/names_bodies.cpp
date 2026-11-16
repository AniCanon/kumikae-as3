// The bodies pass: every type name, and every name inside a function, field initializer or default value.
//
// Lookup order for a bare name: the scopes of the enclosing functions from the inside out, then the class and
// its bases, then the package level. `var` and nested function declarations belong to their whole function,
// wherever in it they are written; a catch variable belongs to its catch block.
//
// Recursion here follows the syntax tree, which the parser keeps under about 512 levels. Chains that the
// parser builds in loops are walked in loops.
#include "names_resolver.h"

namespace kas3 {

Scope &Resolver::retain_scope(Scope &p_fallback, const Node *p_extent) {
	if (!p_extent || unit_.is_declaration) {
		return p_fallback;
	}
	Scope &scope = unit_.local_scopes.emplace_back();
	scope.extent = p_extent;
	scope.imports.parent = context_.imports;
	return scope;
}

// Imports written in the body are in force for the whole function.
void Resolver::body_imports(FunctionDecl *p_function, ImportScope &r_imports, bool p_declared) {
	r_imports.parent = context_.imports;
	if (p_declared) {
		// Resolved, and reported, with the signature.
		auto found = unit_.function_imports.find(p_function);
		if (found != unit_.function_imports.end()) {
			r_imports.imports = found->second.imports;
			r_imports.failed = found->second.failed;
			r_imports.failed_wildcard = found->second.failed_wildcard;
			context_.imports = &r_imports;
		}
		return;
	}
	if (p_function->body) {
		std::vector<Import *> written;
		hoist(p_function->body, &written);
		if (!written.empty()) {
			List<Import *> list;
			list.items = written.data();
			list.count = (uint32_t)written.size();
			compilation_.resolve_imports(unit_, list, r_imports);
			context_.imports = &r_imports;
		}
	}
}

// The parameters, the return type and the implicit `arguments`.
void Resolver::declare_parameters(FunctionDecl *p_function, Scope &r_scope, bool p_declared) {
	for (Parameter &parameter : p_function->parameters) {
		if (!p_declared) {
			type(parameter.type);
		}
		Symbol *symbol = compilation_.new_symbol(SymbolKind::Parameter, parameter.name, &unit_, parameter.offset);
		symbol->type = parameter.type;
		symbol->is_rest = parameter.is_rest;
		parameter.symbol = symbol;
		if (!r_scope.names.emplace(parameter.name, symbol).second) {
			error(parameter.offset, "duplicate parameter " + quoted(parameter.name));
		}
	}
	if (!p_declared) {
		type(p_function->return_type);
	}
	if (!arguments_) {
		arguments_ = compilation_.new_symbol(SymbolKind::Arguments, "arguments", &unit_, 0);
		arguments_->unit = nullptr;
	}
	r_scope.names.emplace("arguments", arguments_);
}

void Resolver::function(FunctionDecl *p_function, bool p_expression, bool p_declared) {
	// A named function expression can call itself by its name. The name sits just outside the body, so a
	// parameter or local may reuse it.
	Scope transient_self, transient_scope;
	Scope &self = retain_scope(transient_self, p_expression && !p_function->name.empty() ? p_function->body : nullptr);
	self.parent = scope_;
	Scope &scope = retain_scope(transient_scope, p_function->body);
	scope.parent = scope_;
	if (p_expression && !p_function->name.empty()) {
		Symbol *symbol = compilation_.new_symbol(SymbolKind::LocalFunction, p_function->name, &unit_,
				p_function->name_offset);
		symbol->node = p_function;
		symbol->type = p_function->return_type;
		p_function->symbol = symbol;
		self.names.emplace(p_function->name, symbol);
		scope.parent = &self;
	}
	Scope *outer_scope = scope_;
	Scope *outer_function = function_scope_;
	ImportScope *outer_imports = context_.imports;
	std::vector<Label> outer_labels;
	outer_labels.swap(labels_);
	int outer_loops = loops_;
	int outer_switches = switches_;
	loops_ = 0;
	switches_ = 0;

	body_imports(p_function, scope.imports, p_declared);
	declare_parameters(p_function, scope, p_declared);

	scope_ = &scope;
	function_scope_ = &scope;
	hoist(p_function->body, nullptr);
	for (Parameter &parameter : p_function->parameters) {
		expression(parameter.default_value);
	}
	if (p_function->body) {
		statement(p_function->body);
	}

	scope_ = outer_scope;
	function_scope_ = outer_function;
	context_.imports = outer_imports;
	labels_.swap(outer_labels);
	loops_ = outer_loops;
	switches_ = outer_switches;
}

// A field's or a package-level variable's: its type was resolved with the signatures.
void Resolver::variables(VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		expression(declarator.initializer);
	}
}

void Resolver::definition(Node *p_node, bool p_in_block) {
	switch (p_node->kind) {
		case NodeKind::ClassDecl: {
			ClassDecl *decl = static_cast<ClassDecl *>(p_node);
			context_ = compilation_.context_of(decl->symbol);
			for (Node *member : decl->members) {
				if (member->kind == NodeKind::VarDecl) {
					VarDecl *field = static_cast<VarDecl *>(member);
					static_ = (field->modifiers.bits & Modifiers::Static) != 0;
					variables(field);
				} else if (member->kind == NodeKind::FunctionDecl) {
					FunctionDecl *method = static_cast<FunctionDecl *>(member);
					static_ = (method->modifiers.bits & Modifiers::Static) != 0;
					function(method, false, true);
				}
			}
		} break;
		case NodeKind::InterfaceDecl: {
			InterfaceDecl *decl = static_cast<InterfaceDecl *>(p_node);
			context_ = compilation_.context_of(decl->symbol);
			static_ = false;
			for (FunctionDecl *method : decl->methods) {
				function(method, false, true);
			}
		} break;
		case NodeKind::FunctionDecl:
			context_ = compilation_.context_of(unit_, p_in_block);
			static_ = false;
			function(static_cast<FunctionDecl *>(p_node), false, true);
			break;
		case NodeKind::VarDecl:
			context_ = compilation_.context_of(unit_, p_in_block);
			static_ = false;
			variables(static_cast<VarDecl *>(p_node));
			break;
		default:
			break;
	}
}

void Resolver::run() {
	Program *tree = unit_.tree;
	if (tree->package) {
		for (Node *node : tree->package->definitions) {
			definition(node, true);
		}
	}
	for (Node *node : tree->definitions) {
		definition(node, false);
	}
}

void resolve_unit_bodies(Compilation &p_compilation, Unit &p_unit) {
	Resolver(p_compilation, p_unit).run();
}

void check_class_rules(Compilation &p_compilation, Unit &p_unit); // names_rules.cpp
void type_unit(Compilation &p_compilation, Unit &p_unit); // typer.cpp

// What a file's bodies say, and the rules its classes keep. Reads only what other files declare, so files
// are checked one at a time, in any order, once every file's signatures are resolved.
void check_unit_bodies(Compilation &p_compilation, Unit &p_unit) {
	resolve_unit_bodies(p_compilation, p_unit);
	check_class_rules(p_compilation, p_unit);
	type_unit(p_compilation, p_unit);
}

} // namespace kas3
