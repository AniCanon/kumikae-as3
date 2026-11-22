#include "ast_printer_class.h"

namespace kas3 {

void Printer::quoted(std::string_view p_text) {
	out_ += '"';
	for (char c : p_text) {
		if (c == '"' || c == '\\') {
			out_ += '\\';
		}
		out_ += c;
	}
	out_ += '"';
}

void Printer::attribute(const char *p_name, std::string_view p_text) {
	out_ += ' ';
	out_ += p_name;
	out_ += '=';
	quoted(p_text);
}

void Printer::flag(bool p_set, const char *p_name) {
	if (p_set) {
		out_ += ' ';
		out_ += p_name;
	}
}

void Printer::dotted(const List<std::string_view> &p_parts) {
	for (uint32_t i = 0; i < p_parts.count; i++) {
		if (i) {
			out_ += '.';
		}
		out_ += p_parts[i];
	}
}

void Printer::dotted_attribute(const char *p_name, const List<std::string_view> &p_parts) {
	out_ += ' ';
	out_ += p_name;
	out_ += "=\"";
	dotted(p_parts);
	out_ += '"';
}

void Printer::modifiers(Modifiers p_modifiers) {
	static const char *const NAMES[] = { "public", "private", "protected", "internal", "static",
		"final", "override", "dynamic", "native" };
	bool first = true;
	for (int bit = 0; bit < 9; bit++) {
		if (p_modifiers.bits & (1 << bit)) {
			out_ += first ? " mods=" : ",";
			out_ += NAMES[bit];
			first = false;
		}
	}
}

void Printer::at(uint32_t p_offset) {
	Position position = source_.position(p_offset);
	out_ += " @";
	out_ += std::to_string(position.line);
	out_ += ':';
	out_ += std::to_string(position.column);
	out_ += '\n';
}

void Printer::label(const char *p_text, int p_depth) {
	indent(p_depth);
	out_ += p_text;
	out_ += '\n';
}

void Printer::type_text(const TypeRef *p_type) {
	switch (p_type->form) {
		case TypeRef::Any:
			out_ += '*';
			break;
		case TypeRef::Void:
			out_ += "void";
			break;
		case TypeRef::Named:
			dotted(p_type->name);
			break;
		case TypeRef::Callback:
			out_ += '(';
			for (uint32_t i = 0; i < p_type->parameters.count; ++i) {
				if (i) {
					out_ += ", ";
				}
				type_text(p_type->parameters[i]);
			}
			out_ += ")=>";
			type_text(p_type->argument);
			break;
		case TypeRef::Applied:
			dotted(p_type->name);
			out_ += ".<";
			if (p_type->argument) {
				type_text(p_type->argument);
			}
			out_ += '>';
			break;
	}
}

void Printer::metadata(const List<Metadata> &p_list, int p_depth) {
	for (const Metadata &tag : p_list) {
		indent(p_depth);
		out_ += "Metadata";
		attribute("name", tag.name);
		if (tag.args.count) {
			out_ += " args=[";
			for (uint32_t i = 0; i < tag.args.count; i++) {
				if (i) {
					out_ += ", ";
				}
				if (!tag.args[i].key.empty()) {
					out_ += tag.args[i].key;
					out_ += '=';
				}
				quoted(tag.args[i].value);
			}
			out_ += ']';
		}
		at(tag.offset);
	}
}

void Printer::labelled(const char *p_label, const Node *p_node, int p_depth) {
	if (p_node) {
		label(p_label, p_depth);
		node(p_node, p_depth + 1);
	}
}

void Printer::head(const char *p_kind, int p_depth) {
	indent(p_depth);
	out_ += p_kind;
}

void Printer::function(const FunctionDecl *p_function, int p_depth) {
	head("FunctionDecl", p_depth);
	if (!p_function->name.empty()) {
		attribute("name", p_function->name);
	}
	flag(p_function->accessor == FunctionDecl::Getter, "getter");
	flag(p_function->accessor == FunctionDecl::Setter, "setter");
	modifiers(p_function->modifiers);
	at(p_function->offset);
	metadata(p_function->metadata, p_depth + 1);
	for (const Parameter &parameter : p_function->parameters) {
		head("Parameter", p_depth + 1);
		attribute("name", parameter.name);
		flag(parameter.is_rest, "rest");
		at(parameter.offset);
		node(parameter.type, p_depth + 2);
		node(parameter.default_value, p_depth + 2);
	}
	labelled("returns:", p_function->return_type, p_depth + 1);
	node(p_function->body, p_depth + 1);
}

void Printer::variable(const VarDecl *p_decl, int p_depth) {
	head("VarDecl", p_depth);
	flag(p_decl->is_const, "const");
	modifiers(p_decl->modifiers);
	at(p_decl->offset);
	metadata(p_decl->metadata, p_depth + 1);
	for (const Declarator &declarator : p_decl->declarators) {
		head("Declarator", p_depth + 1);
		attribute("name", declarator.name);
		at(declarator.offset);
		node(declarator.type, p_depth + 2);
		node(declarator.initializer, p_depth + 2);
	}
}

void Printer::print_array_lit(const Node *p_node, int p_depth) {
	const ArrayLit *array = static_cast<const ArrayLit *>(p_node);
	head("ArrayLit", p_depth);
	at(p_node->offset);
	for (Expr *element : array->elements) {
		if (element) {
			node(element, p_depth + 1);
		} else {
			label("hole", p_depth + 1);
		}
	}
}

void Printer::print_object_lit(const Node *p_node, int p_depth) {
	const ObjectLit *object = static_cast<const ObjectLit *>(p_node);
	head("ObjectLit", p_depth);
	at(p_node->offset);
	for (const ObjectField &field : object->fields) {
		head("Field", p_depth + 1);
		attribute("key", field.key);
		flag(field.key_is_string, "string");
		at(field.offset);
		node(field.value, p_depth + 2);
	}
}

void Printer::print_new(const Node *p_node, int p_depth) {
	const New *create = static_cast<const New *>(p_node);
	head("New", p_depth);
	flag(create->has_arguments, "has_arguments");
	at(p_node->offset);
	node(create->callee, p_depth + 1);
	children(create->arguments, p_depth + 1);
}

void Printer::print_binary(const Node *p_node, int p_depth) {
	const Binary *binary = static_cast<const Binary *>(p_node);
	head("Binary", p_depth);
	attribute("op", token_kind_spelling(binary->op));
	at(p_node->offset);
	node(binary->left, p_depth + 1);
	node(binary->right, p_depth + 1);
}

void Printer::print_assign(const Node *p_node, int p_depth) {
	const Assign *assign = static_cast<const Assign *>(p_node);
	head("Assign", p_depth);
	attribute("op", token_kind_spelling(assign->op));
	at(p_node->offset);
	node(assign->target, p_depth + 1);
	node(assign->value, p_depth + 1);
}

void Printer::print_conditional(const Node *p_node, int p_depth) {
	const Conditional *conditional = static_cast<const Conditional *>(p_node);
	head("Conditional", p_depth);
	at(p_node->offset);
	node(conditional->condition, p_depth + 1);
	node(conditional->then_value, p_depth + 1);
	node(conditional->else_value, p_depth + 1);
}

void Printer::print_if(const Node *p_node, int p_depth) {
	const If *branch = static_cast<const If *>(p_node);
	head("If", p_depth);
	at(p_node->offset);
	node(branch->condition, p_depth + 1);
	node(branch->then_branch, p_depth + 1);
	labelled("else:", branch->else_branch, p_depth + 1);
}

void Printer::print_for(const Node *p_node, int p_depth) {
	const For *loop = static_cast<const For *>(p_node);
	head("For", p_depth);
	at(p_node->offset);
	labelled("init:", loop->init, p_depth + 1);
	labelled("condition:", loop->condition, p_depth + 1);
	labelled("update:", loop->update, p_depth + 1);
	node(loop->body, p_depth + 1);
}

void Printer::print_for_in(const Node *p_node, int p_depth) {
	const ForIn *loop = static_cast<const ForIn *>(p_node);
	head("ForIn", p_depth);
	flag(loop->each, "each");
	at(p_node->offset);
	if (loop->declaration) {
		variable(loop->declaration, p_depth + 1);
	}
	node(loop->target, p_depth + 1);
	node(loop->collection, p_depth + 1);
	node(loop->body, p_depth + 1);
}

void Printer::print_switch(const Node *p_node, int p_depth) {
	const Switch *choice = static_cast<const Switch *>(p_node);
	head("Switch", p_depth);
	at(p_node->offset);
	node(choice->subject, p_depth + 1);
	for (const SwitchCase &clause : choice->cases) {
		head(clause.is_default ? "Default" : "Case", p_depth + 1);
		at(clause.offset);
		node(clause.test, p_depth + 2);
		children(clause.body, p_depth + 2);
	}
}

void Printer::print_break(const Node *p_node, int p_depth) {
	head("Break", p_depth);
	std::string_view name = static_cast<const Break *>(p_node)->label;
	if (!name.empty()) {
		attribute("label", name);
	}
	at(p_node->offset);
}

void Printer::print_continue(const Node *p_node, int p_depth) {
	head("Continue", p_depth);
	std::string_view name = static_cast<const Continue *>(p_node)->label;
	if (!name.empty()) {
		attribute("label", name);
	}
	at(p_node->offset);
}

void Printer::print_try(const Node *p_node, int p_depth) {
	const Try *attempt = static_cast<const Try *>(p_node);
	head("Try", p_depth);
	at(p_node->offset);
	node(attempt->body, p_depth + 1);
	for (const CatchClause &clause : attempt->catches) {
		head("Catch", p_depth + 1);
		attribute("name", clause.name);
		at(clause.offset);
		node(clause.type, p_depth + 2);
		node(clause.body, p_depth + 2);
	}
	labelled("finally:", attempt->finally_block, p_depth + 1);
}

void Printer::print_class_decl(const Node *p_node, int p_depth) {
	const ClassDecl *decl = static_cast<const ClassDecl *>(p_node);
	head("ClassDecl", p_depth);
	attribute("name", decl->name);
	modifiers(decl->modifiers);
	at(p_node->offset);
	metadata(decl->metadata, p_depth + 1);
	labelled("extends:", decl->extends, p_depth + 1);
	if (decl->implements.count) {
		label("implements:", p_depth + 1);
		children(decl->implements, p_depth + 2);
	}
	children(decl->members, p_depth + 1);
}

void Printer::print_interface_decl(const Node *p_node, int p_depth) {
	const InterfaceDecl *decl = static_cast<const InterfaceDecl *>(p_node);
	head("InterfaceDecl", p_depth);
	attribute("name", decl->name);
	modifiers(decl->modifiers);
	at(p_node->offset);
	metadata(decl->metadata, p_depth + 1);
	if (decl->extends.count) {
		label("extends:", p_depth + 1);
		children(decl->extends, p_depth + 2);
	}
	children(decl->methods, p_depth + 1);
}

void Printer::print_package_decl(const Node *p_node, int p_depth) {
	const PackageDecl *package = static_cast<const PackageDecl *>(p_node);
	head("PackageDecl", p_depth);
	if (package->name.count) {
		dotted_attribute("name", package->name);
	}
	at(p_node->offset);
	children(package->imports, p_depth + 1);
	children(package->definitions, p_depth + 1);
}

void Printer::print_program(const Node *p_node, int p_depth) {
	const Program *program = static_cast<const Program *>(p_node);
	head("Program", p_depth);
	at(p_node->offset);
	node(program->package, p_depth + 1);
	children(program->imports, p_depth + 1);
	children(program->definitions, p_depth + 1);
}

} // namespace kas3
