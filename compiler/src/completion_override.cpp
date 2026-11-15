// An override's whole declaration, offered where `override` is written before a member's name. What is
// written is what compiles in the file it goes into: a type the file can't name by itself is written in
// full.
#include "query.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace kas3 {

namespace {

std::string_view text_of(const Unit &p_unit, const Node *p_node) {
	const std::string &text = p_unit.source.text();
	return p_node->end <= text.size() && p_node->offset <= p_node->end ?
			std::string_view(text).substr(p_node->offset, p_node->end - p_node->offset) :
			std::string_view();
}

// The words before the cursor's word, back to the end of the member before: only modifiers, `function`
// and `get` or `set` may be among them.
struct Written {
	bool is_override = false;
	bool access = false;
	bool function = false;
	FunctionDecl::Accessor accessor = FunctionDecl::None;
};

std::optional<Written> words_before(const Queries &p_queries, const Unit &p_unit, uint32_t p_start) {
	const std::string_view text = p_unit.source.text();
	Written written;
	uint32_t at = p_start;
	while (true) {
		while (at > 0 && (std::isspace((unsigned char)text[at - 1]) || p_queries.text_byte(p_unit, at - 1))) {
			at--;
		}
		uint32_t begin = at;
		while (begin > 0 && is_identifier_at(text, begin - 1)) {
			begin--;
		}
		if (begin == at) {
			break;
		}
		const std::string_view word = text.substr(begin, at - begin);
		if (word == "override") {
			written.is_override = true;
		} else if (word == "public" || word == "protected" || word == "internal" || word == "private") {
			written.access = true;
		} else if (word == "function") {
			written.function = true;
		} else if ((word == "get" || word == "set") && !written.function) {
			written.accessor = word == "get" ? FunctionDecl::Getter : FunctionDecl::Setter;
		} else if (word != "final") {
			return std::nullopt;
		}
		at = begin;
	}
	// What comes before is the end of a member, the class's opening brace, or metadata.
	if (!written.is_override || (at > 0 && text[at - 1] != '{' && text[at - 1] != '}' && text[at - 1] != ';' && text[at - 1] != ']')) {
		return std::nullopt;
	}
	return written;
}

class Overrider {
	Compilation &compilation_;
	Symbol *class_;
	Context context_;
	Written written_;

	// The type as this file can write it: by its name if that means the same type here, otherwise in full.
	std::string type(const Unit &p_unit, const TypeRef *p_type) {
		if (p_type->form == TypeRef::Any) {
			return "*";
		}
		if (p_type->form == TypeRef::Void) {
			return "void";
		}
		const Symbol *symbol = p_type->symbol;
		if (!symbol) {
			return std::string(text_of(p_unit, p_type));
		}
		std::string name(symbol->name);
		if (compilation_.lookup_package_level(context_, symbol->name).first != symbol && symbol->package &&
				!symbol->package->name.empty()) {
			name = symbol->package->name + "." + name;
		}
		return p_type->form == TypeRef::Applied && p_type->argument ? name + ".<" + type(p_unit, p_type->argument) + ">" : name;
	}

	std::string declaration(const Symbol *p_member, const FunctionDecl *p_function) {
		const Unit &unit = *p_member->unit;
		std::string out;
		if (!written_.access) {
			const uint16_t bits = p_function->modifiers.bits;
			out += bits & Modifiers::Public ? "public " : bits & Modifiers::Protected ? "protected " : "internal ";
		}
		if (!written_.function) {
			out += "function ";
		}
		if (written_.accessor == FunctionDecl::None && p_function->accessor != FunctionDecl::None) {
			out += p_function->accessor == FunctionDecl::Getter ? "get " : "set ";
		}
		std::string name(p_member->name);
		std::string arguments;
		out += name + "(";
		for (const Parameter &parameter : p_function->parameters) {
			if (&parameter != &p_function->parameters[0]) {
				out += ", ";
			}
			out += (parameter.is_rest ? "..." : "") + std::string(parameter.name);
			out += parameter.type ? ":" + type(unit, parameter.type) : std::string();
			if (parameter.default_value) {
				out += " = " + std::string(text_of(unit, parameter.default_value));
			}
			if (!parameter.is_rest) {
				arguments += (arguments.empty() ? "" : ", ") + std::string(parameter.name);
			}
		}
		out += ")";
		const TypeRef *result = p_function->return_type;
		out += result ? ":" + type(unit, result) : std::string();
		if (p_function->accessor == FunctionDecl::Getter) {
			return out + " { return super." + name + "; }";
		}
		if (p_function->accessor == FunctionDecl::Setter) {
			return out + " { super." + name + " = " + arguments + "; }";
		}
		if (result && result->form == TypeRef::Void) {
			return out + " {}";
		}
		return out + " { return super." + name + "(" + arguments + "); }";
	}

	// The function that would be overridden: a method, or the half of a property that is asked for.
	const FunctionDecl *overridable(const Symbol *p_member) const {
		if (p_member->kind == SymbolKind::Method && written_.accessor == FunctionDecl::None) {
			return static_cast<const FunctionDecl *>(p_member->node);
		}
		if (p_member->kind != SymbolKind::Property) {
			return nullptr;
		}
		if (written_.accessor == FunctionDecl::Setter) {
			return p_member->setter;
		}
		return p_member->getter ? p_member->getter : written_.accessor == FunctionDecl::None ? p_member->setter : nullptr;
	}

public:
	Overrider(Compilation &p_compilation, Symbol *p_class, Written p_written) :
			compilation_(p_compilation), class_(p_class), context_(p_compilation.context_of(p_class)), written_(p_written) {}

	// Each member offered, with its declaration.
	std::vector<std::pair<Symbol *, std::string>> offer(uint32_t p_start, std::string_view p_prefix) {
		std::vector<std::pair<Symbol *, std::string>> found;
		std::unordered_set<std::string_view> seen;
		for (const auto &[name, member] : class_->klass->members) {
			if (member->owner == class_ && member->offset != p_start) {
				seen.insert(name); // Written in this class already: overridden, or a mistake to write twice.
			}
		}
		for (Symbol *base = class_->klass->base; base && base->klass; base = base->klass->base) {
			if (base->unit && base->unit->is_builtin) {
				continue; // Flash takes a built-in method without `override`.
			}
			std::vector<Symbol *> sorted;
			for (const auto &[name, member] : base->klass->members) {
				sorted.push_back(member);
			}
			std::sort(sorted.begin(), sorted.end(), [](const Symbol *p_a, const Symbol *p_b) { return p_a->name < p_b->name; });
			for (Symbol *member : sorted) {
				const FunctionDecl *function = overridable(member);
				if (!function || (function->modifiers.bits & Modifiers::Final) || !inherited_visible(member, base, class_->package) ||
						!seen.insert(member->name).second) {
					continue;
				}
				if (starts_with_any_case(member->name, p_prefix)) {
					found.emplace_back(member, declaration(member, function));
				}
			}
		}
		return found;
	}
};

} // namespace

std::optional<OverrideOffer> Queries::overrides(const Unit &p_unit, Symbol *p_class, uint32_t p_start, std::string_view p_prefix) {
	if (!p_class || p_class->kind != SymbolKind::Class || !p_class->klass) {
		return std::nullopt;
	}
	std::optional<Written> written = words_before(*this, p_unit, p_start);
	if (!written) {
		return std::nullopt;
	}
	OverrideOffer offer;
	offer.after_function = written->function;
	for (auto &[member, declaration] : Overrider(compilation_, p_class, *written).offer(p_start, p_prefix)) {
		Completion item = completion_item(member);
		item.callable = false; // The declaration is written whole: no call follows.
		item.required_parameters.clear();
		item.insert_text = std::move(declaration);
		offer.items.push_back(std::move(item));
	}
	return offer;
}

} // namespace kas3
