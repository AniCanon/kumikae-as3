// AS3's Date, as numbers: a time is milliseconds since 1970 began, in UTC, as ECMAScript 3 (15.9.1) counts
// them. Nothing here knows where the program runs: local time is a time and the offset of the host's zone
// at it, which the caller gives.
#pragma once

#include "ir.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kas3 {
namespace vm {
namespace date {

constexpr double MS_PER_DAY = 86400000.0;

// The parts of a time, in the calendar ECMAScript uses for every year (the Gregorian, back to the
// first): year, month from 0, day of the month from 1, day of the week from 0 (Sunday), and the time of
// day. NaN in every part for a time that is NaN.
struct Parts {
	double year, month, date, day, hours, minutes, seconds, milliseconds;
};
Parts parts_of(double p_time);

// MakeTime, MakeDay, MakeDate and TimeClip (15.9.1.11 to 15.9.1.14). A part that is not finite makes NaN.
double make_time(double p_hours, double p_minutes, double p_seconds, double p_milliseconds);
double make_day(double p_year, double p_month, double p_date);
double make_date(double p_day, double p_time);
double time_clip(double p_time);
// Day (15.9.1.2): the day since 1970 began that the time is in.
double day_of(double p_time);

// The texts AS3's Date gives, in its own formats. A local text is of the time moved by p_offset (the
// zone's milliseconds ahead of UTC), which it also writes, as GMT-0800. NaN is "Invalid Date".
using Text = ir::DateText;
std::string text(Text p_which, double p_time, double p_offset);

// What Date.parse reads: the time, and whether the text named its zone (UTC, or an offset from it).
// Without one, the time is local and the caller makes it UTC. Nothing if the text is not a date it reads.
struct Parsed {
	double time;
	bool zoned;
};
std::optional<Parsed> parse(std::string_view p_text);

} // namespace date
} // namespace vm
} // namespace kas3
