#include "runtime.h"

#include "number_text.h"
#include "text_units.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#if defined(KAS3_TEST_COUNTS)
#include "../tests/script_read_allocations.h"
#endif

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
#if defined(KAS3_TEST_COUNTS)
	count_vm_allocation();
#endif
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

static Str *allocate_string(uint64_t p_size, size_t p_header) {
	if (p_size > UINT32_MAX || p_size >= SIZE_MAX - p_header || allocation_fails()) {
		return nullptr;
	}
	void *memory = std::malloc(p_header + p_size + 1);
	if (!memory) {
		return nullptr;
	}
	++strings_alive;
	const uint32_t size = static_cast<uint32_t>(p_size);
	return size < UINT16_MAX ? new (memory) Str{ { 1 }, size } : new (memory) LongStr{ { { 1 }, UINT16_MAX }, size, -1 };
}

Str *join_strings(std::string_view p_first, std::string_view p_second) {
	const size_t saved = joined_halves(p_first, p_second);
	const uint64_t size = static_cast<uint64_t>(p_first.size()) + p_second.size() - saved;
	const size_t header = size < UINT16_MAX ? sizeof(Str) : sizeof(LongStr);
	Str *string = allocate_string(size, header);
	if (!string) {
		return nullptr;
	}
	char *bytes = reinterpret_cast<char *>(string) + header;
	if (saved) {
		write_joined(p_first, p_second, bytes + p_first.size() - 3);
		p_first.remove_suffix(3);
		p_second.remove_prefix(3);
	}
	if (!p_first.empty()) {
		std::memcpy(bytes, p_first.data(), p_first.size());
	}
	if (!p_second.empty()) {
		std::memcpy(bytes + size - p_second.size(), p_second.data(), p_second.size());
	}
	bytes[size] = 0;
	return string;
}

Str *join_strings(const Str *p_first, const Str *p_second) {
	if (p_first && p_second) {
		const uint32_t left = p_first->sizes;
		const uint32_t right = p_second->sizes;
		if ((left | right) < UINT16_MAX) {
			const auto *first = reinterpret_cast<const char *>(p_first + 1);
			const auto *second = reinterpret_cast<const char *>(p_second + 1);
			// Only a trailing high half and a leading low half can change the byte size.
			if (left < 3 || right < 3 || static_cast<unsigned char>(second[0]) != 0xED ||
					!joined_halves({ first, left }, { second, right })) {
				const uint32_t size = left + right;
				const size_t header = size < UINT16_MAX ? sizeof(Str) : sizeof(LongStr);
				Str *joined = allocate_string(size, header);
				if (!joined) {
					return nullptr;
				}
				char *bytes = reinterpret_cast<char *>(joined) + header;
				std::memcpy(bytes, first, left);
				std::memcpy(bytes + left, second, right + 1);
				return joined;
			}
		}
	}
	return join_strings(JoinOperand(p_first), JoinOperand(p_second));
}

Str *append_string(Str **r_prefix, const Str *p_piece) {
	Str *prefix = *r_prefix;
	if (!prefix || !p_piece || prefix == p_piece || prefix->references != 1 || prefix->is_long() || p_piece->is_long()) {
		return join_strings(prefix, p_piece);
	}
	const auto left = prefix->view(), right = p_piece->view();
	const size_t size = left.size() + right.size();
	if (size >= UINT16_MAX || joined_halves(left, right)) {
		return join_strings(prefix, p_piece);
	}
	uint32_t sizes = static_cast<uint32_t>(size);
	if (prefix->measured() || p_piece->measured()) {
		sizes |= static_cast<uint32_t>(prefix->length() + p_piece->length() + 1) << 16;
	}
	void *memory = allocation_fails() ? nullptr : std::realloc(prefix, sizeof(Str) + size + 1);
	if (!memory) {
		return nullptr;
	}
	Str *joined = new (memory) Str{ { 1 }, sizes };
	std::memcpy(reinterpret_cast<char *>(joined + 1) + left.size(), right.data(), right.size() + 1);
	*r_prefix = nullptr;
	return joined;
}

Str *join_strings(JoinOperand p_first, JoinOperand p_second) {
	Str *joined = join_strings(p_first.bytes, p_second.bytes);
	if (joined && (!p_first.string || !p_second.string || p_first.string->measured() || p_second.string->measured())) {
		// A newly encountered piece is measured once. Joining two surrogate halves changes the
		// byte count but preserves both UTF-16 units.
		const auto units = [](JoinOperand p_part) -> uint32_t {
			return p_part.string ? static_cast<uint32_t>(p_part.string->length()) : static_cast<uint32_t>(p_part.bytes.size());
		};
		const uint32_t length = units(p_first) + units(p_second);
		if (joined->is_long()) {
			joined->sizes |= 1u << 16;
			static_cast<LongStr *>(joined)->full_units = static_cast<int32_t>(length);
		} else {
			joined->sizes |= (length + 1) << 16;
		}
	}
	return joined;
}

int32_t Str::measure_length() const {
	if (!is_long()) {
		const uint32_t count = utf16_length(view());
		sizes |= (count + 1) << 16;
		return static_cast<int32_t>(count);
	}
	const auto *large = static_cast<const LongStr *>(this);
	if (!measured()) {
		sizes |= 1u << 16;
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

// The capture array precedes the function. The collector may release them in either order.
struct alignas(16) MethodCaptures : Vector {
	Slot values[3];
	const Prepared *binding;
};
static constexpr size_t METHOD_OFFSET = sizeof(MethodCaptures);
static_assert(METHOD_OFFSET % alignof(Object) == 0);

const Prepared *method_binding(const Vector *p_bound) {
	return p_bound->paired ? static_cast<const MethodCaptures *>(p_bound)->binding : nullptr;
}

std::pair<Object *, Vector *> new_method_storage(uint32_t p_function, size_t p_fields, Object *p_functions,
		uint32_t p_array, Object *p_arrays, const Prepared *p_binding) {
	if (p_fields > (SIZE_MAX - METHOD_OFFSET - sizeof(Object)) / sizeof(Slot) || allocation_fails()) {
		return {};
	}
	auto *block = static_cast<char *>(std::calloc(1, METHOD_OFFSET + sizeof(Object) + p_fields * sizeof(Slot)));
	if (!block) {
		return {};
	}
	auto *array = new (block) MethodCaptures();
	array->places = array->values;
	array->binding = p_binding;
	array->inline_places = true;
	array->fixed = true;
	array->length = array->capacity = 3;
	auto *function = new (block + METHOD_OFFSET) Object();
	auto initialize = [](Object *p_object, uint32_t p_class, Object *p_list) {
		p_object->references = 1;
		p_object->paired = true;
		p_object->klass = p_class;
		if (p_list) {
			list_insert(p_list, p_object);
		}
	};
	initialize(array, p_array, p_arrays);
	initialize(function, p_function, p_functions);
	objects_alive += 2;
	return { function, array };
}

static void free_method_part(Object *p_object, char *p_block, Object *p_other) {
	p_object->paired = false;
	if (!p_other->paired) {
		std::free(p_block);
	}
}

void free_object(Object *p_object) {
	list_remove(p_object);
	objects_alive--;
	if (p_object->paired) {
		char *block = reinterpret_cast<char *>(p_object) - METHOD_OFFSET;
		free_method_part(p_object, block, reinterpret_cast<Vector *>(block));
	} else {
		std::free(p_object);
	}
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
	if (p_vector->paired) {
		char *block = reinterpret_cast<char *>(p_vector);
		free_method_part(p_vector, block, reinterpret_cast<Object *>(block + METHOD_OFFSET));
	} else {
		std::free(p_vector);
	}
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
