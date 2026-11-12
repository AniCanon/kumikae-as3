#include "check.h"
#include "diagnostics.h"
#include "lexer.h"
#include "source.h"

#include <string>
#include <vector>

using namespace kas3;

struct Lexed {
	std::vector<std::string> kinds;
	std::vector<std::string> texts;
	std::vector<bool> newline_before;
	std::string errors;
};

static Lexed lex(const std::string &p_text) {
	Source source("t.as", p_text);
	Diagnostics diagnostics;
	Lexer lexer(source, diagnostics);
	Lexed out;
	for (;;) {
		Token token = lexer.next();
		if (token.kind == TokenKind::EndOfFile) {
			break;
		}
		out.kinds.push_back(token_kind_name(token.kind));
		out.texts.push_back(std::string(lexer.text(token)));
		out.newline_before.push_back(token.newline_before);
	}
	out.errors = diagnostics.format(source);
	return out;
}

static std::string join(const std::vector<std::string> &p_items) {
	std::string out;
	for (const std::string &item : p_items) {
		out += out.empty() ? "" : " ";
		out += item;
	}
	return out;
}

static void punctuation_and_operators() {
	const char *all = "( ) { } [ ] ; , . .< ... .. : :: ? @ + - * / % ++ -- = += -= *= /= %= == != === !== < > <= >= "
					  "<< >> >>> <<= >>= >>>= & | ^ ~ ! && || &= |= ^= &&= ||=";
	const char *names = "'(' ')' '{' '}' '[' ']' ';' ',' '.' '.<' '...' '..' ':' '::' '?' '@' '+' '-' '*' '/' '%' "
						"'++' '--' '=' '+=' '-=' '*=' '/=' '%=' '==' '!=' '===' '!==' '<' '>' '<=' '>=' '<<' '>>' "
						"'>>>' '<<=' '>>=' '>>>=' '&' '|' '^' '~' '!' '&&' '||' '&=' '|=' '^=' '&&=' '||='";
	Lexed result = lex(all);
	CHECK_EQ(join(result.kinds), std::string(names));
	CHECK_EQ(result.errors, std::string(""));
}

static void longest_match() {
	CHECK_EQ(join(lex("a>>>=b").kinds), std::string("identifier '>>>=' identifier"));
	CHECK_EQ(join(lex("a.<b").kinds), std::string("identifier '.<' identifier"));
	CHECK_EQ(join(lex("a...b").kinds), std::string("identifier '...' identifier"));
	CHECK_EQ(join(lex("a..b").kinds), std::string("identifier '..' identifier"));
}

static void keywords_and_contextual_words() {
	CHECK_EQ(join(lex("class get set each static override").kinds),
			std::string("'class' identifier identifier identifier identifier identifier"));
	CHECK_EQ(join(lex("instanceof classy In").kinds), std::string("'instanceof' identifier identifier"));
	CHECK(is_reserved_word(TokenKind::KwClass));
	CHECK(!is_reserved_word(TokenKind::Identifier));
	CHECK(is_identifier_like(TokenKind::KwDefault));
	CHECK(is_identifier_like(TokenKind::Identifier));
	CHECK(!is_identifier_like(TokenKind::Number));
}

static void numbers() {
	Lexed result = lex("0 12 1.5 .5 1e3 1.5e-3 0x1F");
	CHECK_EQ(join(result.kinds), std::string("number number number number number number number"));
	CHECK_EQ(join(result.texts), std::string("0 12 1.5 .5 1e3 1.5e-3 0x1F"));
	Lexed member = lex("1.toString");
	CHECK_EQ(join(member.kinds), std::string("number '.' identifier"));
	CHECK_EQ(member.texts[0], std::string("1"));
	CHECK_EQ(join(lex("1.e3").kinds), std::string("number '.' identifier"));
	CHECK_EQ(join(lex("1e+2 2E5").texts), std::string("1e+2 2E5"));
}

static void invalid_number() {
	Lexed result = lex("12abc");
	CHECK_EQ(join(result.kinds), std::string("error"));
	CHECK_EQ(result.texts[0], std::string("12abc"));
	CHECK_EQ(result.errors, std::string("t.as:1:1: error: invalid number\n"));
	CHECK_EQ(lex("0x").errors, std::string("t.as:1:1: error: invalid number\n"));
	CHECK_EQ(lex("1e").errors, std::string("t.as:1:1: error: invalid number\n"));
}

static void strings() {
	Lexed result = lex("\"a\\\"b\" 'it''s'");
	CHECK_EQ(join(result.kinds), std::string("string string string"));
	CHECK_EQ(result.texts[0], std::string("\"a\\\"b\""));
	CHECK_EQ(result.texts[1], std::string("'it'"));
	CHECK_EQ(result.texts[2], std::string("'s'"));
	Lexed continued = lex("\"a\\\nb\"");
	CHECK_EQ(join(continued.kinds), std::string("string"));
	CHECK_EQ(continued.errors, std::string(""));
}

static void unterminated_string() {
	Lexed result = lex("\"abc\nx");
	CHECK_EQ(join(result.kinds), std::string("error identifier"));
	CHECK_EQ(result.errors, std::string("t.as:1:1: error: unterminated string\n"));
	CHECK_EQ(lex("'abc").errors, std::string("t.as:1:1: error: unterminated string\n"));
}

static void comments_are_skipped() {
	Lexed result = lex("a // c\n/* d */ b");
	CHECK_EQ(join(result.kinds), std::string("identifier identifier"));
	CHECK(result.newline_before[1]);
	CHECK_EQ(result.errors, std::string(""));
}

static void unterminated_comment() {
	Lexed result = lex("a /* b");
	CHECK_EQ(join(result.kinds), std::string("identifier error"));
	CHECK_EQ(result.errors, std::string("t.as:1:3: error: unterminated comment\n"));
}

static void newline_before() {
	Lexed result = lex("a\nb c");
	CHECK(!result.newline_before[0]);
	CHECK(result.newline_before[1]);
	CHECK(!result.newline_before[2]);
	Lexed in_comment = lex("a /* x\n y */ b");
	CHECK(in_comment.newline_before[1]);
	CHECK(lex("a\r\nb").newline_before[1]);
}

static void regex_rescan() {
	Source source("t.as", "/a[/]b/gi");
	Diagnostics diagnostics;
	Lexer lexer(source, diagnostics);
	Token slash = lexer.next();
	CHECK_EQ(std::string(token_kind_name(slash.kind)), std::string("'/'"));
	Token regex = lexer.rescan_as_regex(slash);
	CHECK_EQ(std::string(token_kind_name(regex.kind)), std::string("regular expression"));
	CHECK_EQ(std::string(lexer.text(regex)), std::string("/a[/]b/gi"));
	CHECK(lexer.next().kind == TokenKind::EndOfFile);
	CHECK(!diagnostics.has_errors());
}

static void regex_rescan_rewinds_past_later_tokens() {
	// The parser may already have looked one token past the slash.
	Source source("t.as", "/\"/ + 1");
	Diagnostics diagnostics;
	Lexer lexer(source, diagnostics);
	Token slash = lexer.next();
	Token regex = lexer.rescan_as_regex(slash);
	CHECK_EQ(std::string(lexer.text(regex)), std::string("/\"/"));
	CHECK_EQ(std::string(token_kind_name(lexer.next().kind)), std::string("'+'"));
	CHECK(!diagnostics.has_errors());
	Source assign_source("t.as", "/=/ ");
	Diagnostics assign_diagnostics;
	Lexer assign_lexer(assign_source, assign_diagnostics);
	Token slash_assign = assign_lexer.next();
	CHECK(slash_assign.kind == TokenKind::SlashAssign);
	CHECK_EQ(std::string(assign_lexer.text(assign_lexer.rescan_as_regex(slash_assign))), std::string("/=/"));
}

static void unterminated_regex() {
	Source source("t.as", "/abc\nx");
	Diagnostics diagnostics;
	Lexer lexer(source, diagnostics);
	Token regex = lexer.rescan_as_regex(lexer.next());
	CHECK(regex.kind == TokenKind::Error);
	CHECK_EQ(diagnostics.format(source), std::string("t.as:1:1: error: unterminated regular expression\n"));
	CHECK_EQ(std::string(token_kind_name(lexer.next().kind)), std::string("identifier"));
}

static void non_ascii_identifier() {
	Lexed result = lex("caf\xC3\xA9 = 1");
	CHECK_EQ(join(result.kinds), std::string("identifier '=' number"));
	CHECK_EQ((int)result.texts[0].size(), 5);
	CHECK_EQ(join(lex("$a _b a1").kinds), std::string("identifier identifier identifier"));
}

static void unicode_spaces_are_whitespace() {
	// No-break space, ideographic space, and a byte-order mark in the middle of a file.
	CHECK_EQ(join(lex("x =\xC2\xA0" "1;").kinds), std::string("identifier '=' number ';'"));
	CHECK_EQ(join(lex("a\xE3\x80\x80+ b").kinds), std::string("identifier '+' identifier"));
	CHECK_EQ(join(lex("a \xEF\xBB\xBF b").kinds), std::string("identifier identifier"));
	CHECK_EQ(join(lex("x = 1\xC2\xA0+ 2;").kinds), std::string("identifier '=' number '+' number ';'"));
	CHECK_EQ(lex("x = 1\xC2\xA0+ 2;").errors, std::string(""));
	// Line and paragraph separators end a line.
	Lexed separated = lex("a\xE2\x80\xA8" "b\xE2\x80\xA9" "c");
	CHECK_EQ(join(separated.kinds), std::string("identifier identifier identifier"));
	CHECK(separated.newline_before[1]);
	CHECK(separated.newline_before[2]);
	// Other non-ASCII characters are still identifier characters.
	CHECK_EQ((int)lex("\xE6\x97\xA5\xE6\x9C\xAC = 1").texts[0].size(), 6);
}

static void control_character() {
	Lexed result = lex("a \x01 b");
	CHECK_EQ(join(result.kinds), std::string("identifier error identifier"));
	CHECK_EQ(result.errors, std::string("t.as:1:3: error: unexpected character '\\x01'\n"));
	CHECK_EQ(lex("a # b").errors, std::string("t.as:1:3: error: unexpected character '#'\n"));
}

static void end_of_file_repeats() {
	Source source("t.as", "");
	Diagnostics diagnostics;
	Lexer lexer(source, diagnostics);
	CHECK(lexer.next().kind == TokenKind::EndOfFile);
	CHECK(lexer.next().kind == TokenKind::EndOfFile);
	CHECK(lexer.next().kind == TokenKind::EndOfFile);
}

static void offsets_and_lengths() {
	Source source("t.as", "ab  +=\n c");
	Diagnostics diagnostics;
	Lexer lexer(source, diagnostics);
	Token a = lexer.next();
	Token op = lexer.next();
	Token c = lexer.next();
	Token end = lexer.next();
	CHECK_EQ(a.offset, 0u);
	CHECK_EQ(a.length, 2u);
	CHECK_EQ(op.offset, 4u);
	CHECK_EQ(op.length, 2u);
	CHECK_EQ(c.offset, 8u);
	CHECK_EQ(end.offset, 9u);
	CHECK_EQ(end.length, 0u);
}

int main() {
	punctuation_and_operators();
	longest_match();
	keywords_and_contextual_words();
	numbers();
	invalid_number();
	strings();
	unterminated_string();
	comments_are_skipped();
	unterminated_comment();
	newline_before();
	regex_rescan();
	regex_rescan_rewinds_past_later_tokens();
	unterminated_regex();
	non_ascii_identifier();
	unicode_spaces_are_whitespace();
	control_character();
	end_of_file_repeats();
	offsets_and_lengths();
	return check::finish();
}
