#include "query.h"

#include "ast_walk.h"

#include <algorithm>

namespace kas3 {

namespace {

std::string_view slice(const Unit &p_unit, uint32_t p_offset, uint32_t p_end) {
	const std::string &text = p_unit.source.text();
	if (p_offset > text.size() || p_end > text.size() || p_end < p_offset) {
		return std::string_view();
	}
	return std::string_view(text).substr(p_offset, p_end - p_offset);
}

std::string type_text(const Unit &p_unit, const TypeRef *p_type) {
	return p_type ? ":" + std::string(slice(p_unit, p_type->offset, p_type->end)) : std::string();
}

// The text of a default value or a constant's initializer, when it is short enough to show.
std::string value_text(const Unit &p_unit, const Expr *p_value) {
	if (!p_value) {
		return std::string();
	}
	std::string_view text = slice(p_unit, p_value->offset, p_value->end);
	if (text.empty() || text.size() > 40 || text.find('\n') != std::string_view::npos) {
		return " = ...";
	}
	return " = " + std::string(text);
}

std::string signature(const Unit &p_unit, const FunctionDecl *p_function, bool p_constructor) {
	std::string out = "function ";
	if (p_function->accessor == FunctionDecl::Getter) {
		out += "get ";
	} else if (p_function->accessor == FunctionDecl::Setter) {
		out += "set ";
	}
	out += p_function->name;
	out += '(';
	bool first = true;
	for (const Parameter &parameter : p_function->parameters) {
		if (!first) {
			out += ", ";
		}
		first = false;
		if (parameter.is_rest) {
			out += "...";
		}
		out += parameter.name;
		out += type_text(p_unit, parameter.type);
		out += value_text(p_unit, parameter.default_value);
	}
	out += ')';
	if (!p_constructor) {
		out += type_text(p_unit, p_function->return_type);
	}
	return out;
}

bool is_constructor(const Symbol *p_symbol) {
	return p_symbol->kind == SymbolKind::Method && p_symbol->owner && p_symbol->owner->klass &&
			p_symbol->owner->klass->constructor == p_symbol;
}

void add_occurrence(std::vector<Occurrence> &r_list, uint32_t p_offset, std::string_view p_name, Symbol *p_symbol,
		bool p_declaration) {
	if (p_symbol && !p_name.empty()) {
		r_list.push_back({ p_offset, (uint32_t)p_name.size(), p_symbol, p_declaration });
	}
}

// `super(...)`: the base class's constructor, or the class if it declares none.
void add_super_occurrence(Call *p_call, Symbol *p_enclosing, std::vector<Occurrence> &r_list) {
	Symbol *base = p_enclosing && p_enclosing->klass ? p_enclosing->klass->base : nullptr;
	if (p_call->callee && p_call->callee->kind == NodeKind::SuperExpr && base) {
		Symbol *constructor = base->klass ? base->klass->constructor : nullptr;
		add_occurrence(r_list, p_call->callee->offset, "super", constructor ? constructor : base, false);
	}
}

void add_function_occurrences(FunctionDecl *p_function, std::vector<Occurrence> &r_list) {
	add_occurrence(r_list, p_function->name_offset, p_function->name, p_function->symbol, true);
	for (const Parameter &parameter : p_function->parameters) {
		add_occurrence(r_list, parameter.offset, parameter.name, parameter.symbol, true);
	}
}

// The names one node declares or uses. r_enclosing is the class whose members are being walked.
void add_occurrences(Node *p_node, Symbol *&r_enclosing, std::vector<Occurrence> &r_list) {
	switch (p_node->kind) {
		case NodeKind::Name: {
			Name *name = static_cast<Name *>(p_node);
			add_occurrence(r_list, name->offset, name->text, name->symbol, false);
			break;
		}
		case NodeKind::TypeRef: {
			TypeRef *type = static_cast<TypeRef *>(p_node);
			if (type->name.count > 0) {
				uint32_t last = type->name.count - 1;
				add_occurrence(r_list, type->name_offsets[last], type->name[last], type->symbol, false);
			}
			break;
		}
		case NodeKind::Member: {
			Member *member = static_cast<Member *>(p_node);
			add_occurrence(r_list, member->name_offset, member->name, member->resolved(), false);
			break;
		}
		case NodeKind::Call:
			add_super_occurrence(static_cast<Call *>(p_node), r_enclosing, r_list);
			break;
		case NodeKind::Import: {
			Import *import = static_cast<Import *>(p_node);
			if (import->name.count > 0 && !import->wildcard) {
				uint32_t last = import->name.count - 1;
				add_occurrence(r_list, import->name_offsets[last], import->name[last], import->symbol, false);
			}
			break;
		}
		case NodeKind::ClassDecl: {
			ClassDecl *decl = static_cast<ClassDecl *>(p_node);
			r_enclosing = decl->symbol; // Parents come first in the walk, and classes do not nest.
			add_occurrence(r_list, decl->name_offset, decl->name, decl->symbol, true);
			break;
		}
		case NodeKind::InterfaceDecl: {
			InterfaceDecl *decl = static_cast<InterfaceDecl *>(p_node);
			add_occurrence(r_list, decl->name_offset, decl->name, decl->symbol, true);
			break;
		}
		case NodeKind::FunctionDecl:
			add_function_occurrences(static_cast<FunctionDecl *>(p_node), r_list);
			break;
		case NodeKind::VarDecl:
			for (const Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
				add_occurrence(r_list, declarator.offset, declarator.name, declarator.symbol, true);
			}
			break;
		case NodeKind::Try:
			for (const CatchClause &clause : static_cast<Try *>(p_node)->catches) {
				add_occurrence(r_list, clause.name_offset, clause.name, clause.symbol, true);
			}
			break;
		default:
			break;
	}
}

std::string class_text(const Unit &p_unit, const Symbol *p_symbol, const std::string &p_name) {
	std::string out = "class " + p_name;
	if (p_symbol->node && p_symbol->node->kind == NodeKind::ClassDecl) {
		const ClassDecl *decl = static_cast<const ClassDecl *>(p_symbol->node);
		if (decl->extends) {
			out += " extends " + std::string(slice(p_unit, decl->extends->offset, decl->extends->end));
		}
		for (uint32_t i = 0; i < decl->implements.count; i++) {
			out += (i == 0 ? " implements " : ", ") +
					std::string(slice(p_unit, decl->implements[i]->offset, decl->implements[i]->end));
		}
	}
	return out;
}

std::string interface_text(const Unit &p_unit, const Symbol *p_symbol, const std::string &p_name) {
	std::string out = "interface " + p_name;
	if (p_symbol->node && p_symbol->node->kind == NodeKind::InterfaceDecl) {
		const InterfaceDecl *decl = static_cast<const InterfaceDecl *>(p_symbol->node);
		for (uint32_t i = 0; i < decl->extends.count; i++) {
			out += (i == 0 ? " extends " : ", ") +
					std::string(slice(p_unit, decl->extends[i]->offset, decl->extends[i]->end));
		}
	}
	return out;
}

// A variable, a field or a local; a constant with its value.
std::string variable_text(const Unit &p_unit, const Symbol *p_symbol, const std::string &p_prefix,
		const std::string &p_name) {
	std::string out = p_prefix + (p_symbol->is_const ? "const " : "var ") + p_name + type_text(p_unit, p_symbol->type);
	if (p_symbol->is_const && p_symbol->node && p_symbol->node->kind == NodeKind::VarDecl) {
		for (const Declarator &declarator : static_cast<const VarDecl *>(p_symbol->node)->declarators) {
			if (declarator.symbol == p_symbol) {
				out += value_text(p_unit, declarator.initializer);
			}
		}
	}
	return out;
}

} // namespace

const std::vector<Occurrence> &Queries::occurrences(const Unit &p_unit) {
	auto cached = occurrences_.find(&p_unit);
	if (cached != occurrences_.end()) {
		return cached->second;
	}
	std::vector<Occurrence> &list = occurrences_[&p_unit];
	Symbol *enclosing = nullptr; // The class whose members are being walked.
	walk(p_unit.tree, [&](Node *p_node) { add_occurrences(p_node, enclosing, list); });
	std::stable_sort(list.begin(), list.end(),
			[](const Occurrence &p_a, const Occurrence &p_b) { return p_a.offset < p_b.offset; });
	return list;
}

const Occurrence *Queries::at(const Unit &p_unit, uint32_t p_offset) {
	const std::vector<Occurrence> &list = occurrences(p_unit);
	// The last name that starts at or before the offset.
	auto after = std::upper_bound(list.begin(), list.end(), p_offset,
			[](uint32_t p_value, const Occurrence &p_item) { return p_value < p_item.offset; });
	if (after == list.begin()) {
		return nullptr;
	}
	const Occurrence &found = *(after - 1);
	return p_offset <= found.offset + found.length ? &found : nullptr;
}

Place Queries::declaration(const Symbol *p_symbol) const {
	Place place;
	if (p_symbol && p_symbol->unit) {
		place.unit = p_symbol->unit;
		place.offset = p_symbol->offset;
		place.length = (uint32_t)p_symbol->name.size();
	}
	return place;
}

// True if one class is the other, or one of its bases.
static bool in_line(const Symbol *p_a, const Symbol *p_b) {
	for (const Symbol *at = p_a; at && at->klass; at = at->klass->base) {
		if (at == p_b) {
			return true;
		}
	}
	for (const Symbol *at = p_b; at && at->klass; at = at->klass->base) {
		if (at == p_a) {
			return true;
		}
	}
	return false;
}

// The same thing for the purpose of finding uses: the symbol itself, or for an instance method or
// property, one that overrides it or that it overrides.
static bool same_member(const Symbol *p_found, const Symbol *p_wanted) {
	if (p_found == p_wanted) {
		return true;
	}
	return p_found && p_found->kind == p_wanted->kind && p_wanted->is_instance_member() && p_found->is_instance_member() &&
			p_wanted->kind != SymbolKind::Field && p_found->name == p_wanted->name && p_found->owner != p_wanted->owner &&
			!(p_found->modifiers & Modifiers::Private) && !(p_wanted->modifiers & Modifiers::Private) &&
			in_line(p_found->owner, p_wanted->owner);
}

std::vector<Place> Queries::references(const Symbol *p_symbol, bool p_with_declaration) {
	std::vector<Place> places;
	if (!p_symbol) {
		return places;
	}
	const std::unordered_set<const Symbol *> implemented = same_through_interfaces(p_symbol);
	for (const Unit &unit : compilation_.units()) {
		if (unit.is_declaration) {
			continue;
		}
		for (const Occurrence &item : occurrences(unit)) {
			if ((same_member(item.symbol, p_symbol) || implemented.count(item.symbol)) &&
					(p_with_declaration || !item.is_declaration)) {
				places.push_back({ &unit, item.offset, item.length });
			}
		}
	}
	if (p_with_declaration && p_symbol->unit && p_symbol->unit->is_declaration) {
		places.push_back(declaration(p_symbol));
	}
	return places;
}

std::string Queries::describe(const Symbol *p_symbol) const {
	if (!p_symbol) {
		return std::string();
	}
	std::string name(p_symbol->name);
	if (!p_symbol->unit) {
		return name + ":Array"; // `arguments`.
	}
	const Unit &unit = *p_symbol->unit;
	std::string prefix = p_symbol->is_static() ? "static " : "";
	switch (p_symbol->kind) {
		case SymbolKind::Class:
			return class_text(unit, p_symbol, name);
		case SymbolKind::Interface:
			return interface_text(unit, p_symbol, name);
		case SymbolKind::Function:
		case SymbolKind::Method:
		case SymbolKind::LocalFunction:
			if (p_symbol->node && p_symbol->node->kind == NodeKind::FunctionDecl) {
				return prefix + signature(unit, static_cast<const FunctionDecl *>(p_symbol->node), is_constructor(p_symbol));
			}
			return prefix + "function " + name;
		case SymbolKind::Property: {
			const FunctionDecl *accessor = p_symbol->getter ? p_symbol->getter : p_symbol->setter;
			// A half that a base class supplies is written in that class's file.
			if (accessor && accessor->symbol && accessor->symbol->unit) {
				return prefix + signature(*accessor->symbol->unit, accessor, false);
			}
			return prefix + "function get " + name + "()" + type_text(unit, p_symbol->type);
		}
		case SymbolKind::Variable:
		case SymbolKind::Field:
		case SymbolKind::Local:
			return variable_text(unit, p_symbol, prefix, name);
		case SymbolKind::Parameter:
			return (p_symbol->is_rest ? "..." : "") + name + type_text(unit, p_symbol->type);
		case SymbolKind::CatchVariable:
			return name + type_text(unit, p_symbol->type);
		case SymbolKind::Arguments:
			return name + ":Array";
	}
	return name;
}

std::string Queries::context(const Symbol *p_symbol) const {
	if (!p_symbol) {
		return std::string();
	}
	switch (p_symbol->kind) {
		case SymbolKind::Local:
			return "local variable";
		case SymbolKind::LocalFunction:
			return "local function";
		case SymbolKind::Parameter:
			return "parameter";
		case SymbolKind::CatchVariable:
			return "catch variable";
		case SymbolKind::Arguments:
			return "the arguments of this call";
		default:
			break;
	}
	std::string kind = is_constructor(p_symbol) ? "constructor" : symbol_kind_name(p_symbol->kind);
	if (p_symbol->owner) {
		return kind + " of " + compilation_.qualified_name(p_symbol->owner);
	}
	if (p_symbol->unit && p_symbol->unit->is_builtin) {
		return kind + ", built in";
	}
	if (p_symbol->file_private && p_symbol->unit) {
		return kind + " in " + p_symbol->unit->source.path();
	}
	if (p_symbol->package && !p_symbol->package->name.empty()) {
		return kind + " in package " + p_symbol->package->name;
	}
	return kind;
}

} // namespace kas3
