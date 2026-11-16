// RegExp's and Date's members by name, and what each is to the IR (RegExpCall's and DateCall's second
// operand): the lowerer reads them for a member of a typed value, and the interpreter for one reached by
// name on an untyped value (`u.lastIndex`, `u.getTime()`). Nothing here allocates.
#pragma once

#include "ir.h"

#include <array>
#include <optional>
#include <string_view>

namespace kas3 {
namespace ir {

struct RegExpProperty {
	std::string_view name;
	RegExpFunction function;
	Kind kind;
};

// Read only. lastIndex is not among them: it is a field (REGEXP_LAST_INDEX), read and written.
constexpr std::array<RegExpProperty, 6> REGEXP_PROPERTIES = { {
		{ "source", RegExpFunction::Source, Kind::Str },
		{ "global", RegExpFunction::Global, Kind::Bool },
		{ "ignoreCase", RegExpFunction::IgnoreCase, Kind::Bool },
		{ "multiline", RegExpFunction::Multiline, Kind::Bool },
		{ "dotall", RegExpFunction::Dotall, Kind::Bool },
		{ "extended", RegExpFunction::Extended, Kind::Bool },
} };

inline const RegExpProperty *regexp_property(std::string_view p_name) {
	for (const RegExpProperty &property : REGEXP_PROPERTIES) {
		if (property.name == p_name) {
			return &property;
		}
	}
	return nullptr;
}

// exec(text) and test(text): the methods of a RegExp.
inline std::optional<RegExpFunction> regexp_method(std::string_view p_name) {
	return p_name == "exec" ? std::optional<RegExpFunction>(RegExpFunction::Exec)
			: p_name == "test" ? std::optional<RegExpFunction>(RegExpFunction::Test)
							   : std::nullopt;
}

constexpr std::array<std::pair<std::string_view, DatePart>, 8> DATE_PARTS = { {
		{ "FullYear", DatePart::FullYear },
		{ "Month", DatePart::Month },
		{ "Date", DatePart::Date },
		{ "Day", DatePart::Day },
		{ "Hours", DatePart::Hours },
		{ "Minutes", DatePart::Minutes },
		{ "Seconds", DatePart::Seconds },
		{ "Milliseconds", DatePart::Milliseconds },
} };

constexpr std::array<std::pair<std::string_view, DateText>, 8> DATE_TEXTS = { {
		{ "toString", DateText::Full },
		{ "toDateString", DateText::Date },
		{ "toTimeString", DateText::Time },
		{ "toLocaleString", DateText::Locale },
		{ "toLocaleDateString", DateText::LocaleDate },
		{ "toLocaleTimeString", DateText::LocaleTime },
		{ "toUTCString", DateText::UTC },
		{ "toJSON", DateText::Full }, // As Flash Player 11 has it: the date's toString().
} };

// What reads and what writes a property of a Date, or one of a part's names: NONE for what is only read.
struct DateAccess {
	uint32_t read;
	uint32_t write;
};

// A part by its name, `FullYear` or `FullYearUTC`, its first letter in either case (the property is
// `fullYear`): there is no setter of the day of the week.
inline std::optional<DateAccess> date_part_access(std::string_view p_name) {
	const bool utc = p_name.size() > 3 && p_name.substr(p_name.size() - 3) == "UTC";
	const std::string_view part = utc ? p_name.substr(0, p_name.size() - 3) : p_name;
	for (const auto &[name, which] : DATE_PARTS) {
		const bool first = !part.empty() && (part[0] == name[0] || part[0] == name[0] - 'A' + 'a');
		if (first && part.substr(1) == name.substr(1)) {
			const uint32_t detail = static_cast<uint32_t>(which);
			return DateAccess{ date_code(DateFunction::Get, detail, utc), which == DatePart::Day ? NONE : date_code(DateFunction::Set, detail, utc) };
		}
	}
	return std::nullopt;
}

// date.fullYear, date.time, date.dayUTC: read as the getters, written as the setters, with the one part.
inline std::optional<DateAccess> date_property(std::string_view p_name) {
	if (p_name == "time") {
		return DateAccess{ date_code(DateFunction::Time), date_code(DateFunction::SetTime) };
	}
	if (p_name == "timezoneOffset") {
		return DateAccess{ date_code(DateFunction::TimezoneOffset), NONE };
	}
	if (p_name.empty() || p_name[0] < 'a' || p_name[0] > 'z') {
		return std::nullopt;
	}
	return date_part_access(p_name);
}

// A method's code: getTime, a text, or get or set, with UTC or not, and a part.
inline std::optional<uint32_t> date_method(std::string_view p_name) {
	if (p_name == "getTime" || p_name == "valueOf") {
		return date_code(DateFunction::Time);
	}
	if (p_name == "setTime") {
		return date_code(DateFunction::SetTime);
	}
	if (p_name == "getTimezoneOffset") {
		return date_code(DateFunction::TimezoneOffset);
	}
	for (const auto &[name, text] : DATE_TEXTS) {
		if (name == p_name) {
			return date_code(DateFunction::Text, static_cast<uint32_t>(text));
		}
	}
	const bool getter = p_name.substr(0, 3) == "get";
	if (!getter && p_name.substr(0, 3) != "set") {
		return std::nullopt;
	}
	// getUTCHours is the part HoursUTC.
	const std::string_view rest = p_name.substr(3);
	const bool utc = rest.substr(0, 3) == "UTC";
	const std::string_view part = utc ? rest.substr(3) : rest;
	if (part.empty() || part[0] < 'A' || part[0] > 'Z' || part.substr(part.size() > 3 ? part.size() - 3 : 0) == "UTC") {
		return std::nullopt;
	}
	std::optional<DateAccess> access = date_part_access(part);
	if (!access) {
		return std::nullopt;
	}
	const uint32_t utc_bit = utc ? 1u << 16 : 0u;
	const uint32_t code = getter ? access->read : access->write;
	return code == NONE ? std::nullopt : std::optional<uint32_t>(code | utc_bit);
}

} // namespace ir
} // namespace kas3
