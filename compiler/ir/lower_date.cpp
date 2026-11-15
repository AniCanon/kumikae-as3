#include "lower_library.h"

#include <array>

namespace kas3 {
namespace ir {

namespace {

constexpr std::array<std::pair<std::string_view, DatePart>, 8> PARTS = { {
		{ "FullYear", DatePart::FullYear },
		{ "Month", DatePart::Month },
		{ "Date", DatePart::Date },
		{ "Day", DatePart::Day },
		{ "Hours", DatePart::Hours },
		{ "Minutes", DatePart::Minutes },
		{ "Seconds", DatePart::Seconds },
		{ "Milliseconds", DatePart::Milliseconds },
} };

constexpr std::array<std::pair<std::string_view, DateText>, 8> TEXTS = { {
		{ "toString", DateText::Full },
		{ "toDateString", DateText::Date },
		{ "toTimeString", DateText::Time },
		{ "toLocaleString", DateText::Locale },
		{ "toLocaleDateString", DateText::LocaleDate },
		{ "toLocaleTimeString", DateText::LocaleTime },
		{ "toUTCString", DateText::UTC },
		{ "toJSON", DateText::Full }, // As Flash Player 11 has it: the date's toString().
} };

std::optional<DatePart> part_named(std::string_view p_name) {
	for (const auto &[name, part] : PARTS) {
		if (name == p_name) {
			return part;
		}
	}
	return std::nullopt;
}

// What reads and what writes one of a part's names, `FullYear` or `FullYearUTC`: there is no setter of
// the day of the week.
struct Access {
	uint32_t read;
	uint32_t write;
};

std::optional<Access> part_access(std::string_view p_name) {
	const bool utc = p_name.size() > 3 && p_name.substr(p_name.size() - 3) == "UTC";
	const std::optional<DatePart> part = part_named(utc ? p_name.substr(0, p_name.size() - 3) : p_name);
	if (!part) {
		return std::nullopt;
	}
	const uint32_t detail = static_cast<uint32_t>(*part);
	return Access{ date_code(DateFunction::Get, detail, utc), *part == DatePart::Day ? NONE : date_code(DateFunction::Set, detail, utc) };
}

// A method's code: getTime, a text, or get or set, with UTC or not, and a part.
std::optional<uint32_t> method_code(std::string_view p_name) {
	if (p_name == "getTime" || p_name == "valueOf") {
		return date_code(DateFunction::Time);
	}
	if (p_name == "setTime") {
		return date_code(DateFunction::SetTime);
	}
	if (p_name == "getTimezoneOffset") {
		return date_code(DateFunction::TimezoneOffset);
	}
	for (const auto &[name, text] : TEXTS) {
		if (name == p_name) {
			return date_code(DateFunction::Text, static_cast<uint32_t>(text));
		}
	}
	const bool getter = p_name.substr(0, 3) == "get";
	if (!getter && p_name.substr(0, 3) != "set") {
		return std::nullopt;
	}
	std::string_view rest = p_name.substr(3);
	const bool utc = rest.substr(0, 3) == "UTC";
	const std::optional<Access> access = part_access(std::string(utc ? rest.substr(3) : rest) + (utc ? "UTC" : ""));
	if (!access) {
		return std::nullopt;
	}
	return getter ? std::optional<uint32_t>(access->read) : access->write == NONE ? std::nullopt : std::optional<uint32_t>(access->write);
}

} // namespace

uint32_t Library::date_method() {
	const std::optional<uint32_t> code = method_code(function_->name);
	if (!code) {
		return refused("", Kind::F64);
	}
	class_of(l_, function_->owner);
	const DateFunction function = static_cast<DateFunction>(*code & 0xFF);
	std::vector<uint32_t> list = { l_.settled(l_.receiver(object()), later(0), position_) };
	for (uint32_t i = 0; i < call_->arguments.count; i++) {
		// A setter takes the parts that are written, and no more: one left out keeps what the date has.
		const bool part = function == DateFunction::Set || function == DateFunction::SetTime;
		const uint32_t value = l_.expression(call_->arguments[i], part ? Kind::F64 : Kind::Str);
		if (part) {
			list.push_back(l_.settled(value, later(i + 1), position_));
		}
	}
	const uint32_t out = emit(Op::DateCall, *code, std::move(list), function == DateFunction::Text ? Kind::Str : Kind::F64);
	return function_->name == "toJSON" ? l_.convert(out, Kind::Any, position_) : out;
}

// Date.parse(text) and Date.UTC(year, month, ...).
uint32_t Library::date_static() {
	class_of(l_, function_->owner);
	const bool parse = function_->name == "parse";
	if (!parse && function_->name != "UTC") {
		return refused("", Kind::F64);
	}
	std::vector<uint32_t> list;
	for (uint32_t i = 0; i < call_->arguments.count; i++) {
		list.push_back(l_.settled(l_.expression(call_->arguments[i], parse ? Kind::Str : Kind::F64), later(i + 1), position_));
	}
	return emit(Op::DateCall, date_code(parse ? DateFunction::Parse : DateFunction::UTC), std::move(list), Kind::F64);
}

// new Date(): now; new Date(x): a time, a Date's time, or text to parse; new Date(year, month, ...): parts.
uint32_t Library::date_new(Lowerer &p_lowerer, New *p_new) {
	Lowerer &l = p_lowerer;
	const uint32_t position = p_new->offset;
	const uint32_t count = p_new->arguments.count;
	std::vector<uint32_t> list;
	for (uint32_t i = 0; i < count; i++) {
		Expr *after = i + 1 < count ? p_new->arguments[i + 1] : nullptr;
		const uint32_t value = count == 1 ? l.any_value(p_new->arguments[i]) : l.expression(p_new->arguments[i], Kind::F64);
		list.push_back(l.settled(value, after, position));
	}
	const uint32_t out = l.reg(Kind::Obj);
	l.emit(Op::DateCall, position, out, date_code(DateFunction::New), l.arguments(std::move(list)));
	return out;
}

// date.fullYear, date.time, date.day: read as the getters, written as the setters, with the one part.
std::optional<Place> Library::date_place(Lowerer &p_lowerer, Member *p_member, Expr *p_later) {
	const std::string_view name = p_member->resolved()->name;
	std::optional<Access> access;
	if (name == "time") {
		access = Access{ date_code(DateFunction::Time), date_code(DateFunction::SetTime) };
	} else if (name == "timezoneOffset") {
		access = Access{ date_code(DateFunction::TimezoneOffset), NONE };
	} else if (!name.empty()) {
		std::string part(name);
		part[0] = static_cast<char>(part[0] - 'a' + 'A');
		access = part_access(part);
	}
	if (!access) {
		return std::nullopt;
	}
	Place place;
	place.where = Place::Library;
	place.op = Op::DateCall;
	place.index = access->read;
	place.key = access->write;
	place.kind = Kind::F64;
	place.object = p_lowerer.settled(p_lowerer.receiver(p_member->object), p_later, p_member->name_offset);
	return place;
}

} // namespace ir
} // namespace kas3
