#include "ast_printer.h"

namespace kas3 {

namespace {

class Printer {
	const Source &source_;
	std::string out_;

	void indent(int p_depth) { out_.append((size_t)p_depth * 2, ' '); }

	void quoted(std::string_view p_text) {
		out_ += '"';
		for (char c : p_text) {
			if (c == '"' || c == '\\') {
				out_ += '\\';
			}
			out_ += c;
		}
		out_ += '"';
	}

	void attribute(const char *p_name, std::string_view p_text) {
		out_ += ' ';
		out_ += p_name;
		out_ += '=';
		quoted(p_text);
	}

	void flag(bool p_set, const char *p_name) {
		if (p_set) {
			out_ += ' ';
			out_ += p_name;
		}
	}

	void dotted(const List<std::string_view> &p_parts) {
		for (uint32_t i = 0; i < p_parts.count; i++) {
			if (i) {
				out_ += '.';
			}
			out_ += p_parts[i];
		}
	}

	void dotted_attribute(const char *p_name, const List<std::string_view> &p_parts) {
		out_ += ' ';
		out_ += p_name;
		out_ += "=\"";
		dotted(p_parts);
		out_ += '"';
	}

	void modifiers(Modifiers p_modifiers) {
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

	void at(uint32_t p_offset) {
		Position position = source_.position(p_offset);
		out_ += " @";
		out_ += std::to_string(position.line);
		out_ += ':';
		out_ += std::to_string(position.column);
		out_ += '\n';
	}

	void label(const char *p_text, int p_depth) {
		indent(p_depth);
		out_ += p_text;
		out_ += '\n';
	}

	void type_text(const TypeRef *p_type) {
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

	void metadata(const List<Metadata> &p_list, int p_depth) {
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

	template <class T>
	void children(const List<T *> &p_list, int p_depth) {
		for (T *item : p_list) {
			node(item, p_depth);
		}
	}

	void labelled(const char *p_label, const Node *p_node, int p_depth) {
		if (p_node) {
			label(p_label, p_depth);
			node(p_node, p_depth + 1);
		}
	}

	void head(const char *p_kind, int p_depth) {
		indent(p_depth);
		out_ += p_kind;
	}

	void function(const FunctionDecl *p_function, int p_depth) {
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

	void variable(const VarDecl *p_decl, int p_depth) {
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

public:
	explicit Printer(const Source &p_source) :
			source_(p_source) {}

	std::string take() { return std::move(out_); }

	void node(const Node *p_node, int p_depth) {
		if (!p_node) {
			return;
		}
		switch (p_node->kind) {
			case NodeKind::TypeRef:
				head("Type ", p_depth);
				type_text(static_cast<const TypeRef *>(p_node));
				at(p_node->offset);
				break;
			case NodeKind::NumberLit:
				head("NumberLit", p_depth);
				attribute("text", static_cast<const NumberLit *>(p_node)->text);
				at(p_node->offset);
				break;
			case NodeKind::StringLit:
				head("StringLit", p_depth);
				attribute("text", static_cast<const StringLit *>(p_node)->text);
				at(p_node->offset);
				break;
			case NodeKind::RegexLit:
				head("RegexLit", p_depth);
				attribute("text", static_cast<const RegexLit *>(p_node)->text);
				at(p_node->offset);
				break;
			case NodeKind::BoolLit:
				head(static_cast<const BoolLit *>(p_node)->value ? "BoolLit true" : "BoolLit false", p_depth);
				at(p_node->offset);
				break;
			case NodeKind::NullLit:
				head("NullLit", p_depth);
				at(p_node->offset);
				break;
			case NodeKind::ThisExpr:
				head("ThisExpr", p_depth);
				at(p_node->offset);
				break;
			case NodeKind::SuperExpr:
				head("SuperExpr", p_depth);
				at(p_node->offset);
				break;
			case NodeKind::Name:
				head("Name", p_depth);
				attribute("text", static_cast<const Name *>(p_node)->text);
				at(p_node->offset);
				break;
			case NodeKind::ArrayLit: {
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
			} break;
			case NodeKind::ObjectLit: {
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
			} break;
			case NodeKind::VectorLit: {
				const VectorLit *vector = static_cast<const VectorLit *>(p_node);
				head("VectorLit", p_depth);
				at(p_node->offset);
				node(vector->element_type, p_depth + 1);
				children(vector->elements, p_depth + 1);
			} break;
			case NodeKind::FunctionExpr:
				head("FunctionExpr", p_depth);
				at(p_node->offset);
				node(static_cast<const FunctionExpr *>(p_node)->function, p_depth + 1);
				break;
			case NodeKind::Member: {
				const Member *member = static_cast<const Member *>(p_node);
				head("Member", p_depth);
				attribute("name", member->name);
				at(p_node->offset);
				node(member->object, p_depth + 1);
			} break;
			case NodeKind::Index: {
				const Index *index = static_cast<const Index *>(p_node);
				head("Index", p_depth);
				at(p_node->offset);
				node(index->object, p_depth + 1);
				node(index->index, p_depth + 1);
			} break;
			case NodeKind::TypeApply: {
				const TypeApply *apply = static_cast<const TypeApply *>(p_node);
				head("TypeApply", p_depth);
				at(p_node->offset);
				node(apply->base, p_depth + 1);
				node(apply->argument, p_depth + 1);
			} break;
			case NodeKind::Call: {
				const Call *call = static_cast<const Call *>(p_node);
				head("Call", p_depth);
				at(p_node->offset);
				node(call->callee, p_depth + 1);
				children(call->arguments, p_depth + 1);
			} break;
			case NodeKind::New: {
				const New *create = static_cast<const New *>(p_node);
				head("New", p_depth);
				flag(create->has_arguments, "has_arguments");
				at(p_node->offset);
				node(create->callee, p_depth + 1);
				children(create->arguments, p_depth + 1);
			} break;
			case NodeKind::Unary: {
				const Unary *unary = static_cast<const Unary *>(p_node);
				head("Unary", p_depth);
				attribute("op", token_kind_spelling(unary->op));
				at(p_node->offset);
				node(unary->operand, p_depth + 1);
			} break;
			case NodeKind::Postfix: {
				const Postfix *postfix = static_cast<const Postfix *>(p_node);
				head("Postfix", p_depth);
				attribute("op", token_kind_spelling(postfix->op));
				at(p_node->offset);
				node(postfix->operand, p_depth + 1);
			} break;
			case NodeKind::Binary: {
				const Binary *binary = static_cast<const Binary *>(p_node);
				head("Binary", p_depth);
				attribute("op", token_kind_spelling(binary->op));
				at(p_node->offset);
				node(binary->left, p_depth + 1);
				node(binary->right, p_depth + 1);
			} break;
			case NodeKind::Assign: {
				const Assign *assign = static_cast<const Assign *>(p_node);
				head("Assign", p_depth);
				attribute("op", token_kind_spelling(assign->op));
				at(p_node->offset);
				node(assign->target, p_depth + 1);
				node(assign->value, p_depth + 1);
			} break;
			case NodeKind::Conditional: {
				const Conditional *conditional = static_cast<const Conditional *>(p_node);
				head("Conditional", p_depth);
				at(p_node->offset);
				node(conditional->condition, p_depth + 1);
				node(conditional->then_value, p_depth + 1);
				node(conditional->else_value, p_depth + 1);
			} break;
			case NodeKind::Block:
				head("Block", p_depth);
				at(p_node->offset);
				children(static_cast<const Block *>(p_node)->statements, p_depth + 1);
				break;
			case NodeKind::ExprStmt:
				head("ExprStmt", p_depth);
				at(p_node->offset);
				node(static_cast<const ExprStmt *>(p_node)->expr, p_depth + 1);
				break;
			case NodeKind::Empty:
				head("Empty", p_depth);
				at(p_node->offset);
				break;
			case NodeKind::VarStmt:
				variable(static_cast<const VarStmt *>(p_node)->decl, p_depth);
				break;
			case NodeKind::If: {
				const If *branch = static_cast<const If *>(p_node);
				head("If", p_depth);
				at(p_node->offset);
				node(branch->condition, p_depth + 1);
				node(branch->then_branch, p_depth + 1);
				labelled("else:", branch->else_branch, p_depth + 1);
			} break;
			case NodeKind::While: {
				const While *loop = static_cast<const While *>(p_node);
				head("While", p_depth);
				at(p_node->offset);
				node(loop->condition, p_depth + 1);
				node(loop->body, p_depth + 1);
			} break;
			case NodeKind::DoWhile: {
				const DoWhile *loop = static_cast<const DoWhile *>(p_node);
				head("DoWhile", p_depth);
				at(p_node->offset);
				node(loop->body, p_depth + 1);
				node(loop->condition, p_depth + 1);
			} break;
			case NodeKind::For: {
				const For *loop = static_cast<const For *>(p_node);
				head("For", p_depth);
				at(p_node->offset);
				labelled("init:", loop->init, p_depth + 1);
				labelled("condition:", loop->condition, p_depth + 1);
				labelled("update:", loop->update, p_depth + 1);
				node(loop->body, p_depth + 1);
			} break;
			case NodeKind::ForIn: {
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
			} break;
			case NodeKind::Switch: {
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
			} break;
			case NodeKind::Break: {
				head("Break", p_depth);
				std::string_view name = static_cast<const Break *>(p_node)->label;
				if (!name.empty()) {
					attribute("label", name);
				}
				at(p_node->offset);
			} break;
			case NodeKind::Continue: {
				head("Continue", p_depth);
				std::string_view name = static_cast<const Continue *>(p_node)->label;
				if (!name.empty()) {
					attribute("label", name);
				}
				at(p_node->offset);
			} break;
			case NodeKind::Return:
				head("Return", p_depth);
				at(p_node->offset);
				node(static_cast<const Return *>(p_node)->value, p_depth + 1);
				break;
			case NodeKind::Throw:
				head("Throw", p_depth);
				at(p_node->offset);
				node(static_cast<const Throw *>(p_node)->value, p_depth + 1);
				break;
			case NodeKind::Try: {
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
			} break;
			case NodeKind::Labeled: {
				const Labeled *labeled = static_cast<const Labeled *>(p_node);
				head("Labeled", p_depth);
				attribute("label", labeled->label);
				at(p_node->offset);
				node(labeled->body, p_depth + 1);
			} break;
			case NodeKind::FunctionStmt:
				head("FunctionStmt", p_depth);
				at(p_node->offset);
				node(static_cast<const FunctionStmt *>(p_node)->function, p_depth + 1);
				break;
			case NodeKind::ImportStmt:
				node(static_cast<const ImportStmt *>(p_node)->import, p_depth);
				break;
			case NodeKind::VarDecl:
				variable(static_cast<const VarDecl *>(p_node), p_depth);
				break;
			case NodeKind::FunctionDecl:
				function(static_cast<const FunctionDecl *>(p_node), p_depth);
				break;
			case NodeKind::ClassDecl: {
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
			} break;
			case NodeKind::InterfaceDecl: {
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
			} break;
			case NodeKind::Import: {
				const Import *import = static_cast<const Import *>(p_node);
				head("Import", p_depth);
				dotted_attribute("name", import->name);
				flag(import->wildcard, "wildcard");
				at(p_node->offset);
			} break;
			case NodeKind::PackageDecl: {
				const PackageDecl *package = static_cast<const PackageDecl *>(p_node);
				head("PackageDecl", p_depth);
				if (package->name.count) {
					dotted_attribute("name", package->name);
				}
				at(p_node->offset);
				children(package->imports, p_depth + 1);
				children(package->definitions, p_depth + 1);
			} break;
			case NodeKind::Program: {
				const Program *program = static_cast<const Program *>(p_node);
				head("Program", p_depth);
				at(p_node->offset);
				node(program->package, p_depth + 1);
				children(program->imports, p_depth + 1);
				children(program->definitions, p_depth + 1);
			} break;
		}
	}
};

} // namespace

std::string print_node(const Node *p_node, const Source &p_source) {
	Printer printer(p_source);
	printer.node(p_node, 0);
	return printer.take();
}

std::string print_ast(const Program *p_program, const Source &p_source) {
	return print_node(p_program, p_source);
}

} // namespace kas3
