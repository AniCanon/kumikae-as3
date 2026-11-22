// The bodies pass: what a function body declares. `var` and nested function declarations belong to their
// whole function, wherever in it they are written, and so do the imports written in it.
#include "names_resolver.h"

namespace kas3 {

namespace {

// Two written types that mean the same. An unwritten type is `*`. A type that failed to resolve matches
// anything: it has been reported already.
bool same_type(const TypeRef *p_a, const TypeRef *p_b) {
	for (;;) {
		TypeRef::Form a = p_a ? p_a->form : TypeRef::Any;
		TypeRef::Form b = p_b ? p_b->form : TypeRef::Any;
		if (a != b) {
			return false;
		}
		if (a == TypeRef::Any || a == TypeRef::Void) {
			return true;
		}
		if (a == TypeRef::Callback) {
			if (p_a->parameters.count != p_b->parameters.count) {
				return false;
			}
			for (uint32_t i = 0; i < p_a->parameters.count; ++i) {
				if (!same_type(p_a->parameters[i], p_b->parameters[i])) {
					return false;
				}
			}
			return same_type(p_a->argument, p_b->argument);
		}
		if (!p_a->symbol || !p_b->symbol) {
			return true;
		}
		if (p_a->symbol != p_b->symbol) {
			return false;
		}
		if (a != TypeRef::Applied) {
			return true;
		}
		p_a = p_a->argument;
		p_b = p_b->argument;
	}
}

} // namespace

void Resolver::declare_locals(VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		type(declarator.type);
		auto found = function_scope_->names.find(declarator.name);
		if (found == function_scope_->names.end()) {
			Symbol *symbol = compilation_.new_symbol(SymbolKind::Local, declarator.name, &unit_, declarator.offset);
			symbol->node = p_decl;
			symbol->type = declarator.type;
			symbol->is_const = p_decl->is_const;
			declarator.symbol = symbol;
			function_scope_->names.emplace(declarator.name, symbol);
			continue;
		}
		Symbol *existing = found->second;
		if (existing->kind == SymbolKind::Arguments) {
			// Declaring `arguments` replaces the implicit one.
			Symbol *symbol = compilation_.new_symbol(SymbolKind::Local, declarator.name, &unit_, declarator.offset);
			symbol->node = p_decl;
			symbol->type = declarator.type;
			declarator.symbol = symbol;
			found->second = symbol;
			continue;
		}
		// AS3 lets one function declare a variable twice; this compiler asks that the types agree.
		bool variable = existing->kind == SymbolKind::Local || existing->kind == SymbolKind::Parameter;
		if (!variable || !same_type(existing->type, declarator.type)) {
			error(declarator.offset, quoted(declarator.name) +
											 " is already defined in this function with a different type (line " +
											 line_of(existing) + ")");
		}
		declarator.symbol = existing;
	}
}

void Resolver::declare_local_function(FunctionDecl *p_function) {
	Symbol *symbol = compilation_.new_symbol(SymbolKind::LocalFunction, p_function->name, &unit_,
			p_function->name_offset);
	symbol->node = p_function;
	symbol->type = p_function->return_type;
	p_function->symbol = symbol;
	auto inserted = function_scope_->names.emplace(p_function->name, symbol);
	if (!inserted.second) {
		Symbol *existing = inserted.first->second;
		if (existing->kind == SymbolKind::Arguments) {
			inserted.first->second = symbol;
		} else {
			error(p_function->name_offset, quoted(p_function->name) +
												   " is already defined in this function with a different type (line " +
												   line_of(existing) + ")");
		}
	}
}

void Resolver::hoist_switch(Switch *p_switch, std::vector<Import *> *r_imports) {
	for (SwitchCase &branch : p_switch->cases) {
		for (Stmt *statement : branch.body) {
			hoist(statement, r_imports);
		}
	}
}

void Resolver::hoist_try(Try *p_try, std::vector<Import *> *r_imports) {
	hoist(p_try->body, r_imports);
	for (CatchClause &clause : p_try->catches) {
		hoist(clause.body, r_imports);
	}
	hoist(p_try->finally_block, r_imports);
}

// Walks the statements of one function body, not entering nested functions. With r_imports, collects the
// body's import statements; without, declares its variables and nested functions.
void Resolver::hoist(Stmt *p_statement, std::vector<Import *> *r_imports) {
	if (!p_statement) {
		return;
	}
	switch (p_statement->kind) {
		case NodeKind::Block:
			for (Stmt *statement : static_cast<Block *>(p_statement)->statements) {
				hoist(statement, r_imports);
			}
			break;
		case NodeKind::VarStmt:
			if (!r_imports) {
				declare_locals(static_cast<VarStmt *>(p_statement)->decl);
			}
			break;
		case NodeKind::FunctionStmt:
			if (!r_imports) {
				declare_local_function(static_cast<FunctionStmt *>(p_statement)->function);
			}
			break;
		case NodeKind::ImportStmt:
			if (r_imports) {
				r_imports->push_back(static_cast<ImportStmt *>(p_statement)->import);
			}
			break;
		case NodeKind::If:
			hoist(static_cast<If *>(p_statement)->then_branch, r_imports);
			hoist(static_cast<If *>(p_statement)->else_branch, r_imports);
			break;
		case NodeKind::While:
			hoist(static_cast<While *>(p_statement)->body, r_imports);
			break;
		case NodeKind::DoWhile:
			hoist(static_cast<DoWhile *>(p_statement)->body, r_imports);
			break;
		case NodeKind::For:
			hoist(static_cast<For *>(p_statement)->init, r_imports);
			hoist(static_cast<For *>(p_statement)->body, r_imports);
			break;
		case NodeKind::ForIn: {
			ForIn *loop = static_cast<ForIn *>(p_statement);
			if (loop->declaration && !r_imports) {
				declare_locals(loop->declaration);
			}
			hoist(loop->body, r_imports);
		} break;
		case NodeKind::Switch:
			hoist_switch(static_cast<Switch *>(p_statement), r_imports);
			break;
		case NodeKind::Try:
			hoist_try(static_cast<Try *>(p_statement), r_imports);
			break;
		case NodeKind::Labeled:
			hoist(static_cast<Labeled *>(p_statement)->body, r_imports);
			break;
		default:
			break;
	}
}

} // namespace kas3
