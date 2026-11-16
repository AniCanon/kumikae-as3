// Vector.sort(f) and Vector.sort(options).
#include "interpreter_internal.h"
#include "table.h"
#include "text_units.h"

#include <cmath>
#include <numeric>

namespace kas3 {
namespace vm {

using ir::Kind;
using ir::NONE;

namespace {

// Array's options, which sort() takes as a number.
constexpr int32_t CASE_INSENSITIVE = 1, DESCENDING = 2, UNIQUE = 4, INDEXED = 8, NUMERIC = 16;

// A stable merge sort of the order: p_compare says whether its first comes after its second (more than 0),
// or nothing if it raised, and then so does this.
template <typename Compare>
bool merge_sort(std::vector<uint32_t> *p_order, Compare p_compare) {
	const size_t count = p_order->size();
	std::vector<uint32_t> spare(count);
	for (size_t width = 1; width < count; width *= 2) {
		for (size_t low = 0; low < count; low += 2 * width) {
			const size_t middle = std::min(low + width, count);
			const size_t high = std::min(low + 2 * width, count);
			size_t left = low, right = middle, out = low;
			while (left < middle && right < high) {
				const std::optional<double> order = p_compare((*p_order)[left], (*p_order)[right]);
				if (!order) {
					return false;
				}
				spare[out++] = *order > 0 ? (*p_order)[right++] : (*p_order)[left++];
			}
			while (left < middle) {
				spare[out++] = (*p_order)[left++];
			}
			while (right < high) {
				spare[out++] = (*p_order)[right++];
			}
		}
		p_order->swap(spare);
	}
	return true;
}

} // namespace

// The new order of p_elements: by the function that compares two (its answer as a Number, NaN as 0), or by
// the options, as text unless they say NUMERIC. Untyped elements that are undefined go last, uncompared, as
// ECMAScript has it. False if the program raised. *r_twins says whether UNIQUESORT found two the same.
bool Interpreter::sort_order(Kind p_kind, const std::vector<Slot> &p_elements, Object *p_compare, int32_t p_options, std::vector<uint32_t> *r_order,
		bool *r_twins) {
	*r_twins = false;
	r_order->clear();
	std::vector<uint32_t> undefined;
	for (uint32_t i = 0; i < p_elements.size(); i++) {
		const bool missing = p_kind == Kind::Any && any::tag(p_elements[i]) == AnyTag::Undefined;
		(missing ? undefined : *r_order).push_back(i);
	}
	const bool descending = p_options & DESCENDING;
	std::vector<double> numbers;
	std::vector<std::string> texts;
	const bool numeric = p_options & NUMERIC;
	if (!p_compare && !sort_keys(p_kind, p_elements, *r_order, p_options, &numbers, &texts)) {
		return false;
	}
	const auto compare = [&](uint32_t p_a, uint32_t p_b) -> std::optional<double> {
		double order = 0;
		if (p_compare) {
			Slot both[2] = { to_any(p_kind, p_elements[p_a]), to_any(p_kind, p_elements[p_b]) };
			const std::optional<Slot> answer = call_function_within(p_compare, both, 2);
			drop_any(both[0]);
			drop_any(both[1]);
			if (!answer) {
				return std::nullopt;
			}
			Slot number;
			const bool ok = from_any(*answer, Kind::F64, NONE, &number);
			drop_any(*answer);
			if (!ok) {
				return std::nullopt;
			}
			order = std::isnan(number.d) ? 0.0 : number.d;
		} else if (numeric && p_kind == Kind::I64) {
			// Int64s by their own 64 bits: past 2^53 a Number can't tell them apart.
			const int64_t a = p_elements[p_a].l, b = p_elements[p_b].l;
			order = a < b ? -1 : a > b ? 1 : 0;
		} else {
			order = numeric ? (numbers[p_a] < numbers[p_b] ? -1 : numbers[p_a] > numbers[p_b] ? 1 : 0) : compare_utf16(texts[p_a], texts[p_b]);
		}
		return descending ? -order : order;
	};
	if (!merge_sort(r_order, compare)) {
		return false;
	}
	// UNIQUESORT: two the same, and nothing moves.
	for (size_t i = 1; (p_options & UNIQUE) && i < r_order->size() && !*r_twins; i++) {
		const std::optional<double> order = compare((*r_order)[i - 1], (*r_order)[i]);
		if (!order) {
			return false;
		}
		*r_twins = *order == 0;
	}
	r_order->insert(r_order->end(), undefined.begin(), undefined.end());
	if (*r_twins) {
		std::iota(r_order->begin(), r_order->end(), 0);
	}
	return true;
}

// Each element's key for sort() by its options, found once: its text (which may run the program) or, for
// NUMERIC, its number. False if the program raised.
bool Interpreter::sort_keys(Kind p_kind, const std::vector<Slot> &p_elements, const std::vector<uint32_t> &p_order, int32_t p_options,
		std::vector<double> *r_numbers, std::vector<std::string> *r_texts) {
	r_numbers->resize(p_elements.size());
	r_texts->resize(p_elements.size());
	for (uint32_t at : p_order) {
		Slot untyped = to_any(p_kind, p_elements[at]);
		bool ok = true;
		if (p_options & NUMERIC) {
			Slot number;
			ok = from_any(untyped, Kind::F64, NONE, &number);
			(*r_numbers)[at] = number.d;
		} else {
			std::string text;
			ok = any_text(untyped, &text);
			(*r_texts)[at] = (p_options & CASE_INSENSITIVE) ? lower_case(text) : std::move(text);
		}
		drop_any(untyped);
		if (!ok) {
			return false;
		}
	}
	return true;
}

// What sort() is told: nothing, a function, a function and the options, or the options, as avmplus's generic
// sort takes them; anything else is a TypeError. False if it raised.
bool Interpreter::sort_behavior(const VectorCall &p_call, Object **r_compare, int32_t *r_options) {
	*r_compare = nullptr;
	*r_options = 0;
	const auto number = [](Slot p_value) {
		const AnyTag tag = any::tag(p_value);
		return tag == AnyTag::Int || tag == AnyTag::Number || tag == AnyTag::Wide;
	};
	const auto refuse = [&](Slot p_value) {
		const std::optional<std::string> shown = any::text(p_value);
		raise(module_.type_error_class, "Error #1034: Type Coercion failed: cannot convert " + (shown ? *shown : std::string("an object")) + " to Function.");
		return false;
	};
	if (p_call.list.size() < 2) {
		return true;
	}
	const Slot first = argument(p_call, 1);
	Object *function = any::tag(first) == AnyTag::Object ? static_cast<Object *>(any::pointer(first)) : nullptr;
	*r_compare = function && function->klass == module_.function_class ? function : nullptr;
	const size_t at = *r_compare ? 2 : 1;
	if (at >= p_call.list.size()) {
		return true;
	}
	const Slot options = argument(p_call, at);
	if (!number(options)) {
		return refuse(options);
	}
	Slot whole;
	if (!from_any(options, Kind::I32, NONE, &whole)) {
		return false;
	}
	*r_options = whole.i;
	return true;
}

// sort(): the elements in their new order, the vector itself given back. The order is worked out on the
// side, with a reference of its own to each element: if the function that compares raises, the vector is as
// it was; what it did to the vector meanwhile is written over. An Array gives what avmplus's generic sort
// gives: 0 for UNIQUESORT with two the same, a new Array of the indices for RETURNINDEXEDARRAY, both leaving
// it as it was; a Vector gives itself all the same.
void Interpreter::vector_sort(Vector *p_vector, const VectorCall &p_call) {
	const ir::Class &klass = module_.classes[p_vector->klass];
	const Kind kind = klass.element;
	Object *compare = nullptr;
	int32_t options = 0;
	if (!sort_behavior(p_call, &compare, &options)) {
		return;
	}
	std::vector<Slot> elements(p_vector->length);
	for (uint32_t i = 0; i < p_vector->length; i++) {
		elements[i] = share(kind, p_vector->data[i]);
	}
	retain(p_vector);
	std::vector<uint32_t> order;
	bool twins = false;
	if (sort_order(kind, elements, compare, options, &order, &twins)) {
		sorted(p_vector, p_call, elements, order, twins, options);
	}
	for (Slot &each : elements) {
		drop(kind, each);
	}
	release_object(p_vector);
}

// What sort() and sortOn() give, once the new order is known (see vector_sort).
void Interpreter::sorted(Vector *p_vector, const VectorCall &p_call, const std::vector<Slot> &p_elements, const std::vector<uint32_t> &p_order,
		bool p_twins, int32_t p_options) {
	const ir::Class &klass = module_.classes[p_vector->klass];
	const Kind kind = klass.element;
	Slot self;
	self.bits = 0;
	self.p = p_vector;
	if (klass.array && p_twins) {
		give(p_call, Kind::Any, any::integer(0));
	} else if (klass.array && (p_options & INDEXED)) {
		if (Vector *indices = fresh_vector(p_vector->klass, p_order.size())) {
			for (uint32_t i = 0; i < p_order.size(); i++) {
				indices->data[i] = any::integer(static_cast<int32_t>(p_order[i]));
			}
			Slot made;
			made.bits = 0;
			made.p = indices;
			give(p_call, Kind::Any, to_any(Kind::Obj, made));
			release_object(indices);
		}
	} else {
		for (uint32_t i = 0; !p_twins && !(p_options & INDEXED) && i < p_order.size() && i < p_vector->length; i++) {
			put(kind, p_vector->data[i], share(kind, p_elements[p_order[i]]));
		}
		const Kind gives = klass.array ? Kind::Any : Kind::Obj;
		give(p_call, gives, gives == Kind::Any ? to_any(Kind::Obj, self) : share(Kind::Obj, self));
	}
}

// sortOn(names, options), as avmplus's generic sortOn reads them: one name (its text) or an Array of
// names; options a number for every field, or an Array of one number per name, the first of which says
// UNIQUESORT and RETURNINDEXEDARRAY for the whole sort. An options Array of another length is not used.
bool Interpreter::sort_on_fields(const VectorCall &p_call, std::vector<SortField> *r_fields, int32_t *r_options) {
	*r_options = 0;
	r_fields->clear();
	const Slot names = p_call.list.size() > 1 ? argument(p_call, 1) : any::undefined();
	const Slot options = p_call.list.size() > 2 ? argument(p_call, 2) : any::undefined();
	const auto array_of = [&](Slot p_value) -> const Vector * {
		const Object *object = held_object(p_value);
		return object && module_.classes[object->klass].array ? static_cast<const Vector *>(object) : nullptr;
	};
	const auto number = [&](Slot p_value, int32_t *r_number) {
		Slot whole;
		const bool done = any::tag(p_value) == AnyTag::Undefined || from_any(p_value, Kind::I32, NONE, &whole);
		*r_number = any::tag(p_value) == AnyTag::Undefined ? 0 : whole.i;
		return done;
	};
	std::vector<Slot> written;
	if (const Vector *list = array_of(names)) {
		written.assign(list->data, list->data + list->length);
	} else {
		written.push_back(names);
	}
	for (Slot each : written) {
		std::string name;
		if (!any_text(each, &name)) {
			return false;
		}
		r_fields->push_back({ std::move(name), 0 });
	}
	const Vector *each_option = array_of(options);
	if (each_option && each_option->length == r_fields->size()) {
		for (uint32_t i = 0; i < each_option->length; i++) {
			if (!number(each_option->data[i], &(*r_fields)[i].options)) {
				return false;
			}
		}
		*r_options = r_fields->empty() ? 0 : (*r_fields)[0].options;
	} else if (!each_option) {
		if (!number(options, r_options)) {
			return false;
		}
		for (SortField &field : *r_fields) {
			field.options = *r_options;
		}
	}
	return true;
}

// The element's property of that name, or undefined when it has none (a value that is not an object, or
// an object without it): sortOn never raises #1069.
bool Interpreter::sort_field(Slot p_element, Str *p_name, Slot *r_value) {
	*r_value = any::undefined();
	if (!held_object(p_element)) {
		return true;
	}
	PropertyKey key;
	bool has = false;
	if (!property_key(p_element, any::string(p_name), &key) || !has_property(p_element, key, false, &has)) {
		return false;
	}
	return !has || get_property(p_element, key, r_value);
}

// Each element's key for each field of sortOn(), found once: [field * count + element]. False if the program
// raised, or memory ran out.
bool Interpreter::sort_on_keys(const std::vector<SortField> &p_fields, const std::vector<Slot> &p_elements, std::vector<double> *r_numbers,
		std::vector<std::string> *r_texts) {
	const size_t count = p_elements.size();
	r_numbers->assign(p_fields.size() * count, 0.0);
	r_texts->assign(p_fields.size() * count, std::string());
	bool ok = true;
	for (size_t f = 0; ok && f < p_fields.size(); f++) {
		Str *name = text(p_fields[f].name);
		ok = name != nullptr;
		for (uint32_t i = 0; ok && i < count; i++) {
			Slot value;
			ok = sort_field(p_elements[i], name, &value);
			if (ok && (p_fields[f].options & NUMERIC)) {
				Slot as_number;
				ok = from_any(value, Kind::F64, NONE, &as_number);
				(*r_numbers)[f * count + i] = as_number.d;
			} else if (ok) {
				std::string words;
				ok = any_text(value, &words);
				(*r_texts)[f * count + i] = (p_fields[f].options & CASE_INSENSITIVE) ? lower_case(words) : std::move(words);
			}
			drop_any(value); // Undefined if it could not be read.
		}
		if (release(name)) {
			free_string(name);
		}
	}
	return ok;
}

// sortOn(): by each field in turn, as text unless the field's options say NUMERIC, each field ascending
// unless they say DESCENDING. Stable; what sort() gives is given (see sorted()).
void Interpreter::vector_sort_on(Vector *p_vector, const VectorCall &p_call) {
	std::vector<SortField> fields;
	int32_t options = 0;
	if (!sort_on_fields(p_call, &fields, &options)) {
		return;
	}
	const uint32_t count = p_vector->length;
	std::vector<Slot> elements(count);
	for (uint32_t i = 0; i < count; i++) {
		elements[i] = share(Kind::Any, p_vector->data[i]);
	}
	retain(p_vector);
	std::vector<double> numbers;
	std::vector<std::string> texts;
	const bool ok = sort_on_keys(fields, elements, &numbers, &texts);
	const auto compare = [&](uint32_t p_a, uint32_t p_b) -> std::optional<double> {
		for (size_t f = 0; f < fields.size(); f++) {
			const double a = numbers[f * count + p_a], b = numbers[f * count + p_b];
			double order = (fields[f].options & NUMERIC) ? (a < b ? -1 : a > b ? 1 : 0) : compare_utf16(texts[f * count + p_a], texts[f * count + p_b]);
			order = (fields[f].options & DESCENDING) ? -order : order;
			if (order != 0) {
				return order;
			}
		}
		return 0.0;
	};
	std::vector<uint32_t> order(count);
	std::iota(order.begin(), order.end(), 0);
	if (ok && merge_sort(&order, compare)) {
		bool twins = false;
		for (size_t i = 1; (options & UNIQUE) && i < order.size() && !twins; i++) {
			twins = *compare(order[i - 1], order[i]) == 0;
		}
		sorted(p_vector, p_call, elements, twins ? std::vector<uint32_t>() : order, twins, options);
	}
	for (Slot &each : elements) {
		drop_any(each);
	}
	release_object(p_vector);
}

} // namespace vm
} // namespace kas3
