// Source text and the mapping from byte offsets to lines and columns.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kas3 {

// Both are 1-based. A column counts bytes from the start of the line, plus one.
struct Position {
	int line;
	int column;
};

class Source {
	std::string path_;
	std::string text_;
	std::vector<uint32_t> line_starts_;

public:
	// A leading UTF-8 byte-order mark is skipped.
	Source(std::string p_path, std::string p_text);

	const std::string &path() const { return path_; }
	const std::string &text() const { return text_; }
	// Offsets past the end clamp to the end.
	Position position(uint32_t p_offset) const;
	// The line without its line break; empty if the line number is out of range.
	std::string_view line_text(int p_line) const;
	int line_count() const { return (int)line_starts_.size(); }
	// Where the line begins; the end of the text if the line number is out of range.
	uint32_t line_start(int p_line) const {
		return p_line >= 1 && p_line <= line_count() ? line_starts_[(size_t)p_line - 1] : (uint32_t)text_.size();
	}
};

// False if the file cannot be read.
bool read_file(const std::string &p_path, std::string *r_text);

} // namespace kas3
