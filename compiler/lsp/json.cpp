#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kas3 {

namespace {

const Json NULL_JSON;
const std::string EMPTY_STRING;
const int MAX_DEPTH = 200;

void append_utf8(std::string &r_out, uint32_t p_code) {
	if (p_code < 0x80) {
		r_out += (char)p_code;
	} else if (p_code < 0x800) {
		r_out += (char)(0xC0 | (p_code >> 6));
		r_out += (char)(0x80 | (p_code & 0x3F));
	} else if (p_code < 0x10000) {
		r_out += (char)(0xE0 | (p_code >> 12));
		r_out += (char)(0x80 | ((p_code >> 6) & 0x3F));
		r_out += (char)(0x80 | (p_code & 0x3F));
	} else {
		r_out += (char)(0xF0 | (p_code >> 18));
		r_out += (char)(0x80 | ((p_code >> 12) & 0x3F));
		r_out += (char)(0x80 | ((p_code >> 6) & 0x3F));
		r_out += (char)(0x80 | (p_code & 0x3F));
	}
}

class Reader {
	std::string_view text_;
	size_t at_ = 0;

	void skip_space() {
		while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
			at_++;
		}
	}
	bool literal(std::string_view p_word) {
		if (text_.compare(at_, p_word.size(), p_word) != 0) {
			return false;
		}
		at_ += p_word.size();
		return true;
	}
	bool hex4(uint32_t *r_code) {
		if (at_ + 4 > text_.size()) {
			return false;
		}
		uint32_t code = 0;
		for (int i = 0; i < 4; i++) {
			char c = text_[at_++];
			code <<= 4;
			if (c >= '0' && c <= '9') {
				code |= (uint32_t)(c - '0');
			} else if (c >= 'a' && c <= 'f') {
				code |= (uint32_t)(c - 'a' + 10);
			} else if (c >= 'A' && c <= 'F') {
				code |= (uint32_t)(c - 'A' + 10);
			} else {
				return false;
			}
		}
		*r_code = code;
		return true;
	}
	bool string(std::string *r_out) {
		at_++; // The opening quote.
		while (at_ < text_.size()) {
			char c = text_[at_++];
			if (c == '"') {
				return true;
			}
			if (c == '\n' || c == '\r') {
				return false;
			}
			if (c != '\\') {
				*r_out += c;
				continue;
			}
			if (at_ >= text_.size()) {
				return false;
			}
			char escape = text_[at_++];
			switch (escape) {
				case '"':
				case '\\':
				case '/':
					*r_out += escape;
					break;
				case 'b':
					*r_out += '\b';
					break;
				case 'f':
					*r_out += '\f';
					break;
				case 'n':
					*r_out += '\n';
					break;
				case 'r':
					*r_out += '\r';
					break;
				case 't':
					*r_out += '\t';
					break;
				case 'u': {
					uint32_t code = 0;
					if (!hex4(&code)) {
						return false;
					}
					if (code >= 0xD800 && code <= 0xDBFF) {
						// The first half of a pair. Without its second half it stands for nothing.
						uint32_t low = 0;
						size_t back = at_;
						if (text_.compare(at_, 2, "\\u") == 0 && (at_ += 2, hex4(&low)) && low >= 0xDC00 && low <= 0xDFFF) {
							code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
						} else {
							at_ = back;
							code = 0xFFFD;
						}
					} else if (code >= 0xDC00 && code <= 0xDFFF) {
						code = 0xFFFD;
					}
					append_utf8(*r_out, code);
					break;
				}
				default:
					return false;
			}
		}
		return false;
	}
	bool number(Json *r_value) {
		size_t start = at_;
		if (at_ < text_.size() && text_[at_] == '-') {
			at_++;
		}
		auto digits = [&]() {
			size_t before = at_;
			while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
				at_++;
			}
			return at_ - before;
		};
		size_t first = at_;
		size_t whole = digits();
		if (whole == 0 || (whole > 1 && text_[first] == '0')) {
			return false;
		}
		if (at_ < text_.size() && text_[at_] == '.') {
			at_++;
			if (digits() == 0) {
				return false;
			}
		}
		if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
			at_++;
			if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) {
				at_++;
			}
			if (digits() == 0) {
				return false;
			}
		}
		std::string copy(text_.substr(start, at_ - start));
		*r_value = Json(std::strtod(copy.c_str(), nullptr));
		return true;
	}

public:
	explicit Reader(std::string_view p_text) :
			text_(p_text) {}

	bool value(Json *r_value, int p_depth) {
		skip_space();
		if (at_ >= text_.size() || p_depth > MAX_DEPTH) {
			return false;
		}
		char c = text_[at_];
		if (c == '{') {
			at_++;
			*r_value = Json::object();
			skip_space();
			if (at_ < text_.size() && text_[at_] == '}') {
				at_++;
				return true;
			}
			while (true) {
				skip_space();
				if (at_ >= text_.size() || text_[at_] != '"') {
					return false;
				}
				std::string key;
				if (!string(&key)) {
					return false;
				}
				skip_space();
				if (at_ >= text_.size() || text_[at_] != ':') {
					return false;
				}
				at_++;
				Json member;
				if (!value(&member, p_depth + 1)) {
					return false;
				}
				r_value->set(key, std::move(member));
				skip_space();
				if (at_ < text_.size() && text_[at_] == ',') {
					at_++;
					continue;
				}
				if (at_ < text_.size() && text_[at_] == '}') {
					at_++;
					return true;
				}
				return false;
			}
		}
		if (c == '[') {
			at_++;
			*r_value = Json::array();
			skip_space();
			if (at_ < text_.size() && text_[at_] == ']') {
				at_++;
				return true;
			}
			while (true) {
				Json element;
				if (!value(&element, p_depth + 1)) {
					return false;
				}
				r_value->push(std::move(element));
				skip_space();
				if (at_ < text_.size() && text_[at_] == ',') {
					at_++;
					continue;
				}
				if (at_ < text_.size() && text_[at_] == ']') {
					at_++;
					return true;
				}
				return false;
			}
		}
		if (c == '"') {
			std::string text;
			if (!string(&text)) {
				return false;
			}
			*r_value = Json(std::move(text));
			return true;
		}
		if (c == 't') {
			*r_value = Json(true);
			return literal("true");
		}
		if (c == 'f') {
			*r_value = Json(false);
			return literal("false");
		}
		if (c == 'n') {
			*r_value = Json();
			return literal("null");
		}
		return number(r_value);
	}
	bool at_end() {
		skip_space();
		return at_ == text_.size();
	}
};

// The length of the UTF-8 sequence that starts at p_at, or 0 if the bytes there are not one.
size_t utf8_length(const std::string &p_text, size_t p_at) {
	unsigned char lead = (unsigned char)p_text[p_at];
	size_t length = lead < 0x80 ? 1 : lead >= 0xC2 && lead <= 0xDF ? 2 : lead >= 0xE0 && lead <= 0xEF ? 3 :
			lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
	if (length == 0 || p_at + length > p_text.size()) {
		return 0;
	}
	for (size_t i = 1; i < length; i++) {
		if (((unsigned char)p_text[p_at + i] & 0xC0) != 0x80) {
			return 0;
		}
	}
	unsigned char second = length > 1 ? (unsigned char)p_text[p_at + 1] : 0;
	if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second > 0x9F) || (lead == 0xF0 && second < 0x90) ||
			(lead == 0xF4 && second > 0x8F)) {
		return 0; // Overlong, a surrogate, or past U+10FFFF.
	}
	return length;
}

void write_string(std::string &r_out, const std::string &p_text) {
	r_out += '"';
	for (size_t i = 0; i < p_text.size();) {
		unsigned char c = (unsigned char)p_text[i];
		if (c == '"' || c == '\\') {
			r_out += '\\';
			r_out += (char)c;
			i++;
		} else if (c == '\n') {
			r_out += "\\n";
			i++;
		} else if (c == '\r') {
			r_out += "\\r";
			i++;
		} else if (c == '\t') {
			r_out += "\\t";
			i++;
		} else if (c < 0x20 || c == 0x7F) {
			char buffer[8];
			std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
			r_out += buffer;
			i++;
		} else {
			size_t length = utf8_length(p_text, i);
			if (length == 0) {
				r_out += "\xEF\xBF\xBD";
				i++;
			} else {
				r_out.append(p_text, i, length);
				i += length;
			}
		}
	}
	r_out += '"';
}

void write(std::string &r_out, const Json &p_value) {
	switch (p_value.kind()) {
		case Json::Null:
			r_out += "null";
			break;
		case Json::Bool:
			r_out += p_value.boolean() ? "true" : "false";
			break;
		case Json::Number: {
			double number = p_value.number();
			char buffer[40];
			if (!std::isfinite(number)) {
				r_out += "null";
			} else if (number == std::floor(number) && std::fabs(number) < 9007199254740992.0) {
				std::snprintf(buffer, sizeof(buffer), "%lld", (long long)number);
				r_out += buffer;
			} else {
				std::snprintf(buffer, sizeof(buffer), "%.17g", number);
				// The shortest form that reads back as the same number.
				for (int digits = 1; digits < 17; digits++) {
					char shorter[40];
					std::snprintf(shorter, sizeof(shorter), "%.*g", digits, number);
					if (std::strtod(shorter, nullptr) == number) {
						std::snprintf(buffer, sizeof(buffer), "%s", shorter);
						break;
					}
				}
				r_out += buffer;
			}
			break;
		}
		case Json::String:
			write_string(r_out, p_value.string());
			break;
		case Json::Array:
			r_out += '[';
			for (size_t i = 0; i < p_value.size(); i++) {
				if (i > 0) {
					r_out += ',';
				}
				write(r_out, p_value.at(i));
			}
			r_out += ']';
			break;
		case Json::Object:
			r_out += '{';
			for (size_t i = 0; i < p_value.size(); i++) {
				if (i > 0) {
					r_out += ',';
				}
				write_string(r_out, p_value.keys()[i]);
				r_out += ':';
				write(r_out, p_value.items()[i]);
			}
			r_out += '}';
			break;
	}
}

} // namespace

int64_t Json::integer(int64_t p_default) const {
	if (kind_ != Number || number_ != std::floor(number_) || std::fabs(number_) >= 9223372036854775808.0) {
		return p_default;
	}
	return (int64_t)number_;
}

const std::string &Json::string() const {
	return kind_ == String ? string_ : EMPTY_STRING;
}

const Json &Json::at(size_t p_index) const {
	return kind_ == Array && p_index < items_.size() ? items_[p_index] : NULL_JSON;
}

const Json &Json::get(std::string_view p_key) const {
	if (kind_ == Object) {
		for (size_t i = 0; i < keys_.size(); i++) {
			if (keys_[i] == p_key) {
				return items_[i];
			}
		}
	}
	return NULL_JSON;
}

bool Json::has(std::string_view p_key) const {
	if (kind_ == Object) {
		for (const std::string &key : keys_) {
			if (key == p_key) {
				return true;
			}
		}
	}
	return false;
}

Json &Json::set(std::string_view p_key, Json p_value) {
	if (kind_ != Object) {
		return *this;
	}
	for (size_t i = 0; i < keys_.size(); i++) {
		if (keys_[i] == p_key) {
			items_[i] = std::move(p_value);
			return *this;
		}
	}
	keys_.emplace_back(p_key);
	items_.push_back(std::move(p_value));
	return *this;
}

Json &Json::push(Json p_value) {
	if (kind_ == Array) {
		items_.push_back(std::move(p_value));
	}
	return *this;
}

bool parse_json(std::string_view p_text, Json *r_value) {
	Reader reader(p_text);
	Json value;
	if (!reader.value(&value, 0) || !reader.at_end()) {
		return false;
	}
	*r_value = std::move(value);
	return true;
}

std::string write_json(const Json &p_value) {
	std::string out;
	write(out, p_value);
	return out;
}

} // namespace kas3
