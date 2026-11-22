#include "script_editor.h"

#if !defined(IOS_ENABLED) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
#include "completion_keywords.h"
#include "language.h"
#include "lexer.h"
#include "names.h"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/text_edit.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <array>

using namespace godot;
using namespace kas3;

namespace {

enum class Style { Text, Keyword, ControlFlow, Number, String, Comment, DocComment, Symbol, Count };

struct ColoredSpan {
	uint32_t begin;
	uint32_t end;
	Style style;
};

bool contextual_keyword(std::string_view p_word) {
	using namespace completion_keywords;
	const auto matches = [p_word](const Keyword &p_keyword) { return p_keyword.name == p_word; };
	return std::any_of(std::begin(IN_CLASS), std::end(IN_CLASS), matches) ||
			std::any_of(std::begin(IN_FILE), std::end(IN_FILE), matches) || p_word == "each";
}

Style token_style(const Token &p_token, std::string_view p_spelling) {
	switch (p_token.kind) {
		case TokenKind::KwBreak:
		case TokenKind::KwCase:
		case TokenKind::KwCatch:
		case TokenKind::KwContinue:
		case TokenKind::KwDefault:
		case TokenKind::KwDo:
		case TokenKind::KwElse:
		case TokenKind::KwFinally:
		case TokenKind::KwFor:
		case TokenKind::KwIf:
		case TokenKind::KwReturn:
		case TokenKind::KwSwitch:
		case TokenKind::KwThrow:
		case TokenKind::KwTry:
		case TokenKind::KwWhile:
			return Style::ControlFlow;
		case TokenKind::Identifier:
			return contextual_keyword(p_spelling) ? Style::Keyword : Style::Text;
		case TokenKind::Number:
			return Style::Number;
		case TokenKind::String:
		case TokenKind::Regex:
		case TokenKind::Error:
			return Style::Text; // Literal and comment regions include incomplete tokens.
		default:
			return is_reserved_word(p_token.kind) ? Style::Keyword : Style::Symbol;
	}
}

bool regex_at(const Parsed &p_parsed, uint32_t p_offset) {
	const auto &regions = p_parsed.text_regions;
	const auto region = std::lower_bound(regions.begin(), regions.end(), p_offset,
			[](const TextRegion &p_region, uint32_t p_byte) { return p_region.begin < p_byte; });
	return region != regions.end() && region->begin == p_offset;
}

std::vector<ColoredSpan> spans(const Source &p_source) {
	Diagnostics diagnostics;
	std::vector<TextRegion> text;
	Lexer lexer(p_source, diagnostics, &text);
	std::vector<ColoredSpan> result;
	std::shared_ptr<Parsed> parsed;
	for (Token token = lexer.next(); token.kind != TokenKind::EndOfFile; token = lexer.next()) {
		if (token.kind == TokenKind::Slash || token.kind == TokenKind::SlashAssign) {
			// The parser distinguishes regular expressions from division.
			if (!parsed) {
				parsed = parse_file(p_source.path(), p_source.text());
			}
			if (regex_at(*parsed, token.offset)) {
				token = lexer.rescan_as_regex(token);
			}
		}
		const Style style = token_style(token, lexer.text(token));
		if (style != Style::Text) {
			result.push_back({ token.offset, token.offset + token.length, style });
		}
	}
	for (const TextRegion &region : text) {
		const std::string_view marker = std::string_view(p_source.text()).substr(region.begin, 3);
		const bool comment = marker.substr(0, 2) == "//" || marker.substr(0, 2) == "/*";
		const Style style = marker == "/**" ? Style::DocComment : comment ? Style::Comment : Style::String;
		result.push_back({ region.begin, region.end, style });
	}
	std::sort(result.begin(), result.end(), [](const ColoredSpan &p_left, const ColoredSpan &p_right) {
		return p_left.begin < p_right.begin;
	});
	return result;
}

std::array<Color, static_cast<size_t>(Style::Count)> colors() {
	const auto settings = EditorInterface::get_singleton()->get_editor_settings();
	const auto color = [&settings](const char *p_name) -> Color {
		return settings->get_setting(String("text_editor/theme/highlighting/") + p_name);
	};
	return { color("text_color"), color("keyword_color"), color("control_flow_keyword_color"),
		color("number_color"), color("string_color"), color("comment_color"), color("doc_comment_color"),
		color("symbol_color") };
}

} // namespace

String AS3SyntaxHighlighter::_get_name() const {
	return AS3Language::NAME;
}

PackedStringArray AS3SyntaxHighlighter::_get_supported_languages() const {
	return PackedStringArray({ _get_name() });
}

Ref<EditorSyntaxHighlighter> AS3SyntaxHighlighter::_create() const {
	Ref<AS3SyntaxHighlighter> highlighter;
	highlighter.instantiate();
	return highlighter;
}

void AS3SyntaxHighlighter::_update_cache() {
	dirty_ = true;
	TextEdit *editor = get_text_edit();
	const Callable edited = callable_mp(this, &AS3SyntaxHighlighter::text_edited);
	if (!editor->is_connected("lines_edited_from", edited)) {
		editor->connect("lines_edited_from", edited);
	}
}

void AS3SyntaxHighlighter::rebuild() const {
	const CharString utf8 = get_text_edit()->get_text().utf8();
	const Source source("", std::string(utf8.get_data(), static_cast<size_t>(utf8.length())));
	lines_.clear();
	lines_.resize(static_cast<size_t>(source.line_count()));
	const auto palette = colors();
	uint32_t byte = 0;
	size_t line = 0;
	int column = source.has_mark() ? 1 : 0;
	const auto mark = [&](Style p_style) {
		auto &entries = lines_[line];
		const Color &color = palette[static_cast<size_t>(p_style)];
		if (!entries.empty() && entries.back().first == column) {
			entries.back().second = color;
		} else {
			entries.emplace_back(column, color);
		}
	};
	const auto advance = [&](uint32_t p_end, Style p_style) {
		for (; byte < p_end; ++byte) {
			const auto ch = static_cast<unsigned char>(source.text()[byte]);
			if (ch == '\n') {
				++line;
				column = 0;
				if (p_style != Style::Text) {
					mark(p_style);
				}
			} else if ((ch & 0xc0) != 0x80) {
				++column; // Godot indexes characters; the lexer indexes UTF-8 bytes.
			}
		}
	};
	for (const ColoredSpan &span : spans(source)) {
		advance(span.begin, Style::Text);
		mark(span.style);
		advance(span.end, span.style);
		mark(Style::Text);
	}
	dirty_ = false;
}

Dictionary AS3SyntaxHighlighter::_get_line_syntax_highlighting(int32_t p_line) const {
	if (dirty_) {
		rebuild();
	}
	Dictionary result;
	if (p_line >= 0 && static_cast<size_t>(p_line) < lines_.size()) {
		for (const auto &[column, color] : lines_[static_cast<size_t>(p_line)]) {
			Dictionary entry;
			entry["color"] = color;
			result[column] = entry;
		}
	}
	return result;
}
#endif
