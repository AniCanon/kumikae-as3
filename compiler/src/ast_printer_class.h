// The class that writes the syntax tree as text, for ast_printer.cpp (the dispatch on each kind of node)
// and ast_printer_parts.cpp (the parts each kind is written from). Nothing else includes this.
#pragma once

#include "ast.h"
#include "source.h"

#include <string>
#include <string_view>

namespace kas3 {

class Printer {
	const Source &source_;
	std::string out_;

	void indent(int p_depth) { out_.append((size_t)p_depth * 2, ' '); }

	void quoted(std::string_view p_text);
	void attribute(const char *p_name, std::string_view p_text);
	void flag(bool p_set, const char *p_name);
	void dotted(const List<std::string_view> &p_parts);
	void dotted_attribute(const char *p_name, const List<std::string_view> &p_parts);
	void modifiers(Modifiers p_modifiers);
	void at(uint32_t p_offset);
	void label(const char *p_text, int p_depth);
	void type_text(const TypeRef *p_type);
	void metadata(const List<Metadata> &p_list, int p_depth);

	template <class T>
	void children(const List<T *> &p_list, int p_depth) {
		for (T *item : p_list) {
			node(item, p_depth);
		}
	}

	void labelled(const char *p_label, const Node *p_node, int p_depth);
	void head(const char *p_kind, int p_depth);
	void function(const FunctionDecl *p_function, int p_depth);
	void variable(const VarDecl *p_decl, int p_depth);
	void print_array_lit(const Node *p_node, int p_depth);
	void print_object_lit(const Node *p_node, int p_depth);
	void print_new(const Node *p_node, int p_depth);
	void print_binary(const Node *p_node, int p_depth);
	void print_assign(const Node *p_node, int p_depth);
	void print_conditional(const Node *p_node, int p_depth);
	void print_if(const Node *p_node, int p_depth);
	void print_for(const Node *p_node, int p_depth);
	void print_for_in(const Node *p_node, int p_depth);
	void print_switch(const Node *p_node, int p_depth);
	void print_break(const Node *p_node, int p_depth);
	void print_continue(const Node *p_node, int p_depth);
	void print_try(const Node *p_node, int p_depth);
	void print_class_decl(const Node *p_node, int p_depth);
	void print_interface_decl(const Node *p_node, int p_depth);
	void print_package_decl(const Node *p_node, int p_depth);
	void print_program(const Node *p_node, int p_depth);

public:
	explicit Printer(const Source &p_source) :
			source_(p_source) {}

	std::string take() { return std::move(out_); }

	void node(const Node *p_node, int p_depth);
};

} // namespace kas3
