#include "interpreter_internal.h"

#include "date_time.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kas3 {
namespace vm {

using ir::DateFunction;
using ir::DatePart;
using ir::Function;
using ir::Instruction;
using ir::Kind;

namespace {

constexpr double NOT_A_NUMBER = __builtin_nan("");

double part_of(const date::Parts &p_parts, DatePart p_part) {
	switch (p_part) {
		case DatePart::FullYear:
			return p_parts.year;
		case DatePart::Month:
			return p_parts.month;
		case DatePart::Date:
			return p_parts.date;
		case DatePart::Day:
			return p_parts.day;
		case DatePart::Hours:
			return p_parts.hours;
		case DatePart::Minutes:
			return p_parts.minutes;
		case DatePart::Seconds:
			return p_parts.seconds;
		case DatePart::Milliseconds:
			return p_parts.milliseconds;
	}
	return NOT_A_NUMBER;
}

std::string_view text_in(Slot p_string) {
	const Str *string = static_cast<const Str *>(p_string.p);
	return string ? string->view() : std::string_view("null");
}

} // namespace

// LocalTime and UTC (15.9.1.9): the host says how far its zone is ahead at a moment. The moment a local
// time stands for is found from the offset at about that time, then the offset at that moment.
double Interpreter::local_time(double p_time) {
	return std::isfinite(p_time) ? p_time + host_.clock.local_offset(p_time) : p_time;
}

double Interpreter::utc_time(double p_local) {
	if (!std::isfinite(p_local)) {
		return p_local;
	}
	const double guess = p_local - host_.clock.local_offset(p_local);
	return p_local - host_.clock.local_offset(guess);
}

// The parts from the year on, in p_list from p_first (15.9.3.1, 15.9.4.3): a year from 0 to 99 is one of
// the 1900s; parts left out are the first day, at midnight.
double Interpreter::date_from_parts(const std::vector<uint32_t> &p_list, size_t p_first, bool p_local) {
	const size_t base = frames_.back().base;
	const auto part = [&](size_t p_index, double p_missing) { return p_first + p_index < p_list.size() ? stack_[base + p_list[p_first + p_index]].d : p_missing; };
	double year = part(0, NOT_A_NUMBER);
	if (!std::isnan(year) && std::trunc(year) >= 0 && std::trunc(year) <= 99) {
		year = 1900 + std::trunc(year);
	}
	const double made = date::make_date(date::make_day(year, part(1, 0), part(2, 1)), date::make_time(part(3, 0), part(4, 0), part(5, 0), part(6, 0)));
	return date::time_clip(p_local ? utc_time(made) : made);
}

// setHours(h, m, s, ms) and the rest (15.9.5.28 to 15.9.5.41): the parts written from the setter's own,
// the others kept as the date has them. setFullYear starts a date that is NaN from 1970.
double Interpreter::date_set(double p_time, uint32_t p_code, const double *p_given, size_t p_count) {
	const DatePart part = static_cast<DatePart>((p_code >> 8) & 0xFF);
	const bool utc = (p_code >> 16) != 0;
	const double from = part == DatePart::FullYear && std::isnan(p_time) ? 0 : utc ? p_time : local_time(p_time);
	const date::Parts at = date::parts_of(from);
	const auto given = [&](size_t p_index, double p_kept) { return p_index < p_count ? p_given[p_index] : p_kept; };
	const double within = from - date::day_of(from) * date::MS_PER_DAY;
	double made = NOT_A_NUMBER;
	switch (part) {
		case DatePart::Milliseconds:
			made = date::make_date(date::day_of(from), date::make_time(at.hours, at.minutes, at.seconds, given(0, 0)));
			break;
		case DatePart::Seconds:
			made = date::make_date(date::day_of(from), date::make_time(at.hours, at.minutes, given(0, 0), given(1, at.milliseconds)));
			break;
		case DatePart::Minutes:
			made = date::make_date(date::day_of(from), date::make_time(at.hours, given(0, 0), given(1, at.seconds), given(2, at.milliseconds)));
			break;
		case DatePart::Hours:
			made = date::make_date(date::day_of(from), date::make_time(given(0, 0), given(1, at.minutes), given(2, at.seconds), given(3, at.milliseconds)));
			break;
		case DatePart::Date:
			made = date::make_date(date::make_day(at.year, at.month, given(0, 0)), within);
			break;
		case DatePart::Month:
			made = date::make_date(date::make_day(at.year, given(0, 0), given(1, at.date)), within);
			break;
		case DatePart::FullYear:
			made = date::make_date(date::make_day(given(0, 0), given(1, at.month), given(2, at.date)), within);
			break;
		case DatePart::Day:
			break;
	}
	return date::time_clip(utc ? made : utc_time(made));
}

// new Date(): now. new Date(x): a Date's time, text parsed, or a number as a time. new Date(y, m, ...):
// the parts, in local time.
void Interpreter::date_new(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	double time = NOT_A_NUMBER;
	if (list.empty()) {
		time = date::time_clip(host_.clock.now());
	} else if (list.size() > 1) {
		time = date_from_parts(list, 0, true);
	} else {
		const Slot value = stack_[frames_.back().base + list[0]];
		const AnyTag tag = any::tag(value);
		const Object *object = tag == AnyTag::Object ? static_cast<const Object *>(any::pointer(value)) : nullptr;
		if (object && module_.date_class != ir::NONE && object->klass == module_.date_class) {
			time = const_cast<Object *>(object)->fields()[ir::DATE_TIME].d;
		} else if (tag == AnyTag::String || tag == AnyTag::Object) {
			std::string written;
			const Slot held = share(Kind::Any, value);
			const bool said = any_text(held, &written); // An object's text may run the program.
			drop_any(held);
			if (!said) {
				return;
			}
			const std::optional<date::Parsed> parsed = date::parse(written);
			time = parsed ? date::time_clip(parsed->zoned ? parsed->time : utc_time(parsed->time)) : NOT_A_NUMBER;
		} else {
			time = date::time_clip(any::to_number(value));
		}
	}
	Object *made = new_object(module_.date_class, module_.classes[module_.date_class].fields.size(), heap_.list_for(module_.date_class));
	if (!made) {
		out_of_memory();
		return;
	}
	made->fields()[ir::DATE_TIME].d = time;
	Slot out;
	out.bits = 0;
	out.p = made;
	put(Kind::Obj, stack_[frames_.back().base + p_in.a], out);
}

void Interpreter::date_call(const Function &p_function, const Instruction &p_in) {
	const DateFunction function = static_cast<DateFunction>(p_in.b & 0xFF);
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	Slot *r = stack_.data() + frames_.back().base;
	if (function == DateFunction::New) {
		date_new(p_function, p_in);
		return;
	}
	if (function == DateFunction::Parse || function == DateFunction::UTC) {
		const std::optional<date::Parsed> parsed = function == DateFunction::Parse ? date::parse(text_in(r[list[0]])) : std::nullopt;
		r[p_in.a].d = function == DateFunction::UTC ? date_from_parts(list, 0, false)
				: parsed								? date::time_clip(parsed->zoned ? parsed->time : utc_time(parsed->time))
														: NOT_A_NUMBER;
		return;
	}
	Object *date = static_cast<Object *>(r[list[0]].p);
	if (!date) {
		raise_null();
		return;
	}
	// A setter takes four parts at most; what is given beyond them is not read.
	std::array<double, 4> given{};
	const size_t count = std::min(list.size() - 1, given.size());
	for (size_t i = 0; i < count; i++) {
		given[i] = r[list[i + 1]].d;
	}
	Slot out;
	if (date_apply(date, p_in.b, given.data(), count, &out)) {
		if (function == DateFunction::Text) {
			assign(Kind::Str, r[p_in.a], static_cast<Str *>(out.p));
		} else {
			r[p_in.a].d = out.d;
		}
	}
}

// One of a Date's methods or properties other than new, parse and UTC, on the parts given: a Number, or
// for a text a String with a reference of its own. False if it raised.
bool Interpreter::date_apply(Object *p_date, uint32_t p_code, const double *p_given, size_t p_count, Slot *r_out) {
	double &time = p_date->fields()[ir::DATE_TIME].d;
	r_out->bits = 0;
	switch (static_cast<DateFunction>(p_code & 0xFF)) {
		case DateFunction::Time:
			r_out->d = time;
			break;
		case DateFunction::SetTime:
			time = date::time_clip(p_count > 0 ? p_given[0] : NOT_A_NUMBER);
			r_out->d = time;
			break;
		case DateFunction::TimezoneOffset:
			r_out->d = std::isnan(time) ? time : (time - local_time(time)) / 60000.0;
			break;
		case DateFunction::Get: {
			const bool utc = (p_code >> 16) != 0;
			r_out->d = part_of(date::parts_of(utc ? time : local_time(time)), static_cast<DatePart>((p_code >> 8) & 0xFF));
			break;
		}
		case DateFunction::Set:
			time = date_set(time, p_code, p_given, p_count);
			r_out->d = time;
			break;
		case DateFunction::Text:
			r_out->p = text(date_text(p_date, static_cast<ir::DateText>((p_code >> 8) & 0xFF)));
			return r_out->p != nullptr;
		default:
			break;
	}
	return true;
}

} // namespace vm
} // namespace kas3
