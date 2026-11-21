// Requests about the code at a place or in the project: definition, references, hover and symbols.
#include "server.h"

namespace kas3 {

namespace {

int symbol_kind(SymbolKind p_kind, bool p_const, bool p_constructor) {
	switch (p_kind) {
		case SymbolKind::Class:
			return 5;
		case SymbolKind::Interface:
			return 11;
		case SymbolKind::Method:
			return p_constructor ? 9 : 6;
		case SymbolKind::Property:
			return 7;
		case SymbolKind::Field:
			return p_const ? 14 : 8;
		case SymbolKind::Function:
		case SymbolKind::LocalFunction:
			return 12;
		default:
			return p_const ? 14 : 13;
	}
}

} // namespace

Json Server::definition(const Json &p_params) {
	Unit *unit = unit_of(p_params);
	if (!unit || !p_params.get("position").is_object()) {
		return Json();
	}
	Json where = location(workspace_.queries().definition(*unit, offset_at(*unit, p_params.get("position"))));
	// A list of one. The protocol allows a place on its own, and VS Code reads it, but the client
	// library JetBrains' IDEs use reads only a list: with a bare place, go to definition does nothing.
	return where.is_null() ? Json() : Json::array().push(std::move(where));
}

Json Server::references(const Json &p_params) {
	Json list = Json::array();
	Unit *unit = nullptr;
	const Occurrence *found = occurrence_at(p_params, &unit);
	if (!found) {
		return list;
	}
	bool with_declaration = p_params.get("context").get("includeDeclaration").boolean();
	workspace_.compilation(); // Uses are in every file's bodies.
	for (const Place &place : workspace_.queries().references(found->symbol, with_declaration)) {
		Json where = location(place);
		if (!where.is_null()) {
			list.push(std::move(where));
		}
	}
	return list;
}

Json Server::hover(const Json &p_params) {
	Unit *unit = nullptr;
	const Occurrence *found = occurrence_at(p_params, &unit);
	if (!found) {
		return Json();
	}
	Queries &queries = workspace_.queries();
	std::string text = "```" + code_language_ + "\n" + queries.describe(found->symbol) + "\n```\n" + queries.context(found->symbol);
	const auto docs = queries.documentation(found->symbol);
	if (!docs.empty()) {
		text += "\n\n" + docs;
	}
	return Json::object()
			.set("contents", Json::object().set("kind", "markdown").set("value", text))
			.set("range", range(*unit, found->offset, found->length));
}

Json Server::document_symbols(const Json &p_params) {
	Json list = Json::array();
	Unit *unit = unit_of(p_params);
	if (!unit) {
		return list;
	}
	struct Convert {
		const Server &server;
		const Unit &unit;
		Json operator()(const OutlineItem &p_item) const {
			Json symbol = Json::object();
			symbol.set("name", p_item.name);
			symbol.set("kind", symbol_kind(p_item.kind, p_item.is_const, p_item.is_constructor));
			symbol.set("range", server.range(unit, p_item.offset, p_item.end - p_item.offset));
			symbol.set("selectionRange", server.range(unit, p_item.name_offset, p_item.name_length));
			if (!p_item.children.empty()) {
				Json children = Json::array();
				for (const OutlineItem &child : p_item.children) {
					children.push((*this)(child));
				}
				symbol.set("children", std::move(children));
			}
			return symbol;
		}
	};
	Convert convert{ *this, *unit };
	for (const OutlineItem &item : workspace_.queries().outline(*unit)) {
		list.push(convert(item));
	}
	return list;
}

Json Server::workspace_symbols(const Json &p_params) {
	Json list = Json::array();
	for (const FoundSymbol &found : workspace_.queries().find(p_params.get("query").string(), 200)) {
		Place place;
		place.unit = found.unit;
		place.offset = found.offset;
		place.length = found.length;
		Json where = location(place);
		if (where.is_null()) {
			continue;
		}
		Json symbol = Json::object();
		symbol.set("name", found.name);
		symbol.set("kind", symbol_kind(found.kind, found.is_const, found.is_constructor));
		symbol.set("location", std::move(where));
		if (!found.container.empty()) {
			symbol.set("containerName", found.container);
		}
		list.push(std::move(symbol));
	}
	return list;
}

} // namespace kas3
