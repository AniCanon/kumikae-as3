#include "source.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace kas3 {

Source::Data::Data(std::string p_path, std::string p_text) :
		path(std::move(p_path)), text(std::move(p_text)) {
	if (text.compare(0, BYTE_ORDER_MARK.size(), BYTE_ORDER_MARK) == 0) {
		text.erase(0, BYTE_ORDER_MARK.size());
		has_mark = true;
	}
	// "\n", "\r\n" and "\r" each end one line.
	line_starts.push_back(0);
	for (size_t i = 0; i < text.size(); i++) {
		char c = text[i];
		if (c == '\r') {
			if (i + 1 < text.size() && text[i + 1] == '\n') {
				i++;
			}
			line_starts.push_back((uint32_t)(i + 1));
		} else if (c == '\n') {
			line_starts.push_back((uint32_t)(i + 1));
		}
	}
}

Source::Source(std::string p_path, std::string p_text) :
		data_(std::make_shared<Data>(std::move(p_path), std::move(p_text))) {}

Position Source::position(uint32_t p_offset) const {
	uint32_t offset = std::min<uint32_t>(p_offset, (uint32_t)data_->text.size());
	auto after = std::upper_bound(data_->line_starts.begin(), data_->line_starts.end(), offset);
	int line = (int)(after - data_->line_starts.begin());
	return Position{ line, (int)(offset - data_->line_starts[(size_t)line - 1]) + 1 };
}

std::string_view Source::line_text(int p_line) const {
	if (p_line < 1 || p_line > line_count()) {
		return std::string_view();
	}
	uint32_t start = data_->line_starts[(size_t)p_line - 1];
	uint32_t end = p_line < line_count() ? data_->line_starts[(size_t)p_line] : (uint32_t)data_->text.size();
	while (end > start && (data_->text[end - 1] == '\n' || data_->text[end - 1] == '\r')) {
		end--;
	}
	return std::string_view(data_->text).substr(start, end - start);
}

bool read_file(const std::string &p_path, std::string *r_text) {
#ifdef _WIN32
	std::FILE *file = _wfopen(std::filesystem::u8path(p_path).c_str(), L"rb");
#else
	std::FILE *file = std::fopen(p_path.c_str(), "rb");
#endif
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
