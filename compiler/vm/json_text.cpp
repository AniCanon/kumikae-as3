#include "json_text.h"

#include "runtime.h"
#include "text_units.h"

#include <optional>
#include <vector>

namespace kas3 {
namespace vm {
namespace json {

namespace {

// JSONWhiteSpace: these four only, not ECMAScript's other spaces.
bool is_space(char p_char) {
	return p_char == ' ' || p_char == '\t' || p_char == '\n' || p_char == '\r';
}

bool is_digit(char p_char) {
	return p_char >= '0' && p_char <= '9';
}

std::optional<uint16_t> hex_digit(char p_char) {
	if (is_digit(p_char)) {
		return static_cast<uint16_t>(p_char - '0');
	}
	if (p_char >= 'a' && p_char <= 'f') {
		return static_cast<uint16_t>(p_char - 'a' + 10);
	}
	if (p_char >= 'A' && p_char <= 'F') {
		return static_cast<uint16_t>(p_char - 'A' + 10);
	}
	return std::nullopt;
}

// Reads one value at a time, keeping the Objects and Arrays it is inside on a stack of its own: text
// nested as deep as MAX_DEPTH takes no more of C++'s stack than a flat one.
class Reader {
	std::string_view text_;
	size_t at_ = 0;
	Builder &builder_;
	std::vector<char> open_; // '[' or '{' for each container the reader is inside.
	std::string scratch_;

	bool next_is(char p_char) const { return at_ < text_.size() && text_[at_] == p_char; }
	void skip_space() {
		while (at_ < text_.size() && is_space(text_[at_])) {
			at_++;
		}
	}
	void skip_digits() {
		while (at_ < text_.size() && is_digit(text_[at_])) {
			at_++;
		}
	}
	Fault given(bool p_kept) const { return p_kept ? Fault::None : Fault::Stopped; }
	Fault string_into(std::string *r_text);
	Fault escape_into(std::string *r_text);
	Fault number();
	Fault word();
	Fault member();
	Fault value(bool *r_opened);
	Fault close();

public:
	Reader(std::string_view p_text, Builder &p_builder) :
			text_(p_text), builder_(p_builder) {}
	Fault run();
};

// A JSONString, from its opening quote: the characters between the escapes are copied as they are.
Fault Reader::string_into(std::string *r_text) {
	r_text->clear();
	size_t run = ++at_;
	while (at_ < text_.size()) {
		const unsigned char c = static_cast<unsigned char>(text_[at_]);
		if (c == '"' || c == '\\') {
			append_text(r_text, text_.substr(run, at_ - run));
			at_++;
			if (c == '"') {
				return Fault::None;
			}
			if (const Fault fault = escape_into(r_text); fault != Fault::None) {
				return fault;
			}
			run = at_;
		} else if (c < 0x20) {
			return Fault::Syntax;
		} else {
			at_++;
		}
	}
	return Fault::Syntax;
}

Fault Reader::escape_into(std::string *r_text) {
	if (at_ >= text_.size()) {
		return Fault::Syntax;
	}
	const char c = text_[at_++];
	switch (c) {
		case '"':
		case '\\':
		case '/':
			r_text->push_back(c);
			return Fault::None;
		case 'b':
			r_text->push_back('\b');
			return Fault::None;
		case 'f':
			r_text->push_back('\f');
			return Fault::None;
		case 'n':
			r_text->push_back('\n');
			return Fault::None;
		case 'r':
			r_text->push_back('\r');
			return Fault::None;
		case 't':
			r_text->push_back('\t');
			return Fault::None;
		case 'u':
			break;
		default:
			return Fault::Syntax;
	}
	uint16_t unit = 0;
	for (int i = 0; i < 4; i++) {
		const std::optional<uint16_t> digit = at_ < text_.size() ? hex_digit(text_[at_]) : std::nullopt;
		if (!digit) {
			return Fault::Syntax;
		}
		unit = static_cast<uint16_t>(unit << 4 | *digit);
		at_++;
	}
	append_unit(r_text, unit); // A half of a character is joined with the half before it.
	return Fault::None;
}

// -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?, read as ECMAScript reads the number's text.
Fault Reader::number() {
	const size_t start = at_;
	if (next_is('-')) {
		at_++;
	}
	if (next_is('0')) {
		at_++;
	} else if (at_ < text_.size() && is_digit(text_[at_])) {
		skip_digits();
	} else {
		return Fault::Syntax;
	}
	if (next_is('.')) {
		at_++;
		if (at_ >= text_.size() || !is_digit(text_[at_])) {
			return Fault::Syntax;
		}
		skip_digits();
	}
	if (next_is('e') || next_is('E')) {
		at_++;
		if (next_is('+') || next_is('-')) {
			at_++;
		}
		if (at_ >= text_.size() || !is_digit(text_[at_])) {
			return Fault::Syntax;
		}
		skip_digits();
	}
	return given(builder_.number(string_to_number(text_.substr(start, at_ - start))));
}

Fault Reader::word() {
	for (std::string_view each : { "true", "false", "null" }) {
		if (text_.substr(at_, each.size()) == each) {
			at_ += each.size();
			return given(each[0] == 'n' ? builder_.null() : builder_.boolean(each[0] == 't'));
		}
	}
	return Fault::Syntax;
}

// A member's name and its colon, up to its value.
Fault Reader::member() {
	if (!next_is('"')) {
		return Fault::Syntax;
	}
	if (const Fault fault = string_into(&scratch_); fault != Fault::None) {
		return fault;
	}
	if (!builder_.key(scratch_)) {
		return Fault::Stopped;
	}
	skip_space();
	if (!next_is(':')) {
		return Fault::Syntax;
	}
	at_++;
	skip_space();
	return Fault::None;
}

// One value; an Object or an Array with something in it is opened, and its first value comes next.
Fault Reader::value(bool *r_opened) {
	*r_opened = false;
	if (at_ >= text_.size()) {
		return Fault::Syntax;
	}
	const char c = text_[at_];
	if (c == '[' || c == '{') {
		if (open_.size() == MAX_DEPTH) {
			return Fault::TooDeep;
		}
		at_++;
		open_.push_back(c);
		if (!(c == '[' ? builder_.begin_array() : builder_.begin_object())) {
			return Fault::Stopped;
		}
		skip_space();
		if (next_is(c == '[' ? ']' : '}')) {
			return close();
		}
		*r_opened = true;
		return c == '{' ? member() : Fault::None;
	}
	if (c == '"') {
		const Fault fault = string_into(&scratch_);
		return fault != Fault::None ? fault : given(builder_.string(scratch_));
	}
	if (c == '-' || is_digit(c)) {
		return number();
	}
	return word();
}

Fault Reader::close() {
	at_++;
	const bool kept = open_.back() == '[' ? builder_.end_array() : builder_.end_object();
	open_.pop_back();
	return given(kept);
}

Fault Reader::run() {
	skip_space();
	for (;;) {
		bool opened = false;
		if (const Fault fault = value(&opened); fault != Fault::None) {
			return fault;
		}
		if (opened) {
			continue;
		}
		// After a value: the containers it ends, then the next value of the one it is in.
		for (;;) {
			skip_space();
			if (open_.empty()) {
				return at_ == text_.size() ? Fault::None : Fault::Syntax;
			}
			if (next_is(',')) {
				at_++;
				skip_space();
				if (open_.back() == '{') {
					if (const Fault fault = member(); fault != Fault::None) {
						return fault;
					}
				}
				break;
			}
			if (!next_is(open_.back() == '[' ? ']' : '}')) {
				return Fault::Syntax;
			}
			if (const Fault fault = close(); fault != Fault::None) {
				return fault;
			}
		}
	}
}

} // namespace

Fault parse(std::string_view p_text, Builder &p_builder) {
	return Reader(p_text, p_builder).run();
}

void quote(std::string_view p_text, std::string *r_out) {
	constexpr char HEX[] = "0123456789abcdef";
	r_out->push_back('"');
	size_t run = 0;
	for (size_t i = 0; i < p_text.size(); i++) {
		const unsigned char c = static_cast<unsigned char>(p_text[i]);
		if (c != '"' && c != '\\' && c >= 0x20) {
			continue;
		}
		r_out->append(p_text.substr(run, i - run));
		run = i + 1;
		r_out->push_back('\\');
		switch (c) {
			case '"':
			case '\\':
				r_out->push_back(static_cast<char>(c));
				break;
			case '\b':
				r_out->push_back('b');
				break;
			case '\f':
				r_out->push_back('f');
				break;
			case '\n':
				r_out->push_back('n');
				break;
			case '\r':
				r_out->push_back('r');
				break;
			case '\t':
				r_out->push_back('t');
				break;
			default:
				r_out->append("u00");
				r_out->push_back(HEX[c >> 4]);
				r_out->push_back(HEX[c & 0xF]);
				break;
		}
	}
	r_out->append(p_text.substr(run));
	r_out->push_back('"');
}

} // namespace json
} // namespace vm
} // namespace kas3
