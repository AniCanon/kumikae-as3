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
	walk(p_value, [&](Node *p_node) {
		if (p_node->kind == NodeKind::Member) {
			const auto *symbol = static_cast<Member *>(p_node)->symbol;
			accessible &= symbol && (symbol->modifiers & Modifiers::Public);
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

bool matches(std::string_view p_name, std::string_view p_prefix) {
	return p_name.size() >= p_prefix.size() && std::equal(p_prefix.begin(), p_prefix.end(), p_name.begin(), [](char p_a, char p_b) {
		return std::tolower(static_cast<unsigned char>(p_a)) == std::tolower(static_cast<unsigned char>(p_b));
	});
}

} // namespace

std::vector<Completion> Queries::overrides(const Unit &p_unit, const FunctionDecl &p_function, Symbol *p_class, uint32_t p_offset) {
	std::vector<Completion> result;
	// Completion edits replace one line only and must not duplicate an existing signature or body.
	if (p_function.has_parameter_list || p_function.return_type || p_function.body) {
		return result;
	}
	std::unordered_set<std::string> seen;
	std::unordered_set<const Symbol *> visited;
	const auto prefix = p_function.name.substr(0, p_offset >= p_function.name_offset ? p_offset - p_function.name_offset : 0);
	const auto span = completion_range(p_unit, p_offset);
	const std::string newline = line_break(p_unit);
	const std::string indent = line_indent(p_unit, p_function.offset);
	for (Symbol *base = p_class->klass->base; base && base->klass && visited.insert(base).second; base = base->klass->base) {
		for (const auto &[name, member] : base->klass->members) {
			if (!matches(name, prefix) || !seen.insert(std::string(name)).second ||
					(member->modifiers & Modifiers::Final) || member->is_static() ||
					!inherited_visible(member, base, p_class->package)) {
				continue;
			}
			auto own = p_class->klass->members.find(name);
			if (own != p_class->klass->members.end() && own->second != p_function.symbol) {
				continue;
			}
			const FunctionDecl *function = p_function.accessor == FunctionDecl::Getter ? member->getter :
					p_function.accessor == FunctionDecl::Setter ? member->setter : function_declaration(member);
			if (!function || function->accessor != p_function.accessor) {
				continue;
			}
			constexpr uint16_t ACCESS = Modifiers::Public | Modifiers::Protected | Modifiers::Private | Modifiers::Internal;
			const uint16_t inherited_access = function->modifiers.bits & ACCESS;
			const uint16_t written_access = p_function.modifiers.bits & ACCESS;
			if (written_access && written_access != (inherited_access ? inherited_access : Modifiers::Internal)) {
				continue;
			}
			auto text = override_declaration(compilation_, *function, compilation_.context_of(p_class));
			if (!text) {
				continue;
			}
			Completion item = completion_item(member);
			// Keep the filtering word in the primary single-line edit. Modifiers may span lines.
			const size_t name_at = text->find(std::string(function->name) + "(");
			if (!written_access) {
				const auto &metadata = p_function.metadata;
				const uint32_t at = metadata.count ? metadata[metadata.count - 1].end : p_function.offset;
				item.additional_edit = CompletionEdit{ at, at, (metadata.count ? " " : "") + text->substr(0, text->find(' ') + 1) };
			}
			text->erase(0, name_at);
			item.callable = false;
			*text += " {" + newline + indent + "\t";
			item.caret = static_cast<uint32_t>(text->size());
			*text += "throw new Error(\"Not implemented\");" + newline + indent + "}";
			item.edit = CompletionEdit{ span.begin, span.end, std::move(*text) };
			result.push_back(std::move(item));
		}
	}
	std::sort(result.begin(), result.end(), [](const Completion &p_a, const Completion &p_b) { return p_a.name < p_b.name; });
	return result;
}

} // namespace kas3
