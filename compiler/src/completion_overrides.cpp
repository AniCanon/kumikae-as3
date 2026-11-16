#include "query.h"

#include "ast_walk.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace kas3 {

namespace {

std::string source_name(const Compilation &p_compilation, const Symbol *p_symbol) {
	return p_symbol->file_private ? p_symbol->name : p_compilation.qualified_name(p_symbol);
}

std::string written_type(const Compilation &p_compilation, const TypeRef *p_type, const Context &p_context) {
	if (!p_type || p_type->form == TypeRef::Any) {
		return "*";
	}
	if (p_type->form == TypeRef::Void) {
		return "void";
	}
	if (p_type->symbol && !p_compilation.visible(p_type->symbol, p_context)) {
		return {};
	}
	std::string name = p_type->symbol ? source_name(p_compilation, p_type->symbol) : std::string();
	if (name.empty()) {
		return name;
	}
	if (p_type->form == TypeRef::Applied) {
		const std::string argument = written_type(p_compilation, p_type->argument, p_context);
		if (argument.empty()) {
			return {};
		}
		name += ".<" + argument + ">";
	}
	return name;
}

std::optional<std::string> default_value(const Compilation &p_compilation, const Unit &p_unit, Expr *p_value, const Context &p_context) {
	bool accessible = true;
	std::vector<CompletionEdit> names;
	std::unordered_set<const Node *> package_parts;
	walk(p_value, [&](Node *p_node) {
		if (package_parts.count(p_node)) {
			return;
		}
		if (p_node->kind == NodeKind::Member) {
			const auto *member = static_cast<Member *>(p_node);
			if (member->symbol) {
				accessible &= p_compilation.visible(member->symbol, p_context);
				// Below a package-qualified definition the names are package text, not value references.
				walk(member->object, [&](Node *p_part) { package_parts.insert(p_part); });
			} else {
				accessible &= member->member && (member->member->modifiers & Modifiers::Public);
			}
			return;
		}
		if (p_node->kind != NodeKind::Name) {
			return;
		}
		const auto *name = static_cast<Name *>(p_node);
		const auto *symbol = name->symbol;
		if (!symbol || (!symbol->package && !symbol->owner)) {
			return;
		}
		accessible &= symbol->owner ? (symbol->modifiers & Modifiers::Public) && p_compilation.visible(symbol->owner, p_context) :
				p_compilation.visible(symbol, p_context);
		const std::string qualified = symbol->owner ? source_name(p_compilation, symbol->owner) + "." + symbol->name :
				source_name(p_compilation, symbol);
		names.push_back({ name->offset, name->end, qualified });
	});
	if (!accessible) {
		return std::nullopt;
	}
	std::sort(names.begin(), names.end(), [](const CompletionEdit &p_a, const CompletionEdit &p_b) { return p_a.begin < p_b.begin; });
	const std::string &source = p_unit.source.text();
	uint32_t at = p_value->offset;
	std::string result;
	for (const auto &name : names) {
		if (name.begin >= at && name.end <= p_value->end) {
			result.append(source, at, name.begin - at);
			result += name.text;
			at = name.end;
		}
	}
	result.append(source, at, p_value->end - at);
	return result;
}

std::optional<std::string> override_declaration(const Compilation &p_compilation, const FunctionDecl &p_function, const Context &p_context) {
	std::string text = p_function.modifiers.bits & Modifiers::Protected ? "protected " :
			p_function.modifiers.bits & Modifiers::Public ? "public " : "internal ";
	text += "override function ";
	if (p_function.accessor != FunctionDecl::None) {
		text += p_function.accessor == FunctionDecl::Getter ? "get " : "set ";
	}
	text += p_function.name;
	text += '(';
	bool first = true;
	for (const Parameter &parameter : p_function.parameters) {
		if (!first) {
			text += ", ";
		}
		first = false;
		text += parameter.is_rest ? "..." : "";
		text += parameter.name;
		if (parameter.type) {
			const std::string type = written_type(p_compilation, parameter.type, p_context);
			if (type.empty()) {
				return std::nullopt;
			}
			text += ":" + type;
		}
		if (parameter.default_value) {
			const auto value = default_value(p_compilation, *p_function.symbol->unit, parameter.default_value, p_context);
			if (!value) {
				return std::nullopt;
			}
			text += " = " + *value;
		}
	}
	const std::string type = written_type(p_compilation, p_function.return_type, p_context);
	return type.empty() ? std::nullopt : std::optional<std::string>(text + "):" + type);
}

constexpr uint16_t ACCESS = Modifiers::Public | Modifiers::Protected | Modifiers::Private | Modifiers::Internal;

// What is written of the declaration before the name being completed.
struct Written {
	uint16_t access = 0;
	bool function = false;
	FunctionDecl::Accessor accessor = FunctionDecl::None; // `get` or `set`, after `function`.
	uint32_t modifiers_at = 0; // Where an access word goes when none is written.
	bool after_metadata = false; // The access word follows metadata on the same declaration.
	const FunctionDecl *self = nullptr; // The declaration the parser made of what is written, if any.
};

Written from_declaration(const FunctionDecl &p_function) {
	Written written;
	written.access = p_function.modifiers.bits & ACCESS;
	written.function = true;
	written.accessor = p_function.accessor;
	const auto &metadata = p_function.metadata;
	written.modifiers_at = metadata.count ? metadata[metadata.count - 1].end : p_function.offset;
	written.after_metadata = metadata.count != 0;
	written.self = &p_function;
	return written;
}

// The words before the cursor's word, back to the end of the member before, when the parser made no
// declaration of them: only modifiers with `override`, `function`, and `get` or `set` after it.
std::optional<Written> words_before(const Queries &p_queries, const Unit &p_unit, uint32_t p_start) {
	const std::string_view text = p_unit.source.text();
	Written written;
	bool is_override = false;
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
			is_override = true;
		} else if (word == "public" || word == "protected" || word == "internal" || word == "private") {
			written.access = word == "public" ? Modifiers::Public : word == "protected" ? Modifiers::Protected :
					word == "internal" ? Modifiers::Internal : Modifiers::Private;
		} else if (word == "function") {
			written.function = true;
		} else if ((word == "get" || word == "set") && !written.function) {
			written.accessor = word == "get" ? FunctionDecl::Getter : FunctionDecl::Setter;
		} else if (word != "final") {
			return std::nullopt;
		}
		at = begin;
		written.modifiers_at = begin;
	}
	// `get` or `set` without `function` before it is not a declaration's start.
	if (!is_override || (written.accessor != FunctionDecl::None && !written.function)) {
		return std::nullopt;
	}
	// What comes before is the end of a member, the class's opening brace, or metadata.
	if (at > 0 && text[at - 1] != '{' && text[at - 1] != '}' && text[at - 1] != ';' && text[at - 1] != ']') {
		return std::nullopt;
	}
	return written;
}

const FunctionDecl *accessor_of(const Symbol *p_member, FunctionDecl::Accessor p_accessor) {
	return p_accessor == FunctionDecl::Getter ? p_member->getter : p_member->setter;
}

bool already_declared(const Symbol *p_member, const Written &p_written) {
	const FunctionDecl *self = p_written.self;
	if (p_member->kind != SymbolKind::Property || p_written.accessor == FunctionDecl::None) {
		return p_member != (self ? self->symbol : nullptr);
	}
	const auto *accessor = accessor_of(p_member, p_written.accessor);
	// Property linking copies the other half from the base; that is not a declaration in this class.
	return accessor && accessor != self && accessor->symbol == p_member;
}

// The function that would be overridden. Once `function` is written, what follows it says whether it is
// a method or which half of a property; before that, either may still be written. p_method is the
// member's declaration as a method.
const FunctionDecl *overridable(const Symbol *p_member, const FunctionDecl *p_method, const Written &p_written) {
	if (p_written.function) {
		const FunctionDecl *function = p_written.accessor == FunctionDecl::None ? p_method : accessor_of(p_member, p_written.accessor);
		return function && function->accessor == p_written.accessor ? function : nullptr;
	}
	if (p_member->kind == SymbolKind::Property) {
		return p_member->getter ? p_member->getter : p_member->setter;
	}
	return p_member->kind == SymbolKind::Method ? p_method : nullptr;
}

// The item for one inherited method: its declaration as the edit, with the caret in a body that throws.
Completion override_item(Completion p_item, const FunctionDecl &p_function, std::string p_text, const Written &p_written,
		const CompletionRange &p_span, const std::string &p_newline, const std::string &p_indent) {
	// Keep the filtering word in the primary single-line edit. Modifiers may span lines.
	const size_t name_at = p_text.find(std::string(p_function.name) + "(");
	if (!p_written.access) {
		p_item.additional_edit = CompletionEdit{ p_written.modifiers_at, p_written.modifiers_at,
			(p_written.after_metadata ? " " : "") + p_text.substr(0, p_text.find(' ') + 1) };
	}
	p_text.erase(0, name_at);
	if (!p_written.function) {
		const char *half = p_function.accessor == FunctionDecl::Getter ? "get " :
				p_function.accessor == FunctionDecl::Setter ? "set " : "";
		p_text.insert(0, std::string("function ") + half);
	}
	p_item.callable = false;
	p_item.required_parameters.clear();
	p_text += " {" + p_newline + p_indent + "\t";
	const auto caret = static_cast<uint32_t>(p_text.size());
	p_item.tab_stops.push_back({ caret, caret, 0 });
	p_text += "throw new Error(\"Not implemented\");" + p_newline + p_indent + "}";
	p_item.edit = CompletionEdit{ p_span.begin, p_span.end, std::move(p_text) };
	return p_item;
}

} // namespace

std::optional<OverrideOffer> Queries::overrides(const Unit &p_unit, const FunctionDecl *p_function, Symbol *p_class,
		uint32_t p_start, uint32_t p_offset) {
	if (!p_class || p_class->kind != SymbolKind::Class || !p_class->klass) {
		return std::nullopt;
	}
	std::optional<Written> found = p_function ? from_declaration(*p_function) : words_before(*this, p_unit, p_start);
	if (!found) {
		return std::nullopt;
	}
	const Written &written = *found;
	OverrideOffer offer;
	offer.after_function = written.function;
	// Completion edits replace one line only and must not duplicate an existing signature or body.
	if (p_function && (p_function->has_parameter_list || p_function->return_type || p_function->body)) {
		return offer;
	}
	std::unordered_set<std::string> seen;
	std::unordered_set<const Symbol *> visited;
	const std::string_view prefix = std::string_view(p_unit.source.text()).substr(p_start, p_offset - p_start);
	const auto span = completion_range(p_unit, p_offset);
	const std::string newline = line_break(p_unit);
	const std::string indent = line_indent(p_unit, p_function ? p_function->offset : written.modifiers_at);
	for (Symbol *base = p_class->klass->base; base && base->klass && visited.insert(base).second; base = base->klass->base) {
		for (const auto &[name, member] : base->klass->members) {
			if (!starts_with_any_case(name, prefix) || member->is_static() ||
					!inherited_visible(member, base, p_class->package) || !seen.insert(std::string(name)).second) {
				continue;
			}
			auto own = p_class->klass->members.find(name);
			if (own != p_class->klass->members.end() && already_declared(own->second, written)) {
				continue;
			}
			const FunctionDecl *function = overridable(member, function_declaration(member), written);
			if (!function || (function->modifiers.bits & Modifiers::Final)) {
				continue;
			}
			const uint16_t inherited_access = function->modifiers.bits & ACCESS;
			if (written.access && written.access != (inherited_access ? inherited_access : Modifiers::Internal)) {
				continue;
			}
			auto text = override_declaration(compilation_, *function, compilation_.context_of(p_class));
			if (!text) {
				continue;
			}
			offer.items.push_back(
					override_item(completion_item(member), *function, std::move(*text), written, span, newline, indent));
		}
	}
	std::sort(offer.items.begin(), offer.items.end(), [](const Completion &p_a, const Completion &p_b) { return p_a.name < p_b.name; });
	return offer;
}

} // namespace kas3
