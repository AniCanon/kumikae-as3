#include "typer.h"

namespace kas3 {

void Typer::shown(Expr *p_expression) {
	if (p_expression) {
		unit_.typed.push_back({ p_expression->offset, expression(p_expression) });
	}
}

void Typer::declarators(VarDecl *p_decl) {
	for (Declarator &declarator : p_decl->declarators) {
		shown(declarator.initializer);
		store(declarator.initializer, compilation_.type_of(declarator.type));
	}
}

void Typer::statement(Stmt *p_statement) {
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
			shown(static_cast<ExprStmt *>(p_statement)->expr);
			break;
		case NodeKind::VarStmt:
			declarators(static_cast<VarStmt *>(p_statement)->decl);
			break;
		case NodeKind::If: {
			If *branch = static_cast<If *>(p_statement);
			expression(branch->condition);
			statement(branch->then_branch);
			statement(branch->else_branch);
		} break;
		case NodeKind::While:
			expression(static_cast<While *>(p_statement)->condition);
			statement(static_cast<While *>(p_statement)->body);
			break;
		case NodeKind::DoWhile:
			statement(static_cast<DoWhile *>(p_statement)->body);
			expression(static_cast<DoWhile *>(p_statement)->condition);
			break;
		case NodeKind::For: {
			For *loop = static_cast<For *>(p_statement);
			statement(loop->init);
			expression(loop->condition);
			expression(loop->update);
			statement(loop->body);
		} break;
		case NodeKind::ForIn: {
			ForIn *loop = static_cast<ForIn *>(p_statement);
			expression(loop->target);
			expression(loop->collection);
			statement(loop->body);
		} break;
		case NodeKind::Switch: {
			Switch *choice = static_cast<Switch *>(p_statement);
			expression(choice->subject);
			for (SwitchCase &branch : choice->cases) {
				expression(branch.test);
				for (Stmt *inner : branch.body) {
					statement(inner);
				}
			}
		} break;
		case NodeKind::Return:
			returned(static_cast<Return *>(p_statement));
			break;
		case NodeKind::Throw:
			expression(static_cast<Throw *>(p_statement)->value);
			leaves_ = true;
			break;
		case NodeKind::Try: {
			Try *attempt = static_cast<Try *>(p_statement);
			statement(attempt->body);
			for (CatchClause &clause : attempt->catches) {
				statement(clause.body);
			}
			statement(attempt->finally_block);
		} break;
		case NodeKind::Labeled:
			statement(static_cast<Labeled *>(p_statement)->body);
			break;
		case NodeKind::FunctionStmt:
			closures_++;
			function(static_cast<FunctionStmt *>(p_statement)->function);
			closures_--;
			break;
		default:
			break;
	}
}

void Typer::returned(Return *p_return) {
	const Type *value = expression(p_return->value);
	if (!p_return->value || !current_) {
		return;
	}
	leaves_ = true;
	bool constructor = class_ && class_->klass->constructor && class_->klass->constructor->node == current_;
	TypeRef *declared = current_->return_type;
	if (constructor) {
		error(p_return->value->offset, "a constructor can't return a value");
	} else if (declared && declared->form == TypeRef::Void) {
		// Passing on the result of another function that returns nothing is left alone, and so is a
		// value that may be `undefined`.
		if (value->kind != Type::Void && value->kind != Type::Unknown && value->kind != Type::Any) {
			error(p_return->value->offset, "a function declared 'void' can't return a value");
		}
	} else {
		store(p_return->value, compilation_.type_of(declared));
	}
}

void Typer::function(FunctionDecl *p_function) {
	for (Parameter &parameter : p_function->parameters) {
		expression(parameter.default_value);
		if (!parameter.is_rest) {
			store(parameter.default_value, compilation_.type_of(parameter.type));
		}
	}
	FunctionDecl *outer = current_;
	bool outer_leaves = leaves_;
	current_ = p_function;
	leaves_ = false;
	statement(p_function->body);
	// Paths are not followed: only a body with no `return <value>` and no `throw` anywhere is reported.
	TypeRef *declared = p_function->return_type;
	bool constructor = class_ && class_->klass->constructor && class_->klass->constructor->node == p_function;
	if (p_function->body && !leaves_ && !constructor && declared && declared->form != TypeRef::Void &&
			declared->form != TypeRef::Any) {
		const Type *type = compilation_.type_of(declared);
		if (type->kind != Type::Unknown) {
			bool named = !p_function->name.empty();
			error(named ? p_function->name_offset : p_function->offset,
					(named ? quoted(p_function->name) : std::string("this function")) + " is declared to return " +
							quoted(compilation_.type_name(type)) + " but never returns a value");
		}
	}
	current_ = outer;
	leaves_ = outer_leaves;
}

void Typer::definition(Node *p_node) {
	switch (p_node->kind) {
		case NodeKind::ClassDecl: {
			ClassDecl *decl = static_cast<ClassDecl *>(p_node);
			class_ = decl->symbol;
			for (Node *member : decl->members) {
				if (member->kind == NodeKind::VarDecl) {
					static_ = (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static) != 0;
					declarators(static_cast<VarDecl *>(member));
				} else if (member->kind == NodeKind::FunctionDecl) {
					static_ = (static_cast<FunctionDecl *>(member)->modifiers.bits & Modifiers::Static) != 0;
					function(static_cast<FunctionDecl *>(member));
				}
			}
			class_ = nullptr;
			static_ = false;
		} break;
		case NodeKind::InterfaceDecl:
			for (FunctionDecl *method : static_cast<InterfaceDecl *>(p_node)->methods) {
				function(method);
			}
			break;
		case NodeKind::FunctionDecl:
			function(static_cast<FunctionDecl *>(p_node));
			break;
		case NodeKind::VarDecl:
			declarators(static_cast<VarDecl *>(p_node));
			break;
		default:
			break;
	}
}

const Type *Typer::builtin(const char *p_name) {
	Symbol *symbol = compilation_.builtin(p_name);
	return symbol ? compilation_.instance_type(symbol) : compilation_.unknown_type();
}

Typer::Typer(Compilation &p_compilation, Unit &p_unit) :
		compilation_(p_compilation), unit_(p_unit) {
	any_ = compilation_.any_type();
	unknown_ = compilation_.unknown_type();
	int_ = builtin("int");
	uint_ = builtin("uint");
	number_ = builtin("Number");
	int64_ = builtin("Int64");
	boolean_ = builtin("Boolean");
	string_ = builtin("String");
	array_ = builtin("Array");
	object_ = builtin("Object");
	function_ = builtin("Function");
	regexp_ = builtin("RegExp");
	class_type_ = builtin("Class");
	godot_ = compilation_.find_package("godot");
	if (Package *utils = compilation_.find_package("flash.utils")) {
		auto found = utils->definitions.find("Dictionary");
		if (found != utils->definitions.end()) {
			dictionary_ = compilation_.instance_type(found->second);
		}
	}
}

void Typer::run() {
	Program *tree = unit_.tree;
	if (tree->package) {
		for (Node *node : tree->package->definitions) {
			definition(node);
		}
	}
	for (Node *node : tree->definitions) {
		definition(node);
	}
}

void type_bodies(Compilation &p_compilation) {
	for (Unit &unit : p_compilation.units()) {
		Typer(p_compilation, unit).run();
	}
}

} // namespace kas3
