// Source text and the mapping from byte offsets to lines and columns.
#pragma once

#include <cstdint>
#include <memory>
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
	struct Data {
		std::string path;
		std::string text;
		std::vector<uint32_t> line_starts;
		Data(std::string p_path, std::string p_text);
	};
	std::shared_ptr<const Data> data_; // Immutable source snapshots can share text and line indexes.

public:
	// A leading UTF-8 byte-order mark is skipped.
	Source(std::string p_path, std::string p_text);

	const std::string &path() const { return data_->path; }
	const std::string &text() const { return data_->text; }
	// Offsets past the end clamp to the end.
	Position position(uint32_t p_offset) const;
	// The line without its line break; empty if the line number is out of range.
	std::string_view line_text(int p_line) const;
	int line_count() const { return (int)data_->line_starts.size(); }
	// Where the line begins; the end of the text if the line number is out of range.
	uint32_t line_start(int p_line) const {
		return p_line >= 1 && p_line <= line_count() ? data_->line_starts[(size_t)p_line - 1] : (uint32_t)data_->text.size();
	}
};

// False if the file cannot be read.
bool read_file(const std::string &p_path, std::string *r_text);

} // namespace kas3
