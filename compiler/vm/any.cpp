#include "any.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include "numeric.h"

namespace kas3 {
namespace vm {

namespace {

// Android tags the top byte of an arm64 heap pointer. Save the tag once and use bit 3 to mark
// pointers that need it restored. On wasm32 bit 3 belongs to the address and there is no top-byte tag.
constexpr uint64_t HEAP_TAGGED = any::POINTER_ALIGNMENT == 16 ? uint64_t(1) << 3 : 0;
uint64_t heap_tag = 0;

[[noreturn]] void unholdable(const char *p_why) {
	std::fprintf(stderr, "kas3: this address can't be held in an untyped value: %s\n", p_why);
	std::abort();
}

Slot tagged(const void *p_pointer, any::PointerTag p_tag) {
	// No pointer is null with a tag: a null String or object is `null`.
	if (!p_pointer) {
		return any::null();
	}
	const uint64_t address = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p_pointer));
	const uint64_t top = address >> 56;
	const uint64_t rest = address & ((uint64_t(1) << 56) - 1);
	if ((rest >> 47) != 0) {
		unholdable("it is above 2^47");
	}
	if (rest & HEAP_TAGGED) {
		unholdable("it is not 16-aligned");
	}
	if (top != 0) {
		heap_tag = heap_tag == 0 ? top : heap_tag;
		if (top != heap_tag) {
			unholdable("the platform tags each allocation differently (memory tagging)");
		}
	}
	return any::of_bits(rest | (top != 0 ? HEAP_TAGGED : 0) | static_cast<uint64_t>(p_tag));
}

} // namespace

Slot empty_value(bool p_number, bool p_null) {
	Slot out;
	out.bits = 0;
	if (p_number) {
		out.d = std::numeric_limits<double>::quiet_NaN();
	} else if (p_null) {
		out = any::null();
	}
	return out;
}

Wide *new_wide(int64_t p_value) {
	void *memory = allocation_fails() ? nullptr : std::malloc(sizeof(Wide));
	if (!memory) {
		return nullptr;
	}
	Wide *wide = new (memory) Wide();
	wide->references = 1;
	wide->value = p_value;
	return wide;
}

void free_wide(Wide *p_wide) {
	std::free(p_wide);
}

namespace any {

Slot string(Str *p_string) {
	return tagged(p_string, STRING);
}
Slot object(Object *p_object) {
	return tagged(p_object, OBJECT);
}
Slot godot_value(void *p_handle) {
	return tagged(p_handle, VALUE);
}
Slot godot_object(void *p_handle) {
	return tagged(p_handle, GODOT);
}
Slot wide(Wide *p_wide) {
	return tagged(p_wide, WIDE);
}

void *pointer(Slot p_value) {
	if (is_number(p_value) || (p_value.bits & INT_FLAG) || (p_value.bits & TAG_MASK) == 0) {
		return nullptr;
	}
	const uint64_t address = p_value.bits & ~(TAG_MASK | HEAP_TAGGED);
	return reinterpret_cast<void *>(static_cast<uintptr_t>((p_value.bits & HEAP_TAGGED) ? address | (heap_tag << 56) : address));
}
Counted *counted(Slot p_value) {
	const AnyTag kind = tag(p_value);
	return kind == AnyTag::String || kind == AnyTag::Object || kind == AnyTag::Wide ? static_cast<Counted *>(pointer(p_value)) : nullptr;
}

double to_number(Slot p_value) {
	switch (tag(p_value)) {
		case AnyTag::Number:
			return to_number_payload(p_value);
		case AnyTag::Int:
			return to_int_payload(p_value);
		case AnyTag::Null:
			return 0;
		case AnyTag::Boolean:
			return to_bool_payload(p_value) ? 1 : 0;
		case AnyTag::String:
			return string_to_number(static_cast<Str *>(pointer(p_value))->view());
		case AnyTag::Wide:
			return static_cast<double>(static_cast<Wide *>(pointer(p_value))->value);
		default:
			return std::numeric_limits<double>::quiet_NaN();
	}
}

float to_float(Slot p_value) {
	return tag(p_value) == AnyTag::Wide ? static_cast<float>(static_cast<Wide *>(pointer(p_value))->value) : static_cast<float>(to_number(p_value));
}

bool to_boolean(Slot p_value) {
	switch (tag(p_value)) {
		case AnyTag::Undefined:
		case AnyTag::Null:
			return false;
		case AnyTag::Boolean:
			return to_bool_payload(p_value);
		case AnyTag::Number: {
			const double number = to_number_payload(p_value);
			return number == number && number != 0;
		}
		case AnyTag::Int:
			return to_int_payload(p_value) != 0;
		case AnyTag::String:
			return static_cast<Str *>(pointer(p_value))->size != 0;
		case AnyTag::Wide:
			return static_cast<Wide *>(pointer(p_value))->value != 0;
		default:
			return true;
	}
}

const char *type_of(Slot p_value) {
	switch (tag(p_value)) {
		case AnyTag::Undefined:
			return "undefined";
		case AnyTag::Boolean:
			return "boolean";
		case AnyTag::Number:
		case AnyTag::Int:
		case AnyTag::Wide:
			return "number";
		case AnyTag::String:
			return "string";
		default:
			return "object";
	}
}

std::optional<std::string> text(Slot p_value) {
	switch (tag(p_value)) {
		case AnyTag::Undefined:
			return std::string("undefined");
		case AnyTag::Null:
			return std::string("null");
		case AnyTag::Boolean:
			return std::string(to_bool_payload(p_value) ? "true" : "false");
		case AnyTag::Number:
			return number_to_string(to_number_payload(p_value));
		case AnyTag::Int:
			return std::to_string(to_int_payload(p_value));
		case AnyTag::String:
			return std::string(static_cast<Str *>(pointer(p_value))->view());
		case AnyTag::Wide:
			return std::to_string(static_cast<Wide *>(pointer(p_value))->value);
		default:
			return std::nullopt;
	}
}

namespace {

bool numeric(AnyTag p_tag) {
	return p_tag == AnyTag::Number || p_tag == AnyTag::Int || p_tag == AnyTag::Wide;
}

} // namespace

std::optional<int> compare_numbers(Slot p_a, Slot p_b) {
	const AnyTag a = tag(p_a);
	const AnyTag b = tag(p_b);
	const auto whole = [](Slot p_value, AnyTag p_tag) {
		return p_tag == AnyTag::Wide ? static_cast<Wide *>(pointer(p_value))->value : static_cast<int64_t>(to_int_payload(p_value));
	};
	if ((a == AnyTag::Wide || a == AnyTag::Int) && (b == AnyTag::Wide || b == AnyTag::Int)) {
		const int64_t x = whole(p_a, a);
		const int64_t y = whole(p_b, b);
		return x < y ? -1 : (x > y ? 1 : 0);
	}
	if (a == AnyTag::Wide || b == AnyTag::Wide) {
		const bool wide_first = a == AnyTag::Wide;
		const int64_t x = whole(wide_first ? p_a : p_b, AnyTag::Wide);
		const Slot other = wide_first ? p_b : p_a;
		const AnyTag other_tag = wide_first ? b : a;
		// Text that is a whole number an Int64 holds is compared exactly, as typed CmpI64Str does.
		if (other_tag == AnyTag::String) {
			const std::string_view text = static_cast<Str *>(pointer(other))->view();
			if (const std::optional<int64_t> exact = string_to_wide_exact(text)) {
				const int sign = x < *exact ? -1 : (x > *exact ? 1 : 0);
				return wide_first ? sign : -sign;
			}
		}
		// An Int64 against a Number, exactly: compare_wide puts the Int64 first.
		const double order = compare_wide(x, to_number(other));
		if (order != order) {
			return std::nullopt;
		}
		const int sign = order < 0 ? -1 : (order > 0 ? 1 : 0);
		return wide_first ? sign : -sign;
	}
	const double x = to_number(p_a);
	const double y = to_number(p_b);
	if (x != x || y != y) {
		return std::nullopt;
	}
	return x < y ? -1 : (x > y ? 1 : 0);
}

std::optional<bool> strict_equal(Slot p_a, Slot p_b) {
	const AnyTag a = tag(p_a);
	const AnyTag b = tag(p_b);
	if (numeric(a) && numeric(b)) {
		return compare_numbers(p_a, p_b) == 0;
	}
	if (is_engine(p_a) && is_engine(p_b)) {
		return std::nullopt; // Godot says whether two of its values are equal.
	}
	if (a != b) {
		return false;
	}
	if (a == AnyTag::String) {
		return static_cast<Str *>(pointer(p_a))->view() == static_cast<Str *>(pointer(p_b))->view();
	}
	return a == AnyTag::Undefined || p_a.bits == p_b.bits;
}

std::optional<bool> loose_equal(Slot p_a, Slot p_b) {
	const AnyTag a = tag(p_a);
	const AnyTag b = tag(p_b);
	const auto nothing = [](AnyTag p_tag) { return p_tag == AnyTag::Undefined || p_tag == AnyTag::Null; };
	if (nothing(a) || nothing(b)) {
		return nothing(a) && nothing(b);
	}
	const auto primitive = [](AnyTag p_tag) { return p_tag == AnyTag::Boolean || p_tag == AnyTag::String || numeric(p_tag); };
	if (primitive(a) && primitive(b)) {
		if (a == AnyTag::String && b == AnyTag::String) {
			return strict_equal(p_a, p_b);
		}
		return compare_numbers(p_a, p_b) == 0;
	}
	if (primitive(a) != primitive(b)) {
		return std::nullopt; // An object against a primitive: compared by its text, which the interpreter gives.
	}
	return strict_equal(p_a, p_b);
}

} // namespace any
} // namespace vm
} // namespace kas3
