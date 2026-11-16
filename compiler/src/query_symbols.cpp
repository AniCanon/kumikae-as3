// The symbols an editor lists: a file's outline, and the project's definitions whose name contains a query.
#include "query.h"

#include <algorithm>

namespace kas3 {

namespace {

void add_outline(std::vector<OutlineItem> &r_items, OutlineItem p_item) {
	if (p_item.name_length == 0) {
		return; // A declaration whose name is still to be typed.
	}
	p_item.offset = std::min(p_item.offset, p_item.name_offset);
	p_item.end = std::max(p_item.end, p_item.name_offset + p_item.name_length);
	r_items.push_back(std::move(p_item));
}

void outline_function(std::vector<OutlineItem> &r_items, const FunctionDecl *p_function, SymbolKind p_kind,
		std::string_view p_class) {
	OutlineItem item;
	item.name = std::string(p_function->name);
	item.kind = p_kind;
	if (p_function->accessor != FunctionDecl::None) {
		item.kind = SymbolKind::Property;
		item.name = (p_function->accessor == FunctionDecl::Getter ? "get " : "set ") + item.name;
	} else {
		item.is_constructor = !p_class.empty() && p_function->name == p_class;
	}
	item.offset = p_function->offset;
	item.end = p_function->end;
	item.name_offset = p_function->name_offset;
	item.name_length = (uint32_t)p_function->name.size();
	add_outline(r_items, std::move(item));
}

void outline_variables(std::vector<OutlineItem> &r_items, const VarDecl *p_decl, SymbolKind p_kind) {
	for (const Declarator &declarator : p_decl->declarators) {
		OutlineItem item;
		item.name = std::string(declarator.name);
		item.kind = p_kind;
		item.is_const = p_decl->is_const;
		item.offset = declarator.offset;
		item.end = declarator.end;
		item.name_offset = declarator.offset;
		item.name_length = (uint32_t)declarator.name.size();
		add_outline(r_items, std::move(item));
	}
}

void outline_definition(std::vector<OutlineItem> &r_items, const Node *p_node) {
	switch (p_node->kind) {
		case NodeKind::ClassDecl: {
			const ClassDecl *decl = static_cast<const ClassDecl *>(p_node);
			OutlineItem item;
			item.name = std::string(decl->name);
			item.kind = SymbolKind::Class;
			item.offset = decl->offset;
			item.end = decl->end;
			item.name_offset = decl->name_offset;
			item.name_length = (uint32_t)decl->name.size();
			for (const Node *member : decl->members) {
				if (member->kind == NodeKind::VarDecl) {
					outline_variables(item.children, static_cast<const VarDecl *>(member), SymbolKind::Field);
				} else if (member->kind == NodeKind::FunctionDecl) {
					outline_function(item.children, static_cast<const FunctionDecl *>(member), SymbolKind::Method, decl->name);
				}
			}
			add_outline(r_items, std::move(item));
			break;
		}
		case NodeKind::InterfaceDecl: {
			const InterfaceDecl *decl = static_cast<const InterfaceDecl *>(p_node);
			OutlineItem item;
			item.name = std::string(decl->name);
			item.kind = SymbolKind::Interface;
			item.offset = decl->offset;
			item.end = decl->end;
			item.name_offset = decl->name_offset;
			item.name_length = (uint32_t)decl->name.size();
			for (const FunctionDecl *method : decl->methods) {
				outline_function(item.children, method, SymbolKind::Method, std::string_view());
			}
			add_outline(r_items, std::move(item));
			break;
		}
		case NodeKind::FunctionDecl:
			outline_function(r_items, static_cast<const FunctionDecl *>(p_node), SymbolKind::Function, std::string_view());
			break;
		case NodeKind::VarDecl:
			outline_variables(r_items, static_cast<const VarDecl *>(p_node), SymbolKind::Variable);
			break;
		default:
			break;
	}
}

bool contains_ignoring_case(std::string_view p_text, std::string_view p_query) {
	if (p_query.empty()) {
		return true;
	}
	auto lower = [](char p_c) { return p_c >= 'A' && p_c <= 'Z' ? (char)(p_c - 'A' + 'a') : p_c; };
	for (size_t start = 0; start + p_query.size() <= p_text.size(); start++) {
		size_t i = 0;
		while (i < p_query.size() && lower(p_text[start + i]) == lower(p_query[i])) {
			i++;
		}
		if (i == p_query.size()) {
			return true;
		}
	}
	return false;
}

} // namespace

std::vector<OutlineItem> Queries::outline(const Unit &p_unit) const {
	std::vector<OutlineItem> items;
	if (!p_unit.tree) {
		return items;
	}
	if (p_unit.tree->package) {
		for (const Node *definition : p_unit.tree->package->definitions) {
			outline_definition(items, definition);
		}
	}
	for (const Node *definition : p_unit.tree->definitions) {
		outline_definition(items, definition);
	}
	return items;
}

std::vector<FoundSymbol> Queries::find(std::string_view p_query, size_t p_limit) {
	std::vector<FoundSymbol> found;
	auto consider = [&](const Unit &p_unit, const OutlineItem &p_item, const std::string &p_container) {
		std::string name = p_item.kind == SymbolKind::Property ? p_item.name.substr(4) : p_item.name;
		if (found.size() >= p_limit || !contains_ignoring_case(name, p_query)) {
			return;
		}
		// A property with a getter and a setter is one definition: its first accessor stands for it.
		if (p_item.kind == SymbolKind::Property) {
			for (const FoundSymbol &earlier : found) {
				if (earlier.kind == SymbolKind::Property && earlier.unit == &p_unit && earlier.name == name && earlier.container == p_container) {
					return;
				}
			}
		}
		FoundSymbol symbol;
		symbol.name = std::move(name);
		symbol.container = p_container;
		symbol.kind = p_item.kind;
		symbol.is_const = p_item.is_const;
		symbol.is_constructor = p_item.is_constructor;
		symbol.unit = &p_unit;
		symbol.offset = p_item.name_offset;
		symbol.length = p_item.name_length;
		found.push_back(std::move(symbol));
	};
	// The program's own definitions first: with a limit, they are what is kept.
	for (bool declared : { false, true }) {
		if (declared && p_query.empty()) {
			break; // Everything, unasked: the program is what is meant, not Godot's thousands of names.
		}
		for (const Unit *file : compilation_.files()) {
			const Unit &unit = *file;
			if (unit.is_declaration != declared) {
				continue;
			}
			const std::string package = unit.package ? unit.package->name : std::string();
			for (const OutlineItem &item : outline(unit)) {
				consider(unit, item, package);
				const std::string container = package.empty() ? item.name : package + "." + item.name;
				for (const OutlineItem &child : item.children) {
					consider(unit, child, container);
				}
				if (found.size() >= p_limit) {
					return found;
				}
			}
		}
	}
	return found;
}

} // namespace kas3
