#include "query.h"

#include <algorithm>
#include <cctype>

namespace kas3 {

namespace {

bool comment_at(std::string_view p_text, uint32_t p_offset) {
	return p_offset + 1 < p_text.size() && p_text[p_offset] == '/' &&
			(p_text[p_offset + 1] == '/' || p_text[p_offset + 1] == '*');
}

bool after_parenthesis(const Unit &p_unit, uint32_t p_offset) {
	const auto &text = p_unit.source.text();
	const auto &regions = p_unit.parsed->text_regions;
	while (p_offset) {
		if (std::isspace(static_cast<unsigned char>(text[p_offset - 1]))) {
			--p_offset;
			continue;
		}
		const auto region = std::lower_bound(regions.begin(), regions.end(), p_offset,
				[](const TextRegion &p_region, uint32_t p_at) { return p_region.end < p_at; });
		if (region != regions.end() && region->end == p_offset && comment_at(text, region->begin)) {
			p_offset = region->begin;
		} else {
			return text[p_offset - 1] == ')';
		}
	}
	return false;
}

} // namespace

uint32_t Queries::skip_trivia(const Unit &p_unit, uint32_t p_offset) {
	const auto &text = p_unit.source.text();
	const auto &regions = p_unit.parsed->text_regions;
	auto region = std::lower_bound(regions.begin(), regions.end(), p_offset,
			[](const TextRegion &p_region, uint32_t p_at) { return p_region.begin < p_at; });
	while (p_offset < text.size()) {
		if (std::isspace(static_cast<unsigned char>(text[p_offset]))) {
			++p_offset;
		} else if (region != regions.end() && region->begin == p_offset && comment_at(text, p_offset)) {
			p_offset = region->end;
			++region;
		} else {
			break;
		}
	}
	return p_offset;
}

// In a block: at one of its statements, or in a gap between them where a statement may begin.
bool Queries::block_start(const Unit &p_unit, const Block *p_block, uint32_t p_offset) {
	uint32_t previous = p_block->offset + 1;
	bool inside = false;
	for (const Stmt *statement : p_block->statements) {
		if (statement && statement->offset == p_offset) {
			return true;
		}
		inside |= statement->offset < p_offset && p_offset < statement->end;
		if (statement->end <= p_offset) {
			previous = std::max(previous, statement->end);
		}
	}
	const auto &text = p_unit.source.text();
	const bool boundary = previous <= text.size() && previous > 0 &&
			(previous == p_block->offset + 1 || text[previous - 1] == ';' || text[previous - 1] == '}');
	if (!inside && boundary && p_block->offset < p_offset && p_block->offset < text.size() &&
			text[p_block->offset] == '{' && skip_trivia(p_unit, previous) >= p_offset) {
		return true;
	}
	return false;
}

bool Queries::statement_start(const Unit &p_unit, uint32_t p_offset) const {
	const auto starts = [&](const Stmt *p_statement, bool p_after_header = false) {
		return p_statement && p_statement->offset == p_offset && (!p_after_header || after_parenthesis(p_unit, p_offset));
	};
	for (const Node *node : context_nodes(p_unit)) {
		if (node->offset > p_offset || node->end < p_offset) {
			continue;
		}
		switch (node->kind) {
			case NodeKind::Block:
				if (block_start(p_unit, static_cast<const Block *>(node), p_offset)) {
					return true;
				}
				break;
			case NodeKind::If: {
				const auto *branch = static_cast<const If *>(node);
				if (starts(branch->then_branch, true) || starts(branch->else_branch)) {
					return true;
				}
				break;
			}
			case NodeKind::While:
				if (starts(static_cast<const While *>(node)->body, true)) { return true; }
				break;
			case NodeKind::DoWhile:
				if (starts(static_cast<const DoWhile *>(node)->body)) { return true; }
				break;
			case NodeKind::For:
				if (starts(static_cast<const For *>(node)->body, true)) { return true; }
				break;
			case NodeKind::ForIn:
				if (starts(static_cast<const ForIn *>(node)->body, true)) { return true; }
				break;
			case NodeKind::Labeled:
				if (starts(static_cast<const Labeled *>(node)->body)) { return true; }
				break;
			case NodeKind::Switch:
				for (const auto &clause : static_cast<const Switch *>(node)->cases) {
					for (const Stmt *statement : clause.body) {
						if (starts(statement)) { return true; }
					}
				}
				break;
			default:
				break;
		}
	}
	return false;
}

namespace {

// Source and tab-stop spans share one builder; the transport alone knows snippet syntax.
class StatementText {
	Completion &item_;
	std::string newline_;
	std::string indent_;

public:
	StatementText(Completion &p_item, CompletionRange p_range, std::string p_newline, std::string p_indent) :
			item_(p_item), newline_(std::move(p_newline)), indent_(std::move(p_indent)) {
		item_.edit = CompletionEdit{ p_range.begin, p_range.end, {} };
	}
	StatementText &text(std::string_view p_text) {
		item_.edit->text += p_text;
		return *this;
	}
	StatementText &line(size_t p_depth = 0) { return text(newline_ + indent_ + std::string(p_depth, '\t')); }
	StatementText &stop(uint32_t p_index, std::string_view p_default = {}) {
		const auto begin = static_cast<uint32_t>(item_.edit->text.size());
		text(p_default);
		item_.tab_stops.push_back({ begin, static_cast<uint32_t>(item_.edit->text.size()), p_index });
		return *this;
	}
	StatementText &body(uint32_t p_index) { return line(1).stop(p_index).line().text("}"); }
};

bool has_template(std::string_view p_name) {
	return p_name == "try" || p_name == "if" || p_name == "while" || p_name == "do" ||
			p_name == "for" || p_name == "for each" || p_name == "switch";
}

void write_template(StatementText &p_text, std::string_view p_name) {
	auto &out = p_text;
	out.text(p_name);
	if (p_name == "try") {
		out.text(" {").body(1);
		out.text(" catch (").stop(2, "error").text(":").stop(3, "Error").text(") {");
		out.line(1).stop(4).text("throw ").stop(2, "error").text(";").line().text("}");
	} else if (p_name == "do") {
		out.text(" {").body(1).text(" while (").stop(2, "condition").text(");");
	} else if (p_name == "for") {
		out.text(" (var ").stop(1, "i").text(":int = 0; ").stop(1, "i").text(" < ");
		out.stop(2, "count").text("; ").stop(1, "i").text("++) {").body(3);
	} else if (p_name == "for each") {
		out.text(" (var ").stop(1, "item").text(":* in ").stop(2, "items").text(") {").body(3);
	} else if (p_name == "switch") {
		out.text(" (").stop(1, "value").text(") {").line(1).text("case ").stop(2, "0");
		out.text(":").line(2).stop(3).line(2).text("break;").line(1).text("default:");
		out.line(2).stop(4).line(2).text("break;").line().text("}");
	} else {
		out.text(" (").stop(1, "condition").text(") {").body(2);
	}
	out.stop(0);
}

} // namespace

void Queries::statement_templates(const Unit &p_unit, uint32_t p_offset, std::vector<Completion> &p_items) const {
	if (std::none_of(p_items.begin(), p_items.end(), [](const Completion &p_item) {
		return p_item.is_keyword && has_template(p_item.name);
	})) {
		return;
	}
	const auto span = completion_range(p_unit, p_offset);
	if (!statement_start(p_unit, span.begin)) {
		return;
	}
	const auto after = skip_trivia(p_unit, span.end);
	const auto &source = p_unit.source.text();
	if (after < source.size() && (source[after] == '(' || source[after] == '{')) {
		return;
	}
	const auto newline = line_break(p_unit);
	const auto indent = line_indent(p_unit, p_offset);
	for (auto &item : p_items) {
		if (item.is_keyword && has_template(item.name)) {
			StatementText text(item, span, newline, indent);
			write_template(text, item.name);
			item.detail = item.name == "try" ? "try / catch block" : item.name + " block";
		}
	}
}

} // namespace kas3
