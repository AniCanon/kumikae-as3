#include "date_time.h"

#include <array>
#include <cmath>

namespace kas3 {
namespace vm {
namespace date {

namespace {

constexpr std::array<std::string_view, 12> MONTH_NAMES = { "january", "february", "march", "april", "may", "june", "july", "august",
	"september", "october", "november", "december" };
constexpr std::array<std::string_view, 7> DAY_NAMES = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };

bool is_digit(char p_char) {
	return p_char >= '0' && p_char <= '9';
}

bool is_letter(char p_char) {
	return (p_char >= 'a' && p_char <= 'z') || (p_char >= 'A' && p_char <= 'Z');
}

// A word is a name if it is at least its first three letters, and no more than the whole of it.
bool names(std::string_view p_word, std::string_view p_name) {
	return p_word.size() >= 3 && p_word.size() <= p_name.size() && p_name.substr(0, p_word.size()) == p_word;
}

// Reads the forms AS3's reference gives for Date.parse, and what Date's own texts write: words for the
// month and the day of the week, numbers for the rest, in most orders.
class Reader {
	std::string_view text_;
	size_t at_ = 0;
	double year_ = NAN, month_ = NAN, date_ = NAN;
	double hours_ = 0, minutes_ = 0, seconds_ = 0;
	bool time_ = false;
	int half_ = 0; // 1 for AM, 2 for PM.
	bool zoned_ = false;
	double offset_ = 0; // Minutes ahead of UTC.

	char peek() const { return at_ < text_.size() ? text_[at_] : '\0'; }
	struct Number {
		double value;
		size_t digits;
	};
	std::optional<Number> number() {
		const size_t start = at_;
		double value = 0;
		while (is_digit(peek())) {
			value = value * 10 + (text_[at_++] - '0');
		}
		if (at_ == start) {
			return std::nullopt;
		}
		return Number{ value, at_ - start };
	}
	bool word();
	bool zone();
	bool time(double p_hours);
	bool slashed(Number p_first);
	bool lone(Number p_number);

public:
	explicit Reader(std::string_view p_text) :
			text_(p_text) {}
	std::optional<Parsed> read();
};

std::optional<Parsed> Reader::read() {
	while (at_ < text_.size()) {
		const char next = peek();
		bool read = true;
		if (next == ' ' || next == ',' || next == '\t') {
			at_++;
		} else if (next == '(') {
			while (at_ < text_.size() && text_[at_] != ')') {
				at_++;
			}
			at_++;
		} else if (is_letter(next)) {
			read = word();
		} else if (is_digit(next)) {
			const Number first = *number();
			read = peek() == ':' ? time(first.value) : peek() == '/' ? slashed(first) : lone(first);
		} else if ((next == '+' || next == '-') && time_ && !zoned_) {
			zoned_ = true;
			read = zone();
		} else {
			read = false;
		}
		if (!read) {
			return std::nullopt;
		}
	}
	if (std::isnan(year_) || std::isnan(month_) || std::isnan(date_) || (half_ && (hours_ < 1 || hours_ > 12))) {
		return std::nullopt;
	}
	const double hours = half_ == 2 ? std::fmod(hours_, 12) + 12 : half_ == 1 ? std::fmod(hours_, 12) : hours_;
	const double year = year_ < 100 ? year_ + 1900 : year_;
	const double time = make_date(make_day(year, month_, date_), make_time(hours, minutes_, seconds_, 0)) - offset_ * 60000.0;
	return Parsed{ time, zoned_ };
}

bool Reader::word() {
	std::string lower;
	while (is_letter(peek())) {
		lower += static_cast<char>(text_[at_++] | 0x20);
	}
	for (size_t month = 0; month < MONTH_NAMES.size(); month++) {
		if (names(lower, MONTH_NAMES[month])) {
			if (!std::isnan(month_)) {
				return false;
			}
			month_ = static_cast<double>(month);
			if (peek() == '/') {
				at_++; // "Feb/1/2005": the day and the year follow.
			}
			return true;
		}
	}
	for (const std::string_view day : DAY_NAMES) {
		if (names(lower, day)) {
			return true;
		}
	}
	if (lower == "am" || lower == "pm") {
		half_ = lower == "am" ? 1 : 2;
		return time_;
	}
	if (lower == "gmt" || lower == "utc" || lower == "ut" || lower == "z") {
		zoned_ = true;
		return peek() == '+' || peek() == '-' ? zone() : true;
	}
	return false;
}

// "+0100", "-08", "+01:00": how far the zone is ahead of UTC.
bool Reader::zone() {
	const double sign = text_[at_++] == '-' ? -1 : 1;
	const std::optional<Number> first = number();
	if (!first) {
		return false;
	}
	double minutes = first->digits > 2 ? std::floor(first->value / 100) * 60 + std::fmod(first->value, 100) : first->value * 60;
	if (first->digits <= 2 && peek() == ':') {
		at_++;
		const std::optional<Number> rest = number();
		if (!rest) {
			return false;
		}
		minutes += rest->value;
	}
	offset_ = sign * minutes;
	return true;
}

// "13:05" or "13:05:59", the hours read already.
bool Reader::time(double p_hours) {
	if (time_) {
		return false;
	}
	time_ = true;
	hours_ = p_hours;
	at_++;
	const std::optional<Number> minutes = number();
	if (!minutes) {
		return false;
	}
	minutes_ = minutes->value;
	if (peek() == ':') {
		at_++;
		const std::optional<Number> seconds = number();
		if (!seconds) {
			return false;
		}
		seconds_ = seconds->value;
	}
	return hours_ < 24 && minutes_ < 60 && seconds_ < 60;
}

// "2/1/2005" (month, day, year), "2005/2/1" (year, month, day), or after a month's name "1/2005".
bool Reader::slashed(Number p_first) {
	std::array<Number, 3> parts = { p_first, Number{ 0, 0 }, Number{ 0, 0 } };
	size_t count = 1;
	while (peek() == '/' && count < parts.size()) {
		at_++;
		const std::optional<Number> next = number();
		if (!next) {
			return false;
		}
		parts[count++] = *next;
	}
	if (!std::isnan(year_) || !std::isnan(date_)) {
		return false;
	}
	if (count == 2 && !std::isnan(month_)) {
		date_ = parts[0].value;
		year_ = parts[1].value;
		return true;
	}
	if (count != 3 || !std::isnan(month_)) {
		return false;
	}
	const bool year_first = parts[0].digits > 2;
	year_ = year_first ? parts[0].value : parts[2].value;
	month_ = (year_first ? parts[1].value : parts[0].value) - 1;
	date_ = year_first ? parts[2].value : parts[1].value;
	return month_ >= 0 && month_ < 12;
}

// A number on its own: the day of the month if that is still to come and it can be one, else the year.
bool Reader::lone(Number p_number) {
	if (std::isnan(date_) && p_number.digits <= 2 && p_number.value >= 1 && p_number.value <= 31) {
		date_ = p_number.value;
		return true;
	}
	if (!std::isnan(year_)) {
		return false;
	}
	year_ = p_number.value;
	return true;
}

} // namespace

std::optional<Parsed> parse(std::string_view p_text) {
	return Reader(p_text).read();
}

} // namespace date
} // namespace vm
} // namespace kas3
