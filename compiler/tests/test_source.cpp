#include "check.h"
#include "diagnostics.h"
#include "source.h"

using namespace kas3;

static void position_of_first_byte() {
	Source source("a.as", "ab\ncd");
	CHECK_EQ(source.position(0).line, 1);
	CHECK_EQ(source.position(0).column, 1);
	CHECK_EQ(source.position(3).line, 2);
	CHECK_EQ(source.position(3).column, 1);
	CHECK_EQ(source.position(4).line, 2);
	CHECK_EQ(source.position(4).column, 2);
}

static void line_endings() {
	Source source("a.as", "a\r\nb\rc\nd");
	CHECK_EQ(source.line_count(), 4);
	CHECK_EQ(source.position(7).line, 4);
	CHECK_EQ(source.position(7).column, 1);
	CHECK_EQ(source.line_text(1), std::string_view("a"));
	CHECK_EQ(source.line_text(2), std::string_view("b"));
	CHECK_EQ(source.line_text(4), std::string_view("d"));
	CHECK_EQ(source.line_text(9), std::string_view(""));
}

static void byte_order_mark_is_skipped() {
	Source source("a.as", "\xEF\xBB\xBFx");
	CHECK_EQ(source.text(), std::string("x"));
	CHECK_EQ(source.position(0).line, 1);
	CHECK_EQ(source.position(0).column, 1);
}

static void offset_past_end_clamps() {
	Source source("a.as", "ab");
	CHECK_EQ(source.position(99).line, 1);
	CHECK_EQ(source.position(99).column, 3);
}

static void empty_source() {
	Source source("a.as", "");
	CHECK_EQ(source.line_count(), 1);
	CHECK_EQ(source.position(0).line, 1);
	CHECK_EQ(source.position(0).column, 1);
}

static void diagnostic_format() {
	Source source("dir/A.as", "x\n  y");
	Diagnostics diagnostics;
	CHECK(!diagnostics.has_errors());
	diagnostics.error(4, "unexpected 'y'");
	CHECK(diagnostics.has_errors());
	CHECK_EQ(diagnostics.format(source), std::string("dir/A.as:2:3: error: unexpected 'y'\n"));
}

static void diagnostics_are_ordered_by_position() {
	Source source("dir/A.as", "0123456789");
	Diagnostics diagnostics;
	diagnostics.error(9, "late");
	diagnostics.error(2, "early");
	diagnostics.error(2, "early again");
	CHECK_EQ(diagnostics.format(source), std::string("dir/A.as:1:3: error: early\ndir/A.as:1:3: error: early again\n"
													 "dir/A.as:1:10: error: late\n"));
}

static void diagnostic_limit() {
	Source source("dir/A.as", "x");
	Diagnostics diagnostics;
	for (int i = 0; i < 105; i++) {
		diagnostics.error(0, "e");
	}
	CHECK_EQ((int)diagnostics.items().size(), 100);
	CHECK_EQ(diagnostics.count(), 105);
	std::string text = diagnostics.format(source);
	std::string tail = "dir/A.as: error: too many errors (5 more)\n";
	CHECK(text.size() > tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0);
}

// Past the limit, the errors kept are the first in the file, whatever order the passes found them in.
static void diagnostic_limit_keeps_the_first() {
	Source source("dir/A.as", std::string(300, 'x'));
	Diagnostics diagnostics;
	for (int i = 0; i < 150; i++) {
		diagnostics.error((uint32_t)(149 - i), "e" + std::to_string(149 - i));
	}
	diagnostics.warning(5, "w");
	diagnostics.error(200, "late");
	CHECK_EQ((int)diagnostics.items().size(), 100);
	CHECK_EQ(diagnostics.count(), 151);
	CHECK_EQ(diagnostics.dropped(), 52);
	CHECK_EQ(diagnostics.items().front().message, std::string("e0"));
	CHECK_EQ(diagnostics.items()[6].message, std::string("w"));
	CHECK_EQ(diagnostics.items().back().message, std::string("e98"));
}

static void read_missing_file_fails() {
	std::string text;
	CHECK(!read_file("/nonexistent/x.as", &text));
}

int main() {
	position_of_first_byte();
	line_endings();
	byte_order_mark_is_skipped();
	offset_past_end_clamps();
	empty_source();
	diagnostic_format();
	diagnostics_are_ordered_by_position();
	diagnostic_limit();
	diagnostic_limit_keeps_the_first();
	read_missing_file_fails();
	return check::finish();
}
