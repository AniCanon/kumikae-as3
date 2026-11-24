#include "runtime.h"

#include "number_text.h"
#include "text_units.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace kas3 {
namespace vm {

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "float vectors require IEEE binary32");

namespace {
size_t strings_alive = 0;
size_t objects_alive = 0;
uint32_t failing_allocations = 0;
} // namespace

void fail_allocations(uint32_t p_count) {
	failing_allocations = p_count;
}

bool allocation_fails() {
	if (failing_allocations == 0) {
		return false;
	}
	failing_allocations--;
	return true;
}

size_t live_strings() {
	return strings_alive;
}
size_t live_objects() {
	return objects_alive;
}

Str *join_strings(std::string_view p_first, std::string_view p_second) {
	// A length is held in 32 bits: a longer text is refused as memory running out, never cut short.
	// Two halves of a character that meet are joined into its four bytes (vm/text_units.h).
	const size_t saved = joined_halves(p_first, p_second);
	const uint64_t size = (uint64_t)p_first.size() + p_second.size() - saved;
	const size_t header = size < UINT16_MAX ? sizeof(Str) : sizeof(LongStr);
	void *memory = size > UINT32_MAX || size >= SIZE_MAX - header || allocation_fails() ? nullptr : std::malloc(header + size + 1);
	if (!memory) {
		return nullptr;
	}
	Str *string;
	if (size < UINT16_MAX) {
		string = new (memory) Str();
		string->short_size = static_cast<uint16_t>(size);
	} else {
		auto *large = new (memory) LongStr();
		large->short_size = UINT16_MAX;
		large->byte_size = static_cast<uint32_t>(size);
		string = large;
	}
	string->references = 1;
	if (saved) {
		write_joined(p_first, p_second, string->data() + p_first.size() - 3);
		p_first.remove_suffix(3);
		p_second.remove_prefix(3);
	}
	if (!p_first.empty()) {
		std::memcpy(string->data(), p_first.data(), p_first.size());
	}
	if (!p_second.empty()) {
		std::memcpy(string->data() + size - p_second.size(), p_second.data(), p_second.size());
	}
	string->data()[size] = 0;
	strings_alive++;
	return string;
}

int32_t Str::measure_length() const {
	if (short_size != UINT16_MAX) {
		units = static_cast<uint16_t>(utf16_length(view()));
		return units;
	}
	const auto *large = static_cast<const LongStr *>(this);
	if (large->full_units == -1) {
		large->full_units = utf16_length(view());
	}
	return large->full_units;
}

Str *new_string(std::string_view p_text) {
	return join_strings(p_text, std::string_view());
}

void free_string(Str *p_string) {
	strings_alive--;
	std::free(p_string);
}

void list_init(Object *p_head) {
	p_head->gc_previous = p_head;
	p_head->gc_next = p_head;
}

void list_insert(Object *p_head, Object *p_object) {
	p_object->gc_previous = p_head->gc_previous;
	p_object->gc_next = p_head;
	p_head->gc_previous->gc_next = p_object;
	p_head->gc_previous = p_object;
}

void list_remove(Object *p_object) {
	if (p_object->gc_next) {
		p_object->gc_previous->gc_next = p_object->gc_next;
		p_object->gc_next->gc_previous = p_object->gc_previous;
		p_object->gc_previous = nullptr;
		p_object->gc_next = nullptr;
	}
}

void list_splice(Object *p_head, Object *p_from) {
	if (list_empty(p_from)) {
		return;
	}
	Object *first = p_from->gc_next;
	Object *last = p_from->gc_previous;
	first->gc_previous = p_head->gc_previous;
	p_head->gc_previous->gc_next = first;
	last->gc_next = p_head;
	p_head->gc_previous = last;
	list_init(p_from);
}

Object *new_object(uint32_t p_class, size_t p_fields, Object *p_list) {
	void *memory = allocation_fails() ? nullptr : std::calloc(1, sizeof(Object) + p_fields * sizeof(Slot));
	if (!memory) {
		return nullptr;
	}
	Object *object = new (memory) Object();
	object->references = 1;
	object->klass = p_class;
	if (p_list) {
		list_insert(p_list, object);
	}
	objects_alive++;
	return object;
}

void free_object(Object *p_object) {
	list_remove(p_object);
	objects_alive--;
	std::free(p_object);
}

Vector *new_vector(uint32_t p_class, uint32_t p_length, bool p_fixed, uint8_t p_bare, bool p_narrow, Object *p_list,
		bool p_bytes, VectorPlaces p_places) {
	const bool inline_places = p_places == VectorPlaces::Inline;
	size_t bytes = sizeof(Vector);
	if (inline_places) {
		const size_t width = place_size(p_bytes, p_narrow);
		if (p_length > (SIZE_MAX - bytes) / width) {
			return nullptr;
		}
		bytes += p_length * width;
	}
	void *memory = allocation_fails() ? nullptr : std::calloc(1, bytes);
	if (!memory) {
		return nullptr;
	}
	Vector *vector = new (memory) Vector();
	vector->references = 1;
	vector->klass = p_class;
	vector->fixed = p_fixed || inline_places;
	vector->bare = p_bare;
	vector->narrow = p_narrow;
	vector->bytes = p_bytes;
	vector->inline_places = inline_places;
	objects_alive++;
	if (inline_places) {
		vector->places = p_length ? vector + 1 : nullptr;
		vector->capacity = p_length;
	} else if (p_length) {
		vector->places = allocation_fails() ? nullptr : std::calloc(p_length, place_size(vector));
		if (!vector->places) {
			free_vector(vector);
			return nullptr;
		}
		vector->capacity = p_length;
	}
	vector->length = p_length;
	if (p_list) {
		list_insert(p_list, vector);
	}
	return vector;
}

bool vector_reserve(Vector *p_vector, uint32_t p_capacity) {
	if (p_capacity <= p_vector->capacity) {
		return true;
	}
	if (p_vector->inline_places) {
		return false;
	}
	// Half as much again, so that a run of pushes copies each element a bounded number of times.
	const uint64_t grown = (uint64_t)p_vector->capacity + p_vector->capacity / 2;
	const uint64_t capacity = grown > p_capacity ? grown : (uint64_t)p_capacity;
	const size_t size = place_size(p_vector);
	if (capacity > UINT32_MAX || capacity > SIZE_MAX / size) {
		return false;
	}
	void *memory = allocation_fails() ? nullptr : std::realloc(p_vector->places, capacity * size);
	if (!memory) {
		return false;
	}
	p_vector->places = memory;
	std::memset(static_cast<char *>(memory) + p_vector->capacity * size, 0, (capacity - p_vector->capacity) * size);
	p_vector->capacity = (uint32_t)capacity;
	return true;
}

void free_vector(Vector *p_vector) {
	list_remove(p_vector);
	if (!p_vector->inline_places) {
		std::free(p_vector->places);
	}
	objects_alive--;
	std::free(p_vector);
}

uint32_t to_uint32(double p_value) {
	if (p_value >= 0.0 && p_value < 4294967296.0) {
		return static_cast<uint32_t>(p_value);
	}
	if (!std::isfinite(p_value)) {
		return 0;
	}
	double whole = std::trunc(p_value);
	double wrapped = std::fmod(whole, 4294967296.0);
	if (wrapped < 0) {
		wrapped += 4294967296.0;
	}
	return (uint32_t)wrapped;
}

int32_t to_int32(double p_value) {
	if (p_value >= -2147483648.0 && p_value < 2147483648.0) {
		return static_cast<int32_t>(p_value);
	}
	return (int32_t)to_uint32(p_value);
}

std::string number_to_string(double p_value) {
	if (std::isnan(p_value)) {
		return "NaN";
	}
	if (std::isinf(p_value)) {
		return p_value < 0 ? "-Infinity" : "Infinity";
	}
	if (p_value == 0) {
		return "0";
	}
	// The fewest digits that read back as the same number.
	char buffer[40];
	for (int digits = 1; digits <= 17; digits++) {
		std::snprintf(buffer, sizeof(buffer), "%.*e", digits - 1, p_value);
		if (std::strtod(buffer, nullptr) == p_value) {
			break;
		}
	}
	// buffer is "[-]d[.ddd]e[+-]xx".
	std::string text(buffer);
	bool negative = text[0] == '-';
	size_t mark = text.find('e');
	std::string digits;
	for (size_t i = negative ? 1 : 0; i < mark; i++) {
		if (text[i] != '.') {
			digits += text[i];
		}
	}
	while (digits.size() > 1 && digits.back() == '0') {
		digits.pop_back();
	}
	int exponent = std::atoi(text.c_str() + mark + 1);
	int count = (int)digits.size();
	int point = exponent + 1; // Where the decimal point falls among the digits.
	std::string out = negative ? "-" : "";
	if (count <= point && point <= 21) {
		out += digits + std::string((size_t)(point - count), '0');
	} else if (0 < point && point <= 21) {
		out += digits.substr(0, (size_t)point) + "." + digits.substr((size_t)point);
	} else if (-6 < point && point <= 0) {
		out += "0." + std::string((size_t)(-point), '0') + digits;
	} else {
		out += digits.substr(0, 1);
		if (count > 1) {
			out += "." + digits.substr(1);
		}
		out += (exponent < 0 ? "e-" : "e+") + std::to_string(exponent < 0 ? -exponent : exponent);
	}
	return out;
}

double string_to_number(std::string_view p_text) {
	p_text = without_leading_space(p_text);
	while (!p_text.empty()) {
		size_t last = p_text.size() - 1, size = 0;
		while (last > 0 && (static_cast<unsigned char>(p_text[last]) & 0xC0) == 0x80) {
			last--;
		}
		if (!is_text_space(code_point_at(p_text, last, &size))) {
			break;
		}
		p_text.remove_suffix(p_text.size() - last);
	}
	if (p_text.empty()) {
		return 0;
	}
	std::string text(p_text);
	if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		double value = 0;
		for (size_t i = 2; i < text.size(); i++) {
			char c = text[i];
			int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
			if (digit < 0) {
				return std::nan("");
			}
			value = value * 16 + digit;
		}
		return value;
	}
	if (text == "0x" || text == "0X") {
		return std::nan("");
	}
	if (text == "Infinity" || text == "+Infinity") {
		return INFINITY;
	}
	if (text == "-Infinity") {
		return -INFINITY;
	}
	// Only digits, a sign, a point and an exponent: strtod alone would also take "inf", "nan" and hex floats.
	for (char c : text) {
		if (!((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) {
			return std::nan("");
		}
	}
	char *end = nullptr;
	double value = std::strtod(text.c_str(), &end);
	return end == text.c_str() + text.size() ? value : std::nan("");
}

int32_t utf16_length(std::string_view p_text) {
	uint32_t count = 0;
	for (unsigned char c : p_text) {
		count += (c & 0xC0) != 0x80; // A character starts here,
		count += c >= 0xF0; // and one of four bytes is two units.
	}
	return count;
}

namespace {

// The first UTF-16 unit of a code point: a character past U+FFFF starts with its high surrogate.
uint32_t first_unit(uint32_t p_code_point) {
	return p_code_point < 0x10000 ? p_code_point : 0xD800 + ((p_code_point - 0x10000) >> 10);
}

} // namespace

int compare_utf16(std::string_view p_a, std::string_view p_b) {
	size_t a = 0, b = 0;
	while (a < p_a.size() && b < p_b.size()) {
		size_t a_size = 0, b_size = 0;
		const uint32_t x = code_point_at(p_a, a, &a_size);
		const uint32_t y = code_point_at(p_b, b, &b_size);
		if (x != y) {
			const uint32_t x_unit = first_unit(x), y_unit = first_unit(y);
			// With the same high surrogate, the low ones are in the code points' order.
			return x_unit != y_unit ? (x_unit < y_unit ? -1 : 1) : (x < y ? -1 : 1);
		}
		a += a_size;
		b += b_size;
	}
	return a < p_a.size() ? 1 : b < p_b.size() ? -1 : 0;
}

} // namespace vm
} // namespace kas3
