#include "positions.h"

namespace kas3 {

int utf16_units(std::string_view p_text) {
	int units = 0;
	for (unsigned char c : p_text) {
		if ((c & 0xC0) != 0x80) {
			units += c >= 0xF0 ? 2 : 1;
		}
	}
	return units;
}

uint32_t bytes_for_units(std::string_view p_line, int64_t p_units) {
	uint32_t at = 0;
	int64_t units = 0;
	while (at < p_line.size() && units < p_units) {
		unsigned char c = (unsigned char)p_line[at];
		units += c >= 0xF0 ? 2 : 1;
		at++;
		while (at < p_line.size() && ((unsigned char)p_line[at] & 0xC0) == 0x80) {
			at++;
		}
	}
	return at;
}

} // namespace kas3
