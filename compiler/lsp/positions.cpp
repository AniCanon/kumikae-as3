#include "positions.h"

namespace kas3 {

namespace {

bool continues(char p_c) {
	return ((unsigned char)p_c & 0xC0) == 0x80;
}

} // namespace

int character_at(std::string_view p_line, size_t p_bytes, bool p_utf8) {
	size_t bytes = p_bytes < p_line.size() ? p_bytes : p_line.size();
	if (p_utf8) {
		return (int)bytes;
	}
	int units = 0;
	for (unsigned char c : p_line.substr(0, bytes)) {
		if ((c & 0xC0) != 0x80) {
			units += c >= 0xF0 ? 2 : 1;
		}
	}
	return units;
}

uint32_t bytes_before(std::string_view p_line, int64_t p_character, bool p_utf8) {
	uint32_t at = 0;
	int64_t counted = 0;
	while (at < p_line.size() && counted < p_character) {
		uint32_t start = at;
		unsigned char c = (unsigned char)p_line[at];
		at++;
		while (at < p_line.size() && continues(p_line[at])) {
			at++;
		}
		counted += p_utf8 ? at - start : c >= 0xF0 ? 2 : 1;
	}
	return at;
}

uint32_t offset_in_text(std::string_view p_text, int64_t p_line, int64_t p_character, bool p_utf8) {
	if (p_line < 0) {
		return 0;
	}
	size_t start = 0;
	for (int64_t line = 0; line < p_line; line++) {
		size_t end = p_text.find_first_of("\r\n", start);
		if (end == std::string_view::npos) {
			return (uint32_t)p_text.size();
		}
		start = end + (p_text[end] == '\r' && end + 1 < p_text.size() && p_text[end + 1] == '\n' ? 2 : 1);
	}
	size_t end = p_text.find_first_of("\r\n", start);
	std::string_view line = p_text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
	return (uint32_t)start + bytes_before(line, p_character, p_utf8);
}

} // namespace kas3
