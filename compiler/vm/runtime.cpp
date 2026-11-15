#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace kas3 {
namespace vm {

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
	const uint64_t size = (uint64_t)p_first.size() + p_second.size();
	void *memory = size > UINT32_MAX || allocation_fails() ? nullptr : std::malloc(sizeof(Str) + (size_t)size + 1);
	if (!memory) {
		return nullptr;
	}
	Str *string = new (memory) Str();
	string->references = 1;
	string->size = (uint32_t)size;
	std::memcpy(string->data(), p_first.data(), p_first.size());
	std::memcpy(string->data() + p_first.size(), p_second.data(), p_second.size());
	string->data()[size] = 0;
	strings_alive++;
	return string;
}

Str *new_string(std::string_view p_text) {
	return join_strings(p_text, std::string_view());
}

void free_string(Str *p_string) {
	strings_alive--;
	std::free(p_string);
}

Object *new_object(uint32_t p_class, size_t p_fields) {
	void *memory = allocation_fails() ? nullptr : std::calloc(1, sizeof(Object) + p_fields * sizeof(Slot));
	if (!memory) {
		return nullptr;
	}
	Object *object = new (memory) Object();
	object->references = 1;
	object->klass = p_class;
	objects_alive++;
	return object;
}

void free_object(Object *p_object) {
	objects_alive--;
	std::free(p_object);
}

Vector *new_vector(uint32_t p_class, uint32_t p_length, bool p_fixed) {
	void *memory = allocation_fails() ? nullptr : std::calloc(1, sizeof(Vector));
	if (!memory) {
		return nullptr;
	}
	Vector *vector = new (memory) Vector();
	vector->references = 1;
	vector->klass = p_class;
	vector->fixed = p_fixed;
	objects_alive++;
	if (!vector_reserve(vector, p_length)) {
		free_vector(vector);
		return nullptr;
	}
	vector->length = p_length;
	return vector;
}

bool vector_reserve(Vector *p_vector, uint32_t p_capacity) {
	if (p_capacity <= p_vector->capacity) {
		return true;
	}
	// Half as much again, so that a run of pushes copies each element a bounded number of times.
	const uint64_t grown = (uint64_t)p_vector->capacity + p_vector->capacity / 2;
	const uint64_t capacity = grown > p_capacity ? grown : (uint64_t)p_capacity;
	if (capacity > UINT32_MAX) {
		return false;
	}
	void *memory = allocation_fails() ? nullptr : std::realloc(p_vector->data, capacity * sizeof(Slot));
	if (!memory) {
		return false;
	}
	p_vector->data = static_cast<Slot *>(memory);
	std::memset(static_cast<void *>(p_vector->data + p_vector->capacity), 0, (capacity - p_vector->capacity) * sizeof(Slot));
	p_vector->capacity = (uint32_t)capacity;
	return true;
}

void free_vector(Vector *p_vector) {
	std::free(p_vector->data);
	objects_alive--;
	std::free(p_vector);
}

uint32_t to_uint32(double p_value) {
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
	auto space = [](char p_c) { return p_c == ' ' || p_c == '\t' || p_c == '\n' || p_c == '\r' || p_c == '\f' || p_c == '\v'; };
	while (!p_text.empty() && space(p_text.front())) {
		p_text.remove_prefix(1);
	}
	while (!p_text.empty() && space(p_text.back())) {
		p_text.remove_suffix(1);
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

int32_t character_count(std::string_view p_text) {
	int32_t count = 0;
	for (unsigned char c : p_text) {
		count += (c & 0xC0) != 0x80;
	}
	return count;
}

} // namespace vm
} // namespace kas3
