#include "library_names.h"
#include "lower_library.h"

namespace kas3 {
namespace ir {


uint32_t Library::date_method() {
	const std::optional<uint32_t> code = ir::date_method(function_->name);
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
	const std::optional<DateAccess> access = date_property(p_member->resolved()->name);
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
