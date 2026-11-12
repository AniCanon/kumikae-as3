// The bodies pass: every type name, and every name inside a function, field initializer or default value.
//
// Lookup order for a bare name: the scopes of the enclosing functions from the inside out, then the class and
// its bases, then the package level. `var` and nested function declarations belong to their whole function,
// wherever in it they are written; a catch variable belongs to its catch block.
//
// Recursion here follows the syntax tree, which the parser keeps under about 512 levels. Chains that the
// parser builds in loops are walked in loops.
#include "names.h"

namespace kas3 {

namespace {

struct Scope {
	Scope *parent = nullptr;
	std::unordered_map<std::string_view, Symbol *> names;
};

struct Label {
	std::string_view name;
	bool loop;
};

std::string quoted(std::string_view p_name) {
	std::string result = "'";
	result += p_name;
	result += '\'';
	return result;
}

bool is_loop(const Stmt *p_statement) {
	// `a: b: for (;;)` labels the loop twice.
	while (p_statement->kind == NodeKind::Labeled && static_cast<const Labeled *>(p_statement)->body) {
		p_statement = static_cast<const Labeled *>(p_statement)->body;
	}
	switch (p_statement->kind) {
		case NodeKind::While:
		case NodeKind::DoWhile:
		case NodeKind::For:
		case NodeKind::ForIn:
			return true;
		default:
			return false;
	}
}

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

class Resolver {
	Compilation &compilation_;
	Unit &unit_;
	Context context_;
	bool static_ = false; // Inside a static method or a static field's initializer.
	Scope *scope_ = nullptr; // Innermost.
	Scope *function_scope_ = nullptr; // Of the innermost function: where `var` declares.
	std::vector<Label> labels_;
	int loops_ = 0;
	int switches_ = 0;
	Symbol *arguments_ = nullptr;

	void error(uint32_t p_offset, std::string p_message) { unit_.diagnostics.error(p_offset, std::move(p_message)); }
	void record(uint32_t p_offset, std::string_view p_name, Symbol *p_symbol) {
		unit_.references.push_back({ p_offset, p_name, p_symbol });
	}
	std::string line_of(const Symbol *p_symbol) const {
		return std::to_string(unit_.source.position(p_symbol->offset).line);
	}

	// ---- lookup ----

	Symbol *lookup(std::string_view p_name, uint32_t p_offset, bool p_report) {
		for (Scope *scope = scope_; scope; scope = scope->parent) {
			auto found = scope->names.find(p_name);
			if (found != scope->names.end()) {
				return found->second;
			}
		}
		Symbol *hidden = nullptr;
		Symbol *instance = nullptr; // An instance member that static code can't reach.
		if (context_.klass && context_.klass->kind == SymbolKind::Class) {
			Symbol *member = compilation_.find_member(context_.klass, p_name, static_, &instance, &hidden);
			if (member) {
				return member;
			}
		}
		Found found = compilation_.lookup_package_level(context_, p_name);
		if (found.first && !found.second) {
			return found.first;
		}
		if (!p_report) {
			return nullptr;
		}
		if (found.second) {
			error(p_offset, compilation_.ambiguous_message(p_name, found));
		} else if (instance) {
			error(p_offset, "can't use the instance member " + quoted(p_name) + " in a static context");
		} else if (found.suppressed) {
			// A failed import has been reported, and may be where this name was to come from.
		} else if (hidden) {
			if (hidden->modifiers & Modifiers::Private) {
				error(p_offset, quoted(p_name) + " is private in " + std::string(hidden->owner->name));
			} else {
				error(p_offset, quoted(p_name) + " in " + std::string(hidden->owner->name) +
										" is not visible from this package");
			}
		} else if (!(context_.klass && compilation_.hierarchy_broken(context_.klass))) {
			// A base class that failed to resolve may be where the name comes from.
			error(p_offset, "undefined name " + quoted(p_name));
		}
		return nullptr;
	}

	void type(TypeRef *p_type) { compilation_.resolve_type(context_, p_type); }

	// ---- declarations inside a function ----

	void declare_locals(VarDecl *p_decl) {
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

	void declare_local_function(FunctionDecl *p_function) {
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

	// Walks the statements of one function body, not entering nested functions. With r_imports, collects the
	// body's import statements; without, declares its variables and nested functions.
	void hoist(Stmt *p_statement, std::vector<Import *> *r_imports) {
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
				for (SwitchCase &branch : static_cast<Switch *>(p_statement)->cases) {
					for (Stmt *statement : branch.body) {
						hoist(statement, r_imports);
					}
				}
				break;
			case NodeKind::Try: {
				Try *attempt = static_cast<Try *>(p_statement);
				hoist(attempt->body, r_imports);
				for (CatchClause &clause : attempt->catches) {
					hoist(clause.body, r_imports);
				}
				hoist(attempt->finally_block, r_imports);
			} break;
			case NodeKind::Labeled:
				hoist(static_cast<Labeled *>(p_statement)->body, r_imports);
				break;
			default:
				break;
		}
	}

	// ---- functions ----

	void function(FunctionDecl *p_function, bool p_expression) {
		// A named function expression can call itself by its name. The name sits just outside the body, so a
		// parameter or local may reuse it.
		Scope self;
		self.parent = scope_;
		Scope scope;
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

		// Imports written in the body are in force for the whole function.
		ImportScope imports;
		imports.parent = context_.imports;
		if (p_function->body) {
			std::vector<Import *> written;
			hoist(p_function->body, &written);
			if (!written.empty()) {
				List<Import *> list;
				list.items = written.data();
				list.count = (uint32_t)written.size();
				compilation_.resolve_imports(unit_, list, imports);
				context_.imports = &imports;
			}
		}

		for (Parameter &parameter : p_function->parameters) {
			type(parameter.type);
			Symbol *symbol = compilation_.new_symbol(SymbolKind::Parameter, parameter.name, &unit_, parameter.offset);
			symbol->type = parameter.type;
			symbol->is_rest = parameter.is_rest;
			parameter.symbol = symbol;
			if (!scope.names.emplace(parameter.name, symbol).second) {
				error(parameter.offset, "duplicate parameter " + quoted(parameter.name));
			}
		}
		type(p_function->return_type);
		if (!arguments_) {
			arguments_ = compilation_.new_symbol(SymbolKind::Arguments, "arguments", nullptr, 0);
		}
		scope.names.emplace("arguments", arguments_);

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

	// ---- statements ----

	const Label *find_label(std::string_view p_name) const {
		for (size_t i = labels_.size(); i-- > 0;) {
			if (labels_[i].name == p_name) {
				return &labels_[i];
			}
		}
		return nullptr;
	}

	void loop_body(Stmt *p_body) {
		loops_++;
		statement(p_body);
		loops_--;
	}

	void statement(Stmt *p_statement) {
		if (!p_statement) {
			return;
		}
		switch (p_statement->kind) {
			case NodeKind::Block:
				for (Stmt *inner : static_cast<Block *>(p_statement)->statements) {
					statement(inner);
				}
				break;
			case NodeKind::ExprStmt:
				expression(static_cast<ExprStmt *>(p_statement)->expr);
				break;
			case NodeKind::VarStmt:
				// The names and types were handled when the function was entered.
				for (Declarator &declarator : static_cast<VarStmt *>(p_statement)->decl->declarators) {
					expression(declarator.initializer);
				}
				break;
			case NodeKind::If: {
				If *branch = static_cast<If *>(p_statement);
				expression(branch->condition);
				statement(branch->then_branch);
				statement(branch->else_branch);
			} break;
			case NodeKind::While: {
				While *loop = static_cast<While *>(p_statement);
				expression(loop->condition);
				loop_body(loop->body);
			} break;
			case NodeKind::DoWhile: {
				DoWhile *loop = static_cast<DoWhile *>(p_statement);
				loop_body(loop->body);
				expression(loop->condition);
			} break;
			case NodeKind::For: {
				For *loop = static_cast<For *>(p_statement);
				statement(loop->init);
				expression(loop->condition);
				expression(loop->update);
				loop_body(loop->body);
			} break;
			case NodeKind::ForIn: {
				ForIn *loop = static_cast<ForIn *>(p_statement);
				if (loop->target) {
					assignable(loop->target);
					expression(loop->target);
				}
				expression(loop->collection);
				loop_body(loop->body);
			} break;
			case NodeKind::Switch: {
				Switch *choice = static_cast<Switch *>(p_statement);
				expression(choice->subject);
				switches_++;
				for (SwitchCase &branch : choice->cases) {
					expression(branch.test);
					for (Stmt *inner : branch.body) {
						statement(inner);
					}
				}
				switches_--;
			} break;
			case NodeKind::Break: {
				Break *jump = static_cast<Break *>(p_statement);
				if (jump->label.empty()) {
					if (!loops_ && !switches_) {
						error(jump->offset, "'break' is only allowed in a loop or switch");
					}
				} else if (!find_label(jump->label)) {
					error(jump->label_offset, "undefined label " + quoted(jump->label));
				}
			} break;
			case NodeKind::Continue: {
				Continue *jump = static_cast<Continue *>(p_statement);
				if (jump->label.empty()) {
					if (!loops_) {
						error(jump->offset, "'continue' is only allowed in a loop");
					}
				} else if (const Label *label = find_label(jump->label)) {
					if (!label->loop) {
						error(jump->label_offset, quoted(jump->label) + " does not label a loop");
					}
				} else {
					error(jump->label_offset, "undefined label " + quoted(jump->label));
				}
			} break;
			case NodeKind::Return:
				expression(static_cast<Return *>(p_statement)->value);
				break;
			case NodeKind::Throw:
				expression(static_cast<Throw *>(p_statement)->value);
				break;
			case NodeKind::Try: {
				Try *attempt = static_cast<Try *>(p_statement);
				statement(attempt->body);
				for (CatchClause &clause : attempt->catches) {
					type(clause.type);
					Scope scope;
					scope.parent = scope_;
					if (!clause.name.empty()) {
						Symbol *symbol = compilation_.new_symbol(SymbolKind::CatchVariable, clause.name, &unit_,
								clause.name_offset);
						symbol->type = clause.type;
						clause.symbol = symbol;
						scope.names.emplace(clause.name, symbol);
					}
					scope_ = &scope;
					statement(clause.body);
					scope_ = scope.parent;
				}
				statement(attempt->finally_block);
			} break;
			case NodeKind::Labeled: {
				Labeled *labeled = static_cast<Labeled *>(p_statement);
				labels_.push_back({ labeled->label, labeled->body && is_loop(labeled->body) });
				statement(labeled->body);
				labels_.pop_back();
			} break;
			case NodeKind::FunctionStmt:
				function(static_cast<FunctionStmt *>(p_statement)->function, false);
				break;
			default:
				break;
		}
	}

	// ---- expressions ----

	void assignable(Expr *p_target) {
		if (!p_target) {
			return;
		}
		switch (p_target->kind) {
			case NodeKind::Name:
			case NodeKind::Member:
			case NodeKind::Index:
				break;
			default:
				error(p_target->offset, "can't assign to this expression");
		}
	}

	void name(Name *p_name) {
		if (p_name->text.empty()) {
			return; // A placeholder for an expression the parser already reported as missing.
		}
		p_name->symbol = lookup(p_name->text, p_name->offset, true);
		if (p_name->symbol) {
			record(p_name->offset, p_name->text, p_name->symbol);
		}
	}

	// `a.b.c`: either a value `a` with members, or a definition named by its package.
	void member(Member *p_member) {
		std::vector<Member *> chain; // Outermost first.
		Expr *root = p_member;
		while (root->kind == NodeKind::Member) {
			chain.push_back(static_cast<Member *>(root));
			root = static_cast<Member *>(root)->object;
		}
		if (!root) {
			return;
		}
		if (root->kind != NodeKind::Name) {
			expression(root);
			return;
		}
		Name *first = static_cast<Name *>(root);
		if (first->text.empty()) {
			return;
		}
		first->symbol = lookup(first->text, first->offset, false);
		if (first->symbol) {
			record(first->offset, first->text, first->symbol);
			return;
		}
		// Not a value. The longest leading run that names a package, followed by a definition in it.
		std::string package(first->text);
		Member *best = nullptr;
		Symbol *best_symbol = nullptr;
		for (size_t i = chain.size(); i-- > 0;) {
			Member *link = chain[i];
			if (compilation_.find_package(package)) {
				Found found = compilation_.lookup_qualified(context_, package, link->name);
				if (found.first) {
					best = link;
					best_symbol = found.first;
				}
			}
			package += '.';
			package += link->name;
		}
		if (best) {
			best->symbol = best_symbol;
			record(best->name_offset, best->name, best_symbol);
			return;
		}
		name(first); // Reports it.
	}

	void expressions(const List<Expr *> &p_list) {
		for (Expr *item : p_list) {
			expression(item);
		}
	}

	void expression(Expr *p_expression) {
		if (!p_expression) {
			return;
		}
		switch (p_expression->kind) {
			case NodeKind::Name:
				name(static_cast<Name *>(p_expression));
				break;
			case NodeKind::ThisExpr:
				if (static_) {
					error(p_expression->offset, "'this' can't be used in a static context");
				}
				break;
			case NodeKind::SuperExpr:
				if (static_ || !context_.klass || context_.klass->kind != SymbolKind::Class) {
					error(p_expression->offset, "'super' can only be used in an instance method");
				}
				break;
			case NodeKind::ArrayLit:
				expressions(static_cast<ArrayLit *>(p_expression)->elements);
				break;
			case NodeKind::ObjectLit:
				for (ObjectField &field : static_cast<ObjectLit *>(p_expression)->fields) {
					expression(field.value);
				}
				break;
			case NodeKind::VectorLit:
				type(static_cast<VectorLit *>(p_expression)->element_type);
				expressions(static_cast<VectorLit *>(p_expression)->elements);
				break;
			case NodeKind::FunctionExpr:
				function(static_cast<FunctionExpr *>(p_expression)->function, true);
				break;
			case NodeKind::Member:
				member(static_cast<Member *>(p_expression));
				break;
			case NodeKind::Index:
				expression(static_cast<Index *>(p_expression)->object);
				expression(static_cast<Index *>(p_expression)->index);
				break;
			case NodeKind::TypeApply:
				expression(static_cast<TypeApply *>(p_expression)->base);
				type(static_cast<TypeApply *>(p_expression)->argument);
				break;
			case NodeKind::Call:
				expression(static_cast<Call *>(p_expression)->callee);
				expressions(static_cast<Call *>(p_expression)->arguments);
				break;
			case NodeKind::New:
				expression(static_cast<New *>(p_expression)->callee);
				expressions(static_cast<New *>(p_expression)->arguments);
				break;
			case NodeKind::Unary: {
				Unary *unary = static_cast<Unary *>(p_expression);
				if (unary->op == TokenKind::PlusPlus || unary->op == TokenKind::MinusMinus) {
					assignable(unary->operand);
				}
				expression(unary->operand);
			} break;
			case NodeKind::Postfix:
				assignable(static_cast<Postfix *>(p_expression)->operand);
				expression(static_cast<Postfix *>(p_expression)->operand);
				break;
			case NodeKind::Binary:
				expression(static_cast<Binary *>(p_expression)->left);
				expression(static_cast<Binary *>(p_expression)->right);
				break;
			case NodeKind::Assign:
				assignable(static_cast<Assign *>(p_expression)->target);
				expression(static_cast<Assign *>(p_expression)->target);
				expression(static_cast<Assign *>(p_expression)->value);
				break;
			case NodeKind::Conditional:
				expression(static_cast<Conditional *>(p_expression)->condition);
				expression(static_cast<Conditional *>(p_expression)->then_value);
				expression(static_cast<Conditional *>(p_expression)->else_value);
				break;
			default:
				break;
		}
	}

	// ---- definitions ----

	void variables(VarDecl *p_decl) {
		for (Declarator &declarator : p_decl->declarators) {
			type(declarator.type);
			expression(declarator.initializer);
		}
	}

	void definition(Node *p_node, bool p_in_block) {
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
						function(method, false);
					}
				}
			} break;
			case NodeKind::InterfaceDecl: {
				InterfaceDecl *decl = static_cast<InterfaceDecl *>(p_node);
				context_ = compilation_.context_of(decl->symbol);
				static_ = false;
				for (FunctionDecl *method : decl->methods) {
					function(method, false);
				}
			} break;
			case NodeKind::FunctionDecl:
				context_ = compilation_.context_of(unit_, p_in_block);
				static_ = false;
				function(static_cast<FunctionDecl *>(p_node), false);
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

public:
	Resolver(Compilation &p_compilation, Unit &p_unit) :
			compilation_(p_compilation), unit_(p_unit) {}

	void run() {
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
};

} // namespace

void resolve_bodies(Compilation &p_compilation) {
	for (Unit &unit : p_compilation.units()) {
		Resolver(p_compilation, unit).run();
	}
}

} // namespace kas3
