#include "date_time.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace kas3 {
namespace vm {
namespace date {

namespace {

constexpr double MS_PER_HOUR = 3600000.0, MS_PER_MINUTE = 60000.0, MS_PER_SECOND = 1000.0;
constexpr double NOT_A_NUMBER = __builtin_nan("");
constexpr std::array<const char *, 7> DAYS = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
constexpr std::array<const char *, 12> MONTHS = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
constexpr std::array<int, 13> DAYS_BEFORE = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
// Past this many years from 1970 a time is beyond TimeClip's range, and the arithmetic below stays exact.
constexpr double FURTHEST_YEAR = 400000.0;

double modulo(double p_value, double p_by) {
	const double rest = std::fmod(p_value, p_by);
	return rest < 0 ? rest + p_by : rest + 0.0;
}

bool is_leap(double p_year) {
	return modulo(p_year, 4) == 0 && (modulo(p_year, 100) != 0 || modulo(p_year, 400) == 0);
}

double day_from_year(double p_year) {
	return 365 * (p_year - 1970) + std::floor((p_year - 1969) / 4) - std::floor((p_year - 1901) / 100) + std::floor((p_year - 1601) / 400);
}

double year_of(double p_time) {
	double year = std::floor(p_time / (MS_PER_DAY * 365.2425)) + 1970;
	while (day_from_year(year) * MS_PER_DAY > p_time) {
		year--;
	}
	while (day_from_year(year + 1) * MS_PER_DAY <= p_time) {
		year++;
	}
	return year;
}

int days_before(int p_month, bool p_leap) {
	return DAYS_BEFORE[p_month] + (p_leap && p_month >= 2 ? 1 : 0);
}

int whole(double p_value) {
	return static_cast<int>(p_value);
}

} // namespace

double day_of(double p_time) {
	return std::floor(p_time / MS_PER_DAY);
}

Parts parts_of(double p_time) {
	if (!std::isfinite(p_time)) {
		return { NOT_A_NUMBER, NOT_A_NUMBER, NOT_A_NUMBER, NOT_A_NUMBER, NOT_A_NUMBER, NOT_A_NUMBER, NOT_A_NUMBER, NOT_A_NUMBER };
	}
	const double day = day_of(p_time);
	const double within = p_time - day * MS_PER_DAY;
	const double year = year_of(p_time);
	const bool leap = is_leap(year);
	const int in_year = whole(day - day_from_year(year));
	int month = 0;
	while (month < 11 && in_year >= days_before(month + 1, leap)) {
		month++;
	}
	Parts parts;
	parts.year = year;
	parts.month = month;
	parts.date = in_year - days_before(month, leap) + 1;
	parts.day = modulo(day + 4, 7);
	parts.hours = std::floor(within / MS_PER_HOUR);
	parts.minutes = modulo(std::floor(within / MS_PER_MINUTE), 60);
	parts.seconds = modulo(std::floor(within / MS_PER_SECOND), 60);
	parts.milliseconds = modulo(within, 1000);
	return parts;
}

double make_time(double p_hours, double p_minutes, double p_seconds, double p_milliseconds) {
	if (!std::isfinite(p_hours) || !std::isfinite(p_minutes) || !std::isfinite(p_seconds) || !std::isfinite(p_milliseconds)) {
		return NOT_A_NUMBER;
	}
	return std::trunc(p_hours) * MS_PER_HOUR + std::trunc(p_minutes) * MS_PER_MINUTE + std::trunc(p_seconds) * MS_PER_SECOND +
			std::trunc(p_milliseconds);
}

double make_day(double p_year, double p_month, double p_date) {
	if (!std::isfinite(p_year) || !std::isfinite(p_month) || !std::isfinite(p_date)) {
		return NOT_A_NUMBER;
	}
	const double month = std::trunc(p_month);
	const double year = std::trunc(p_year) + std::floor(month / 12);
	if (std::fabs(year - 1970) > FURTHEST_YEAR) {
		return NOT_A_NUMBER;
	}
	return day_from_year(year) + days_before(whole(modulo(month, 12)), is_leap(year)) + std::trunc(p_date) - 1;
}

double make_date(double p_day, double p_time) {
	if (!std::isfinite(p_day) || !std::isfinite(p_time)) {
		return NOT_A_NUMBER;
	}
	return p_day * MS_PER_DAY + p_time;
}

double time_clip(double p_time) {
	if (!std::isfinite(p_time) || std::fabs(p_time) > 8.64e15) {
		return NOT_A_NUMBER;
	}
	return std::trunc(p_time) + 0.0;
}

std::string text(Text p_which, double p_time, double p_offset) {
	if (std::isnan(p_time)) {
		return "Invalid Date";
	}
	const Parts at = parts_of(p_which == Text::UTC ? p_time : p_time + p_offset);
	const int minutes = static_cast<int>(std::fabs(p_offset) / MS_PER_MINUTE);
	const char sign = p_offset < 0 ? '-' : '+';
	const char *day = DAYS[whole(at.day)], *month = MONTHS[whole(at.month)];
	const int hours = whole(at.hours), twelve = hours % 12 == 0 ? 12 : hours % 12;
	const char *half = hours < 12 ? "AM" : "PM";
	char out[96];
	switch (p_which) {
		case Text::Full:
			std::snprintf(out, sizeof(out), "%s %s %d %02d:%02d:%02d GMT%c%02d%02d %d", day, month, whole(at.date), hours, whole(at.minutes),
					whole(at.seconds), sign, minutes / 60, minutes % 60, whole(at.year));
			break;
		case Text::Date:
		case Text::LocaleDate:
			std::snprintf(out, sizeof(out), "%s %s %d %d", day, month, whole(at.date), whole(at.year));
			break;
		case Text::Time:
			std::snprintf(out, sizeof(out), "%02d:%02d:%02d GMT%c%02d%02d", hours, whole(at.minutes), whole(at.seconds), sign, minutes / 60,
					minutes % 60);
			break;
		case Text::Locale:
			std::snprintf(out, sizeof(out), "%s %s %d %d %02d:%02d:%02d %s", day, month, whole(at.date), whole(at.year), twelve,
					whole(at.minutes), whole(at.seconds), half);
			break;
		case Text::LocaleTime:
			std::snprintf(out, sizeof(out), "%02d:%02d:%02d %s", twelve, whole(at.minutes), whole(at.seconds), half);
			break;
		case Text::UTC:
			std::snprintf(out, sizeof(out), "%s %s %d %02d:%02d:%02d %d UTC", day, month, whole(at.date), hours, whole(at.minutes),
					whole(at.seconds), whole(at.year));
			break;
	}
	return out;
}

} // namespace date
} // namespace vm
} // namespace kas3
