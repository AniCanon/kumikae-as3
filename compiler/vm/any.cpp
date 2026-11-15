#include "any.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace kas3 {
namespace vm {

namespace {

const uint64_t NUMBER_OFFSET = uint64_t(1) << 48; // Added to a Number's bits: its top 16 bits are never 0.
const uint64_t INT_FLAG = uint64_t(1) << 47;
const uint64_t TAG_MASK = 7;
const uint64_t NULL_BITS = 1 << 3;
const uint64_t FALSE_BITS = 2 << 3;
const uint64_t TRUE_BITS = 3 << 3;

// The low 3 bits of a pointer that is held.
enum PointerTag : uint64_t { STRING = 1, OBJECT = 2, VALUE = 3, GODOT = 4, WIDE = 5 };

Slot bits(uint64_t p_bits) {
	Slot slot;
	slot.bits = p_bits;
	return slot;
}

Slot tagged(const void *p_pointer, PointerTag p_tag) {
	// No pointer is null with a tag: a null String or object is `null`.
	return p_pointer ? bits(reinterpret_cast<uintptr_t>(p_pointer) | p_tag) : bits(NULL_BITS);
}

bool is_number(Slot p_value) {
	return (p_value.bits >> 48) != 0;
}

} // namespace

Wide *new_wide(int64_t p_value) {
	void *memory = std::malloc(sizeof(Wide));
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

Slot undefined() {
	return bits(0);
}
Slot null() {
	return bits(NULL_BITS);
}
Slot boolean(bool p_value) {
	return bits(p_value ? TRUE_BITS : FALSE_BITS);
}
Slot number(double p_value) {
	if (std::isnan(p_value)) {
		p_value = std::numeric_limits<double>::quiet_NaN();
	}
	uint64_t raw = 0;
	std::memcpy(&raw, &p_value, sizeof(raw));
	return bits(raw + NUMBER_OFFSET);
}
Slot integer(int32_t p_value) {
	return bits(INT_FLAG | static_cast<uint32_t>(p_value));
}
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

AnyTag tag(Slot p_value) {
	if (is_number(p_value)) {
		return AnyTag::Number;
	}
	if (p_value.bits & INT_FLAG) {
		return AnyTag::Int;
	}
	switch (p_value.bits & TAG_MASK) {
		case STRING:
			return AnyTag::String;
		case OBJECT:
			return AnyTag::Object;
		case VALUE:
			return AnyTag::Value;
		case GODOT:
			return AnyTag::Godot;
		case WIDE:
			return AnyTag::Wide;
		default:
			break;
	}
	if (p_value.bits == NULL_BITS) {
		return AnyTag::Null;
	}
	return p_value.bits == FALSE_BITS || p_value.bits == TRUE_BITS ? AnyTag::Boolean : AnyTag::Undefined;
}

bool to_bool_payload(Slot p_value) {
	return p_value.bits == TRUE_BITS;
}
double to_number_payload(Slot p_value) {
	const uint64_t raw = p_value.bits - NUMBER_OFFSET;
	double out = 0;
	std::memcpy(&out, &raw, sizeof(out));
	return out;
}
int32_t to_int_payload(Slot p_value) {
	return static_cast<int32_t>(static_cast<uint32_t>(p_value.bits));
}
void *pointer(Slot p_value) {
	if (is_number(p_value) || (p_value.bits & INT_FLAG) || (p_value.bits & TAG_MASK) == 0) {
		return nullptr;
	}
	return reinterpret_cast<void *>(static_cast<uintptr_t>(p_value.bits & ~TAG_MASK));
}
Counted *counted(Slot p_value) {
	const AnyTag kind = tag(p_value);
	return kind == AnyTag::String || kind == AnyTag::Object || kind == AnyTag::Wide ? static_cast<Counted *>(pointer(p_value)) : nullptr;
}
bool is_engine(Slot p_value) {
	const AnyTag kind = tag(p_value);
	return kind == AnyTag::Value || kind == AnyTag::Godot;
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

std::optional<bool> strict_equal(Slot p_a, Slot p_b) {
	const AnyTag a = tag(p_a);
	const AnyTag b = tag(p_b);
	if (numeric(a) && numeric(b)) {
		if (a == AnyTag::Wide && b == AnyTag::Wide) {
			return static_cast<Wide *>(pointer(p_a))->value == static_cast<Wide *>(pointer(p_b))->value;
		}
		return to_number(p_a) == to_number(p_b);
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
	return p_a.bits == p_b.bits;
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
		return to_number(p_a) == to_number(p_b);
	}
	if (primitive(a) != primitive(b)) {
		return std::nullopt; // An object against a primitive: compared by its text, which the interpreter gives.
	}
	return strict_equal(p_a, p_b);
}

} // namespace any
} // namespace vm
} // namespace kas3
