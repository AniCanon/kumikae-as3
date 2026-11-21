// ASDoc belongs to the declaration immediately following the comment, including its metadata.
#include "query.h"

#include <algorithm>
#include <iterator>

namespace kas3 {

namespace {

std::string_view comment_line(std::string_view p_line, bool p_first) {
	const auto first = p_line.find_first_not_of(" \t\r");
	if (first == std::string_view::npos) {
		return {};
	}
	auto line = p_line.substr(first, p_line.find_last_not_of(" \t\r") - first + 1);
	if (!p_first && line.front() == '*') {
		line.remove_prefix(1);
		if (!line.empty() && line.front() == ' ') {
			line.remove_prefix(1);
		}
	}
	return line;
}

bool only_space(std::string_view p_text) {
	while (!p_text.empty()) {
		const auto offset = p_text.find_first_not_of(" \t\r\n\v\f");
		if (offset == std::string_view::npos) {
			return true;
		}
		p_text.remove_prefix(offset);
		bool line_break;
		const auto width = unicode_space(p_text.data(), static_cast<uint32_t>(p_text.size()), &line_break);
		if (!width) {
			return false;
		}
		p_text.remove_prefix(width);
	}
	return true;
}

std::string preceding_asdoc(const Unit &p_unit, const Node *p_node) {
	if (!p_node) {
		return {};
	}
	const auto &regions = p_unit.parsed->text_regions;
	const auto next = std::lower_bound(regions.begin(), regions.end(), p_node->offset,
			[](const TextRegion &p_region, uint32_t p_at) { return p_region.begin < p_at; });
	if (next == regions.begin()) {
		return {};
	}
	const auto &region = *std::prev(next);
	std::string_view source = p_unit.source.text();
	if (!region.closed || region.end > p_node->offset || region.end - region.begin < 5 ||
			source.substr(region.begin, 3) != "/**" ||
			!only_space(source.substr(region.end, p_node->offset - region.end))) {
		return {};
	}
	auto body = source.substr(region.begin + 3, region.end - region.begin - 5);
	std::string result;
	bool first = true;
	while (!body.empty()) {
		const auto end = body.find_first_of("\r\n");
		auto line = comment_line(body.substr(0, end), first);
		first = false;
		if (!result.empty()) {
			result += '\n';
		}
		result.append(line);
		if (end == std::string_view::npos) {
			break;
		}
		const bool crlf = body.substr(end, 2) == "\r\n";
		body.remove_prefix(end + (crlf ? 2 : 1));
	}
	result.erase(result.find_last_not_of('\n') + 1);
	return result;
}

} // namespace

std::string Queries::documentation(const Symbol *p_symbol) const {
	if (!p_symbol || !p_symbol->unit) {
		return {};
	}
	if (p_symbol->kind == SymbolKind::Property) {
		auto doc = preceding_asdoc(*p_symbol->unit, p_symbol->getter);
		return doc.empty() ? preceding_asdoc(*p_symbol->unit, p_symbol->setter) : doc;
	}
	return preceding_asdoc(*p_symbol->unit, p_symbol->node);
}

} // namespace kas3
