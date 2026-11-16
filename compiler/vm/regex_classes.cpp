#include "regex_tree.h"

#include <array>

namespace kas3 {
namespace vm {
namespace regex {

namespace {

using Range = std::pair<char16_t, char16_t>;

constexpr std::array<Range, 1> DIGITS = { { { u'0', u'9' } } };
constexpr std::array<Range, 4> WORD = { { { u'0', u'9' }, { u'A', u'Z' }, { u'_', u'_' }, { u'a', u'z' } } };
// ECMAScript's white space and line breaks: the same characters as is_text_space() (vm/number_text.h),
// which test_runtime checks unit by unit.
constexpr std::array<Range, 9> SPACE = { { { 0x09, 0x0D }, { 0x20, 0x20 }, { 0xA0, 0xA0 }, { 0x1680, 0x1680 }, { 0x2000, 0x200A },
		{ 0x2028, 0x2029 }, { 0x202F, 0x202F }, { 0x205F, 0x205F }, { 0x3000, 0x3000 } } };

} // namespace

std::vector<Range> class_ranges(char16_t p_letter) {
	std::vector<Range> ranges;
	const char16_t lower = static_cast<char16_t>(p_letter | 0x20);
	if (lower == u'd') {
		ranges.assign(DIGITS.begin(), DIGITS.end());
	} else if (lower == u'w') {
		ranges.assign(WORD.begin(), WORD.end());
	} else {
		ranges.assign(SPACE.begin(), SPACE.end());
	}
	if (lower == p_letter) {
		return ranges;
	}
	std::vector<Range> others;
	uint32_t next = 0;
	for (const auto &[from, to] : ranges) {
		if (from > next) {
			others.push_back({ static_cast<char16_t>(next), static_cast<char16_t>(from - 1) });
		}
		next = to + 1u;
	}
	if (next <= 0xFFFF) {
		others.push_back({ static_cast<char16_t>(next), u'\xFFFF' });
	}
	return others;
}

} // namespace regex
} // namespace vm
} // namespace kas3
