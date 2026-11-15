// Vector.sort(f) and Vector.sort(options).
#include "interpreter_internal.h"

#include <cctype>
#include <cmath>
#include <numeric>

namespace kas3 {
namespace vm {

using ir::Kind;
using ir::NONE;

namespace {

// Array's options, which sort() takes as a number.
constexpr int32_t CASE_INSENSITIVE = 1, DESCENDING = 2, UNIQUE = 4, INDEXED = 8, NUMERIC = 16;

std::string folded(std::string p_text) {
	for (char &each : p_text) {
		each = static_cast<char>(std::tolower(static_cast<unsigned char>(each)));
	}
	return p_text;
}

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
// the options, as text unless they say NUMERIC. False if the program raised.
bool Interpreter::sort_order(Kind p_kind, const std::vector<Slot> &p_elements, Object *p_compare, int32_t p_options, std::vector<uint32_t> *r_order) {
	r_order->resize(p_elements.size());
	std::iota(r_order->begin(), r_order->end(), 0);
	if (p_compare) {
		return merge_sort(r_order, [&](uint32_t p_a, uint32_t p_b) -> std::optional<double> {
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
			return std::isnan(number.d) ? 0.0 : number.d;
		});
	}
	// Each element's key, found once: its text (which may run the program) or its number.
	const bool numeric = p_options & NUMERIC;
	std::vector<double> numbers;
	std::vector<std::string> texts;
	for (const Slot &element : p_elements) {
		Slot untyped = to_any(p_kind, element);
		bool ok = true;
		if (numeric) {
			Slot number;
			ok = from_any(untyped, Kind::F64, NONE, &number);
			numbers.push_back(number.d);
		} else {
			std::string text;
			ok = any_text(untyped, &text);
			texts.push_back((p_options & CASE_INSENSITIVE) ? folded(std::move(text)) : std::move(text));
		}
		drop_any(untyped);
		if (!ok) {
			return false;
		}
	}
	const auto compare = [&](uint32_t p_a, uint32_t p_b) -> std::optional<double> {
		const double order = numeric ? (numbers[p_a] < numbers[p_b] ? -1 : numbers[p_a] > numbers[p_b] ? 1 : 0) : compare_utf16(texts[p_a], texts[p_b]);
		return (p_options & DESCENDING) ? -order : order;
	};
	merge_sort(r_order, compare);
	// UNIQUESORT: two the same, and nothing moves.
	for (size_t i = 1; (p_options & UNIQUE) && i < r_order->size(); i++) {
		if (*compare((*r_order)[i - 1], (*r_order)[i]) == 0) {
			std::iota(r_order->begin(), r_order->end(), 0);
			break;
		}
	}
	return true;
}

// sort(f) or sort(options): the elements in their new order, and the vector itself given back. The order is
// worked out on the side, with a reference of its own to each element: if the function that compares
// raises, the vector is as it was; what it did to the vector meanwhile is written over.
void Interpreter::vector_sort(Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = module_.classes[p_vector->klass].element;
	const Slot behavior = argument(p_call, 1);
	Object *compare = any::tag(behavior) == AnyTag::Object ? static_cast<Object *>(any::pointer(behavior)) : nullptr;
	compare = compare && compare->klass == module_.function_class ? compare : nullptr;
	// A function, or the options as a number; anything else is a TypeError, as avmplus has it.
	Slot options;
	options.bits = 0;
	const AnyTag tag = any::tag(behavior);
	if (!compare && tag != AnyTag::Int && tag != AnyTag::Number && tag != AnyTag::Wide) {
		const std::optional<std::string> shown = any::text(behavior);
		raise(module_.type_error_class, "Error #1034: Type Coercion failed: cannot convert " + (shown ? *shown : std::string("an object")) + " to Function.");
		return;
	}
	if (!compare && !from_any(behavior, Kind::I32, NONE, &options)) {
		return;
	}
	Slot self;
	self.bits = 0;
	self.p = p_vector;
	if (options.i & INDEXED) {
		// Flash works out the indices as an Array and leaves the vector as it is; Vector.sort gives back
		// the vector all the same.
		give(p_call, Kind::Obj, share(Kind::Obj, self));
		return;
	}
	std::vector<Slot> elements(p_vector->length);
	for (uint32_t i = 0; i < p_vector->length; i++) {
		elements[i] = share(kind, p_vector->data[i]);
	}
	retain(p_vector);
	std::vector<uint32_t> order;
	if (sort_order(kind, elements, compare, options.i, &order)) {
		for (uint32_t i = 0; i < order.size() && i < p_vector->length; i++) {
			put(kind, p_vector->data[i], share(kind, elements[order[i]]));
		}
		give(p_call, Kind::Obj, share(Kind::Obj, self));
	}
	for (Slot &each : elements) {
		drop(kind, each);
	}
	release_object(p_vector);
}

} // namespace vm
} // namespace kas3
