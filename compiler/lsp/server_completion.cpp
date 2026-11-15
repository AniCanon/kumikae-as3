#include "server.h"

#include <cstdio>

namespace kas3 {

// The protocol's CompletionItemKind for what is offered.
static int completion_kind(const Completion &p_item) {
	if (p_item.is_package) {
		return 9;
	}
	if (p_item.is_keyword) {
		return 14;
	}
	switch (p_item.kind) {
		case SymbolKind::Class:
			return 7;
		case SymbolKind::Interface:
			return 8;
		case SymbolKind::Method:
			return 2;
		case SymbolKind::Function:
		case SymbolKind::LocalFunction:
			return 3;
		case SymbolKind::Property:
			return 10;
		case SymbolKind::Field:
			return p_item.is_const ? 21 : 5;
		default:
			return p_item.is_const ? 21 : 6;
	}
}

static std::string snippet_escape(std::string_view p_text) {
	std::string result;
	for (char c : p_text) {
		if (c == '$' || c == '}' || c == '\\') {
			result += '\\';
		}
		result += c;
	}
	return result;
}

static std::string insertion(const Completion &p_item, bool p_call) {
	if (!p_call || !p_item.callable) {
		return p_item.name;
	}
	std::string result = snippet_escape(p_item.name) + "(";
	for (size_t i = 0; i < p_item.required_parameters.size(); ++i) {
		if (i) {
			result += ", ";
		}
		result += "${" + std::to_string(i + 1) + ":" + snippet_escape(p_item.required_parameters[i]) + "}";
	}
	return result + ")$0";
}

Json Server::completion(const Json &p_params) {
	Json items = Json::array();
	Json defaults;
	Unit *unit = unit_of(p_params);
	if (unit && p_params.get("position").is_object()) {
		Queries &queries = workspace_.queries();
		const uint32_t offset = offset_at(*unit, p_params.get("position"));
		const CompletionRange span = queries.completion_range(*unit, offset);
		Json edit_range = range(*unit, span.begin, span.end - span.begin);
		if (insert_replace_) {
			edit_range = Json::object().set("insert", range(*unit, span.begin, span.caret - span.begin))
					.set("replace", range(*unit, span.begin, span.end - span.begin));
		}
		if (default_edit_range_) {
			defaults = Json::object().set("editRange", edit_range);
		}
		uint32_t priority = 0;
		for (const Completion &found : queries.complete(*unit, offset)) {
			Json item = Json::object().set("label", found.name).set("kind", completion_kind(found));
			if (!found.detail.empty()) {
				item.set("detail", found.detail);
			}
			char order[16];
			std::snprintf(order, sizeof(order), "%08u", priority++);
			item.set("sortText", order);
			const bool call = snippets_ && span.call && found.callable;
			if (default_edit_range_) {
				if (call) {
					item.set("textEditText", insertion(found, true));
				}
			} else {
				Json edit = Json::object().set("newText", insertion(found, call));
				if (insert_replace_) {
					edit.set("insert", edit_range.get("insert")).set("replace", edit_range.get("replace"));
				} else {
					edit.set("range", edit_range);
				}
				item.set("textEdit", std::move(edit));
			}
			if (call) {
				item.set("insertTextFormat", 2);
			}
			items.push(std::move(item));
		}
	}
	Json result = Json::object().set("isIncomplete", false).set("items", std::move(items));
	if (!defaults.is_null()) {
		result.set("itemDefaults", std::move(defaults));
	}
	return result;
}

Json Server::signature_help(const Json &p_params) {
	Unit *unit = unit_of(p_params);
	if (!unit || !p_params.get("position").is_object()) {
		return Json();
	}
	const auto help = workspace_.queries().signature_help(*unit, offset_at(*unit, p_params.get("position")));
	if (!help) {
		return Json();
	}
	Json parameters = Json::array();
	for (const std::string &parameter : help->parameters) {
		parameters.push(Json::object().set("label", parameter));
	}
	Json signature = Json::object().set("label", help->label).set("parameters", std::move(parameters));
	return Json::object().set("signatures", Json::array().push(std::move(signature)))
			.set("activeSignature", 0).set("activeParameter", static_cast<int>(help->active_parameter));
}

} // namespace kas3
