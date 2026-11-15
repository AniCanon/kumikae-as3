#include "source.h"

#include <algorithm>
#include <cstdio>

namespace kas3 {

Source::Source(std::string p_path, std::string p_text) :
		path_(std::move(p_path)), text_(std::move(p_text)) {
	if (text_.compare(0, BYTE_ORDER_MARK.size(), BYTE_ORDER_MARK) == 0) {
		text_.erase(0, BYTE_ORDER_MARK.size());
		has_mark_ = true;
	}
	// "\n", "\r\n" and "\r" each end one line.
	line_starts_.push_back(0);
	for (size_t i = 0; i < text_.size(); i++) {
		char c = text_[i];
		if (c == '\r') {
			if (i + 1 < text_.size() && text_[i + 1] == '\n') {
				i++;
			}
			line_starts_.push_back((uint32_t)(i + 1));
		} else if (c == '\n') {
			line_starts_.push_back((uint32_t)(i + 1));
		}
	}
}

Position Source::position(uint32_t p_offset) const {
	uint32_t offset = std::min<uint32_t>(p_offset, (uint32_t)text_.size());
	auto after = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
	int line = (int)(after - line_starts_.begin());
	return Position{ line, (int)(offset - line_starts_[(size_t)line - 1]) + 1 };
}

std::string_view Source::line_text(int p_line) const {
	if (p_line < 1 || p_line > line_count()) {
		return std::string_view();
	}
	uint32_t start = line_starts_[(size_t)p_line - 1];
	uint32_t end = p_line < line_count() ? line_starts_[(size_t)p_line] : (uint32_t)text_.size();
	while (end > start && (text_[end - 1] == '\n' || text_[end - 1] == '\r')) {
		end--;
	}
	return std::string_view(text_).substr(start, end - start);
}

bool read_file(const std::string &p_path, std::string *r_text) {
	std::FILE *file = std::fopen(p_path.c_str(), "rb");
	if (!file) {
		return false;
	}
	r_text->clear();
	char buffer[65536];
	size_t got;
	while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
		r_text->append(buffer, got);
	}
	bool ok = !std::ferror(file);
	std::fclose(file);
	return ok;
}

} // namespace kas3
