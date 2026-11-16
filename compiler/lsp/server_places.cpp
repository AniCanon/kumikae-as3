// Places in the editor's terms: a file's URI, and a position or a range in lines and characters.
#include "server.h"

#include "positions.h"
#include "uri.h"

namespace kas3 {

std::string Server::uri_of(const Unit &p_unit) {
	std::string path = workspace_.real_path(p_unit);
	if (path.empty()) {
		return std::string();
	}
	auto known = uris_.find(path);
	return known != uris_.end() ? known->second : uri_from_path(workspace_.shown_path(path));
}

Unit *Server::unit_of(const Json &p_params) {
	std::string path = path_from_uri(p_params.get("textDocument").get("uri").string());
	return path.empty() ? nullptr : workspace_.unit(path);
}

Json Server::position(const Unit &p_unit, uint32_t p_offset) const {
	Position at = p_unit.source.position(p_offset);
	std::string_view line = p_unit.source.line_text(at.line);
	int character = character_at(line, (size_t)(at.column - 1), utf8_);
	if (at.line == 1) {
		character += mark_width(p_unit.source);
	}
	return Json::object().set("line", at.line - 1).set("character", character);
}

Json Server::range(const Unit &p_unit, uint32_t p_offset, uint32_t p_length) const {
	return Json::object().set("start", position(p_unit, p_offset)).set("end", position(p_unit, p_offset + p_length));
}

Json Server::location(const Place &p_place) {
	if (!p_place.unit) {
		return Json();
	}
	std::string uri = uri_of(*p_place.unit);
	if (uri.empty()) {
		return Json();
	}
	return Json::object().set("uri", uri).set("range", range(*p_place.unit, p_place.offset, p_place.length));
}

uint32_t Server::offset_at(const Unit &p_unit, const Json &p_position) const {
	return offset_in(p_unit.source, p_position);
}

uint32_t Server::offset_in(const Source &p_source, const Json &p_position) const {
	int64_t line = p_position.get("line").integer(-1);
	int64_t character = p_position.get("character").integer(0);
	const Source &source = p_source;
	if (line < 0) {
		return 0;
	}
	if (line >= source.line_count()) {
		return (uint32_t)source.text().size();
	}
	if (line == 0) {
		character -= mark_width(source);
	}
	return source.line_start((int)line + 1) + bytes_before(source.line_text((int)line + 1), character, utf8_);
}

// What the editor counts before the first line's text: the byte-order mark it sent, if it sent one. A
// file it has not opened is counted without: an editor shows a file from disk without its mark.
int Server::mark_width(const Source &p_source) const {
	if (!p_source.has_mark() || !workspace_.sent_with_mark(p_source.path())) {
		return 0;
	}
	return utf8_ ? (int)BYTE_ORDER_MARK.size() : 1;
}

const Occurrence *Server::occurrence_at(const Json &p_params, Unit **r_unit) {
	Unit *unit = unit_of(p_params);
	if (!unit || !p_params.get("position").is_object()) {
		return nullptr;
	}
	*r_unit = unit;
	return workspace_.queries().at(*unit, offset_at(*unit, p_params.get("position")));
}

} // namespace kas3
