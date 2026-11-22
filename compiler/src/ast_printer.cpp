#include "ast_printer.h"
#include "ast_printer_class.h"

namespace kas3 {

void Printer::node(const Node *p_node, int p_depth) {
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
		case NodeKind::ArrayLit:
			print_array_lit(p_node, p_depth);
			break;
		case NodeKind::ObjectLit:
			print_object_lit(p_node, p_depth);
			break;
		case NodeKind::VectorLit: {
			const VectorLit *vector = static_cast<const VectorLit *>(p_node);
			head(vector->fixed_array ? "FixedArray" : "VectorLit", p_depth);
			at(p_node->offset);
			node(vector->element_type, p_depth + 1);
			node(vector->length, p_depth + 1);
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
		case NodeKind::New:
			print_new(p_node, p_depth);
			break;
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
		case NodeKind::Binary:
			print_binary(p_node, p_depth);
			break;
		case NodeKind::Assign:
			print_assign(p_node, p_depth);
			break;
		case NodeKind::Conditional:
			print_conditional(p_node, p_depth);
			break;
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
		case NodeKind::If:
			print_if(p_node, p_depth);
			break;
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
		case NodeKind::For:
			print_for(p_node, p_depth);
			break;
		case NodeKind::ForIn:
			print_for_in(p_node, p_depth);
			break;
		case NodeKind::Switch:
			print_switch(p_node, p_depth);
			break;
		case NodeKind::Break:
			print_break(p_node, p_depth);
			break;
		case NodeKind::Continue:
			print_continue(p_node, p_depth);
			break;
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
		case NodeKind::Try:
			print_try(p_node, p_depth);
			break;
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
		case NodeKind::ClassDecl:
			print_class_decl(p_node, p_depth);
			break;
		case NodeKind::InterfaceDecl:
			print_interface_decl(p_node, p_depth);
			break;
		case NodeKind::Import: {
			const Import *import = static_cast<const Import *>(p_node);
			head("Import", p_depth);
			dotted_attribute("name", import->name);
			flag(import->wildcard, "wildcard");
			at(p_node->offset);
		} break;
		case NodeKind::PackageDecl:
			print_package_decl(p_node, p_depth);
			break;
		case NodeKind::Program:
			print_program(p_node, p_depth);
			break;
	}
}

std::string print_node(const Node *p_node, const Source &p_source) {
	Printer printer(p_source);
	printer.node(p_node, 0);
	return printer.take();
}

std::string print_ast(const Program *p_program, const Source &p_source) {
	return print_node(p_program, p_source);
}

} // namespace kas3
