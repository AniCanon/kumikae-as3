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

static std::string snippet_escape(std::string_view p_text, bool p_placeholder = false) {
	std::string result;
	for (char c : p_text) {
		// Only a placeholder uses '}' as a delimiter. Rider preserves an unnecessary top-level escape.
		if (c == '$' || c == '\\' || (p_placeholder && c == '}')) {
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
		result += "${" + std::to_string(i + 1) + ":" + snippet_escape(p_item.required_parameters[i], true) + "}";
	}
	return result + ")$0";
}

static std::string edit_snippet(const Completion &p_item) {
	const std::string_view text = p_item.edit->text;
	std::string result;
	uint32_t previous = 0;
	for (const auto &stop : p_item.tab_stops) {
		result += snippet_escape(text.substr(previous, stop.begin - previous));
		if (stop.begin == stop.end) {
			result += "$" + std::to_string(stop.index);
		} else {
			result += "${" + std::to_string(stop.index) + ":" +
					snippet_escape(text.substr(stop.begin, stop.end - stop.begin), true) + "}";
		}
		previous = stop.end;
	}
	return result + snippet_escape(text.substr(previous));
}

// One item of the list, in the order p_priority gives it.
Json Server::completion_item(const Unit &p_unit, const Completion &p_found, const CompletionRange &p_span,
		const Json &p_edit_range, uint32_t p_priority) const {
	Json item = Json::object().set("label", p_found.name).set("kind", completion_kind(p_found));
	if (!p_found.detail.empty()) {
		item.set("detail", p_found.detail);
	}
	if (!p_found.documentation.empty()) {
		const auto &docs = p_found.documentation;
		// An indented ASDoc example needs Markdown to keep its line breaks in Rider.
		if (markdown_completion_ && docs.find("\n\n    ") != std::string::npos) {
			item.set("documentation", Json::object().set("kind", "markdown").set("value", docs));
		} else {
			item.set("documentation", docs);
		}
	}
	char order[16];
	std::snprintf(order, sizeof(order), "%08u", p_priority);
	item.set("sortText", order);
	if (p_found.is_parameter_name) {
		item.set("preselect", true);
	}
	const bool call = snippets_ && p_span.call && p_found.callable;
	if (p_found.additional_edit) {
		const auto &import = *p_found.additional_edit;
		item.set("additionalTextEdits", Json::array().push(Json::object()
				.set("range", range(p_unit, import.begin, import.end - import.begin)).set("newText", import.text)));
	}
	if (p_found.edit) {
		const auto &primary = *p_found.edit;
		std::string text = primary.text;
		if (snippets_ && !p_found.tab_stops.empty()) {
			text = edit_snippet(p_found);
			item.set("insertTextFormat", 2);
		} else if (p_found.is_keyword) {
			text = p_found.name;
		}
		item.set("textEdit", Json::object().set("range", range(p_unit, primary.begin, primary.end - primary.begin))
				.set("newText", std::move(text)));
	} else if (default_edit_range_) {
		if (call) {
			item.set("textEditText", insertion(p_found, true));
		}
	} else {
		Json edit = Json::object().set("newText", insertion(p_found, call));
		if (insert_replace_) {
			edit.set("insert", p_edit_range.get("insert")).set("replace", p_edit_range.get("replace"));
		} else {
			edit.set("range", p_edit_range);
		}
		item.set("textEdit", std::move(edit));
	}
	if (call) {
		item.set("insertTextFormat", 2);
	}
	return item;
}

Json Server::completion(const Json &p_params) {
	Json items = Json::array();
	bool incomplete = false;
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
		const auto completion = queries.complete(*unit, offset);
		incomplete = completion.incomplete;
		for (const Completion &found : completion.items) {
			items.push(completion_item(*unit, found, span, edit_range, priority++));
		}
	}
	Json result = Json::object().set("isIncomplete", incomplete).set("items", std::move(items));
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
	if (!help->documentation.empty()) {
		signature.set("documentation", help->documentation);
	}
	return Json::object().set("signatures", Json::array().push(std::move(signature)))
			.set("activeSignature", 0).set("activeParameter", static_cast<int>(help->active_parameter));
}

} // namespace kas3
