// Vector.sort(f) and Vector.sort(options).
#include "interpreter_internal.h"
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
	if (!p_compare) {
		// Each element's key, found once: its text (which may run the program) or its number.
		numbers.resize(p_elements.size());
		texts.resize(p_elements.size());
		for (uint32_t at : *r_order) {
			Slot untyped = to_any(p_kind, p_elements[at]);
			bool ok = true;
			if (numeric) {
				Slot number;
				ok = from_any(untyped, Kind::F64, NONE, &number);
				numbers[at] = number.d;
			} else {
				std::string text;
				ok = any_text(untyped, &text);
				texts[at] = (p_options & CASE_INSENSITIVE) ? lower_case(text) : std::move(text);
			}
			drop_any(untyped);
			if (!ok) {
				return false;
			}
		}
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
	Slot self;
	self.bits = 0;
	self.p = p_vector;
	const Kind gives = klass.array ? Kind::Any : Kind::Obj;
	if (sort_order(kind, elements, compare, options, &order, &twins)) {
		if (klass.array && twins) {
			give(p_call, Kind::Any, any::integer(0));
		} else if (klass.array && (options & INDEXED)) {
			if (Vector *indices = fresh_vector(p_vector->klass, order.size())) {
				for (uint32_t i = 0; i < order.size(); i++) {
					indices->data[i] = any::integer(static_cast<int32_t>(order[i]));
				}
				Slot made;
				made.bits = 0;
				made.p = indices;
				give(p_call, Kind::Any, to_any(Kind::Obj, made));
				release_object(indices);
			}
		} else {
			for (uint32_t i = 0; !twins && !(options & INDEXED) && i < order.size() && i < p_vector->length; i++) {
				put(kind, p_vector->data[i], share(kind, elements[order[i]]));
			}
			give(p_call, gives, gives == Kind::Any ? to_any(Kind::Obj, self) : share(Kind::Obj, self));
		}
	}
	for (Slot &each : elements) {
		drop(kind, each);
	}
	release_object(p_vector);
}

} // namespace vm
} // namespace kas3
