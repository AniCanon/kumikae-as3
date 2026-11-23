// Number conversions and formatting. Every expected value was checked against Node, which follows the
// same ECMAScript rules:  node -e 'console.log(String(x), x | 0, x >>> 0, Number(s))'
#include "check.h"

#include "any.h"
#include "fields.h"
#include "json_text.h"
#include "number_text.h"
#include "numeric.h"
#include "regex.h"
#include "runtime.h"
#include "text_methods.h"
#include "text_units.h"

#include <cmath>
#include <string>

using namespace kas3::vm;

static void test_integers() {
	CHECK_EQ(to_int32(0.0), 0);
	CHECK_EQ(to_int32(-0.0), 0);
	CHECK_EQ(to_int32(3.99), 3);
	CHECK_EQ(to_int32(-3.99), -3);
	CHECK_EQ(to_int32(std::nan("")), 0);
	CHECK_EQ(to_int32(INFINITY), 0);
	CHECK_EQ(to_int32(-INFINITY), 0);
	CHECK_EQ(to_int32(2147483647.0), 2147483647);
	CHECK_EQ(to_int32(2147483648.0), (int32_t)-2147483648LL);
	CHECK_EQ(to_int32(4294967296.0), 0);
	CHECK_EQ(to_int32(4294967301.0), 5);
	CHECK_EQ(to_int32(-2147483649.0), 2147483647);
	CHECK_EQ(to_int32(1e10), 1410065408);
	CHECK_EQ(to_int32(-1e10), -1410065408);
	CHECK_EQ(to_int32(1e300), 0);
	CHECK_EQ(to_int32(-1.5), -1);
	CHECK_EQ(to_int32(std::nextafter(2147483648.0, 0.0)), INT32_MAX);
	CHECK_EQ(to_int32(std::nextafter(-2147483648.0, -INFINITY)), INT32_MIN);
	CHECK_EQ(to_int32(2147483648.75), INT32_MIN);
	CHECK_EQ(to_uint32(std::nextafter(4294967296.0, 0.0)), UINT32_MAX);
	CHECK_EQ(to_uint32(4294967296.75), 0u);
	CHECK_EQ(to_uint32(-0.75), 0u);
	CHECK_EQ(to_uint32(-1.0), 4294967295u);
	CHECK_EQ(to_uint32(-1.5), 4294967295u);
	CHECK_EQ(to_uint32(4294967296.0 + 5), 5u);
	CHECK_EQ(to_uint32(3.99), 3u);
	CHECK_EQ(to_uint32(std::nan("")), 0u);
	CHECK_EQ(to_uint32(-4294967297.0), 4294967295u);
}

static void test_number_text() {
	CHECK_EQ(number_to_string(0.0), std::string("0"));
	CHECK_EQ(number_to_string(-0.0), std::string("0"));
	CHECK_EQ(number_to_string(1.0), std::string("1"));
	CHECK_EQ(number_to_string(-1.5), std::string("-1.5"));
	CHECK_EQ(number_to_string(100.0), std::string("100"));
	CHECK_EQ(number_to_string(0.1), std::string("0.1"));
	CHECK_EQ(number_to_string(0.1 + 0.2), std::string("0.30000000000000004"));
	CHECK_EQ(number_to_string(1.0 / 3.0), std::string("0.3333333333333333"));
	CHECK_EQ(number_to_string(1e21), std::string("1e+21"));
	CHECK_EQ(number_to_string(1e20), std::string("100000000000000000000"));
	CHECK_EQ(number_to_string(123456789012345680000.0), std::string("123456789012345680000"));
	CHECK_EQ(number_to_string(1.5e300), std::string("1.5e+300"));
	CHECK_EQ(number_to_string(1e-6), std::string("0.000001"));
	CHECK_EQ(number_to_string(1e-7), std::string("1e-7"));
	CHECK_EQ(number_to_string(1.25e-7), std::string("1.25e-7"));
	CHECK_EQ(number_to_string(0.000123), std::string("0.000123"));
	CHECK_EQ(number_to_string(5e-324), std::string("5e-324"));
	CHECK_EQ(number_to_string(1.7976931348623157e308), std::string("1.7976931348623157e+308"));
	CHECK_EQ(number_to_string(4294967295.0), std::string("4294967295"));
	CHECK_EQ(number_to_string(9007199254740993.0), std::string("9007199254740992"));
	CHECK_EQ(number_to_string(123.456), std::string("123.456"));
	CHECK_EQ(number_to_string(std::nan("")), std::string("NaN"));
	CHECK_EQ(number_to_string(INFINITY), std::string("Infinity"));
	CHECK_EQ(number_to_string(-INFINITY), std::string("-Infinity"));
}

static void test_text_number() {
	CHECK_EQ(string_to_number("42"), 42.0);
	CHECK_EQ(string_to_number("  -1.5e2\n"), -150.0);
	CHECK_EQ(string_to_number(""), 0.0);
	CHECK_EQ(string_to_number("   "), 0.0);
	CHECK_EQ(string_to_number("0x1F"), 31.0);
	CHECK_EQ(string_to_number(".5"), 0.5);
	CHECK_EQ(string_to_number("5."), 5.0);
	CHECK_EQ(string_to_number("Infinity"), (double)INFINITY);
	CHECK_EQ(string_to_number("-Infinity"), (double)-INFINITY);
	CHECK(std::isnan(string_to_number("abc")));
	CHECK(std::isnan(string_to_number("12px")));
	CHECK(std::isnan(string_to_number("1 2")));
	CHECK(std::isnan(string_to_number("inf")));
	CHECK(std::isnan(string_to_number("nan")));
	CHECK(std::isnan(string_to_number("0x")));
	CHECK(std::isnan(string_to_number("0xZZ")));
	CHECK(std::isnan(string_to_number("--1")));
	// AS3's length is in UTF-16 units: a character past U+FFFF counts two.
	CHECK_EQ(utf16_length("abc"), 3);
	CHECK_EQ(utf16_length("\xC3\xA9t\xC3\xA9"), 3);
	CHECK_EQ(utf16_length("\xF0\x9F\x98\x80"), 2);
	CHECK_EQ(utf16_length(""), 0);
	// And its order is the units': U+FFFD comes after a character past U+FFFF, whose first unit is a
	// surrogate (U+D83D), though its code point is smaller.
	CHECK(compare_utf16("\xF0\x9F\x98\x80", "\xEF\xBF\xBD") < 0);
	CHECK(compare_utf16("abc", "abd") < 0);
	CHECK(compare_utf16("ab", "abc") < 0);
	CHECK(compare_utf16("b", "abc") > 0);
	CHECK_EQ(compare_utf16("\xC3\xA9", "\xC3\xA9"), 0);
	CHECK(compare_utf16("\xF0\x9F\x98\x80", "\xF0\x9F\x98\x81") < 0);
}

// A text too long for a string's 32-bit length is refused, never cut short. Its characters are not
// read, so the text need not be there.
// An address with a heap tag in its top byte (Android's, on arm64) is held, and comes back with its tag;
// an untagged one beside it is untouched. Nothing is read through these: they only travel.
static void test_tagged_addresses() {
	Str *tagged = reinterpret_cast<Str *>(static_cast<uintptr_t>(0xB400001234567890ull));
	const Slot held = any::string(tagged);
	CHECK(any::tag(held) == AnyTag::String);
	CHECK(any::pointer(held) == tagged);
	CHECK(any::counted(held) == tagged);
	Str *plain = new_string("plain");
	const Slot beside = any::string(plain);
	CHECK(any::pointer(beside) == plain);
	CHECK(any::tag(beside) == AnyTag::String);
	free_string(plain);
	// The flag that says "tagged" is never taken for one of the values without a pointer.
	CHECK(any::tag(any::null()) == AnyTag::Null);
	CHECK(any::tag(any::boolean(true)) == AnyTag::Boolean);
	CHECK(any::to_bool_payload(any::boolean(true)));
	// And a held address, tagged or not, is told from every value that holds nothing.
	CHECK(any::holds(held) && any::holds(beside) && any::holds(any::object(reinterpret_cast<Object *>(tagged))));
	for (const Slot nothing : { any::undefined(), any::null(), any::boolean(false), any::boolean(true), any::hole(), any::integer(0), any::integer(-1),
				 any::integer(INT32_MIN), any::number(0.5), any::number(-0.0), any::number(std::nan("")), any::number(1e300), any::number(-1e-300) }) {
		CHECK(!any::holds(nothing));
	}
}

static void test_long_text() {
	const char *start = "x";
	CHECK(new_string(std::string_view(start, (size_t)UINT32_MAX + 1)) == nullptr);
	fail_allocations(1);
	CHECK(new_string("short") == nullptr);
	Str *after = new_string("short"); // Only that one failed.
	CHECK(after != nullptr);
	free_string(after);
}

static void test_counts() {
	size_t strings = live_strings();
	size_t objects = live_objects();
	Str *text = new_string("hello");
	CHECK_EQ(std::string(text->view()), std::string("hello"));
	CHECK_EQ(text->references, 1u);
	retain(text);
	CHECK(!release(text));
	CHECK(release(text));
	free_string(text);
	Object *object = new_object(3, 2, nullptr);
	CHECK_EQ(object->klass, 3u);
	CHECK_EQ(slot_at(object, 1).bits, (uint64_t)0);
	CHECK_EQ(live_objects(), objects + 1);
	CHECK(release(object));
	free_object(object);
	CHECK_EQ(live_strings(), strings);
	CHECK_EQ(live_objects(), objects);
	retain(nullptr);
	CHECK(!release(nullptr));
}

// Math, where C's library and AS3 differ. Checked against Node: Math.round(-0.5), Math.min(0, -0), ...
static void test_math() {
	using kas3::ir::MathFunction;
	const double inf = INFINITY;
	CHECK_EQ(math_unary(MathFunction::Round, 2.5), 3.0);
	CHECK_EQ(math_unary(MathFunction::Round, -2.5), -2.0);
	CHECK_EQ(math_unary(MathFunction::Round, 0.49999999999999994), 0.0);
	CHECK(std::signbit(math_unary(MathFunction::Round, -0.5)));
	CHECK(std::signbit(math_unary(MathFunction::Round, -0.2)));
	CHECK_EQ(math_unary(MathFunction::Round, 1e300), 1e300);
	CHECK(std::isnan(math_unary(MathFunction::Round, NAN)));
	CHECK_EQ(math_unary(MathFunction::Sqrt, 16), 4.0);
	CHECK_EQ(math_unary(MathFunction::Abs, -2.5), 2.5);
	CHECK(std::signbit(math_min(0.0, -0.0)) && std::signbit(math_min(-0.0, 0.0)));
	CHECK(!std::signbit(math_max(0.0, -0.0)) && !std::signbit(math_max(-0.0, 0.0)));
	CHECK(std::isnan(math_max(NAN, 1)) && std::isnan(math_min(1, NAN)));
	CHECK_EQ(math_max(1, 2), 2.0);
	CHECK_EQ(math_min(1, 2), 1.0);
	CHECK(std::isnan(math_pow(1, inf)) && std::isnan(math_pow(-1, -inf)) && std::isnan(math_pow(1, NAN)));
	CHECK_EQ(math_pow(NAN, 0), 1.0);
	CHECK_EQ(math_pow(2, 10), 1024.0);
	for (int i = 0; i < 1000; i++) {
		const double value = math_random();
		CHECK(value >= 0 && value < 1);
	}
}

// A vector's memory: its length, its room, and that it is counted among the objects alive.
static void test_vectors() {
	const size_t objects = live_objects();
	Vector *vector = new_vector(7, 3, false, 0, false, nullptr);
	CHECK(vector != nullptr);
	CHECK_EQ(vector->klass, 7u);
	CHECK_EQ(vector->references, 1u);
	CHECK_EQ(vector->length, 3u);
	CHECK(!vector->fixed);
	CHECK(vector->capacity >= 3);
	for (uint32_t i = 0; i < 3; i++) {
		CHECK_EQ(slots_of(vector)[i].bits, (uint64_t)0);
		slots_of(vector)[i].i = (int32_t)i + 1;
	}
	CHECK(vector_reserve(vector, 1000));
	CHECK(vector->capacity >= 1000);
	CHECK_EQ(vector->length, 3u);
	CHECK_EQ(slots_of(vector)[2].i, 3);
	CHECK_EQ(slots_of(vector)[999].bits, (uint64_t)0); // Room that was added is empty.
	CHECK(vector_reserve(vector, 2)); // Never shrinks.
	CHECK(vector->capacity >= 1000);
	CHECK_EQ(live_objects(), objects + 1);
	free_vector(vector);
	CHECK_EQ(live_objects(), objects);
	Vector *empty = new_vector(7, 0, true, 0, false, nullptr);
	CHECK(empty && empty->length == 0 && empty->fixed);
	free_vector(empty);
	CHECK_EQ(live_objects(), objects);
	// A vector of ints or of uints: each place is four bytes, the number itself, and a place read as a slot
	// holds it in its first four bytes with nothing after them. The sanitizer build holds the room to that.
	Vector *bytes = new_vector(7, 3, false, 0, true, nullptr, true);
	CHECK(bytes && place_size(bytes) == 1);
	bytes_of(bytes)[2] = 255;
	CHECK(vector_reserve(bytes, 1000));
	CHECK_EQ(place(bytes, 2).bits, uint64_t{255});
	CHECK_EQ(bytes_of(bytes)[999], 0);
	free_vector(bytes);
	Vector *narrow = new_vector(7, 3, false, 0, true, nullptr);
	CHECK(narrow && narrow->narrow && place_size(narrow) == 4);
	CHECK_EQ(place_size(vector == nullptr ? narrow : narrow), sizeof(int32_t));
	for (uint32_t i = 0; i < 3; i++) {
		CHECK_EQ(numbers_of(narrow)[i], 0);
		Slot value;
		value.bits = 0;
		value.u = 4000000000u + i;
		set_place(narrow, i, value);
	}
	CHECK(vector_reserve(narrow, 1000));
	CHECK_EQ(place(narrow, 2).bits, (uint64_t)4000000002u);
	CHECK_EQ(numbers_of(narrow)[999], 0);
	numbers_of(narrow)[999] = -5;
	move_places(narrow, 0, narrow, 998, 2);
	CHECK_EQ(place(narrow, 1).i, -5);
	CHECK_EQ(place(narrow, 1).bits, (uint64_t)0xfffffffbu);
	clear_places(narrow, 0, 1000);
	CHECK_EQ(numbers_of(narrow)[1], 0);
	free_vector(narrow);
	CHECK_EQ(live_objects(), objects);
}

static void test_fixed_array_allocation() {
	const size_t objects = live_objects();
	struct Shape {
		const char *name;
		kas3::ir::Kind kind;
		size_t width;
	};
	const Shape shapes[] = {
		{ "[]byte", kas3::ir::Kind::U8, 1 },
		{ "[]float", kas3::ir::Kind::F32, 4 },
		{ "[]int", kas3::ir::Kind::I32, 4 },
		{ "[]String", kas3::ir::Kind::Str, sizeof(Slot) },
	};
	for (const auto &[name, kind, width] : shapes) {
		kas3::ir::Class klass;
		klass.name = name;
		klass.element = kind;
		Vector *array = new_vector_of(klass, 7, 3, true, nullptr);
		CHECK(array);
		if (!array) {
			continue;
		}
		CHECK(array->fixed && array->length == 3 && array->capacity == 3);
		CHECK_EQ(array->places, static_cast<void *>(array + 1));
		CHECK_EQ(place_size(array), width);
		CHECK_EQ(place(array, 2).bits, uint64_t{0});
		CHECK(!vector_reserve(array, 4));
		CHECK_EQ(array->length, 3u);
		free_vector(array);
	}

	kas3::ir::Class empty_class;
	empty_class.name = "[]int";
	empty_class.element = kas3::ir::Kind::I32;
	Vector *empty = new_vector_of(empty_class, 7, 0, true, nullptr);
	CHECK(empty && empty->length == 0 && empty->capacity == 0 && !empty->places);
	if (empty) {
		CHECK(!vector_reserve(empty, 1));
		free_vector(empty);
	}
	fail_allocations(1);
	CHECK(!new_vector_of(empty_class, 7, 3, true, nullptr));
	fail_allocations(0);
	CHECK_EQ(live_objects(), objects);
}

// The untyped value. Expected values checked against Node:
//   node -e 'for (const x of [undefined, null, true, 0, -0, 1.5, NaN, "", "12", "abc"])
//              console.log(String(x), typeof x, Number(x), Boolean(x))'
static void test_any() {
	using namespace kas3::vm;
	Slot fresh;
	fresh.bits = 0;
	CHECK(any::tag(fresh) == AnyTag::Undefined); // A slot no one has written is undefined.
	CHECK(any::tag(any::null()) == AnyTag::Null);
	CHECK(any::tag(any::boolean(true)) == AnyTag::Boolean && any::to_bool_payload(any::boolean(true)));
	CHECK(!any::to_bool_payload(any::boolean(false)));
	for (double value : { 0.0, -0.0, 1.5, -1e308, 5e-324, double(INFINITY), -double(INFINITY) }) {
		const Slot held = any::number(value);
		CHECK(any::tag(held) == AnyTag::Number);
		CHECK_EQ(any::to_number_payload(held), value);
		CHECK_EQ(std::signbit(any::to_number_payload(held)), std::signbit(value));
	}
	CHECK(std::isnan(any::to_number_payload(any::number(NAN))) && any::tag(any::number(-NAN)) == AnyTag::Number);
	// An int whose low bits look like a pointer's tag is still an int.
	for (int32_t value : { 0, -1, 1, 2, 3, 4, 5, 8, 16, 24, INT32_MIN, INT32_MAX }) {
		CHECK(any::tag(any::integer(value)) == AnyTag::Int);
		CHECK_EQ(any::to_int_payload(any::integer(value)), value);
	}
	Str *text = new_string("12");
	Str *word = new_string("abc");
	Str *empty = new_string("");
	CHECK(any::tag(any::string(text)) == AnyTag::String && any::pointer(any::string(text)) == text);
	CHECK(any::counted(any::string(text)) == text && any::counted(any::number(1)) == nullptr);

	CHECK_EQ(any::text(fresh).value_or("?"), std::string("undefined"));
	CHECK_EQ(any::text(any::null()).value_or("?"), std::string("null"));
	CHECK_EQ(any::text(any::boolean(false)).value_or("?"), std::string("false"));
	CHECK_EQ(any::text(any::number(-0.0)).value_or("?"), std::string("0"));
	CHECK_EQ(any::text(any::number(1.5)).value_or("?"), std::string("1.5"));
	CHECK_EQ(any::text(any::integer(-7)).value_or("?"), std::string("-7"));
	CHECK_EQ(any::text(any::string(word)).value_or("?"), std::string("abc"));
	CHECK(!any::text(any::godot_value(reinterpret_cast<void *>(0x1000))).has_value());

	CHECK(std::isnan(any::to_number(fresh)));
	CHECK_EQ(any::to_number(any::null()), 0.0);
	CHECK_EQ(any::to_number(any::boolean(true)), 1.0);
	CHECK_EQ(any::to_number(any::string(text)), 12.0);
	CHECK(std::isnan(any::to_number(any::string(word))));
	CHECK_EQ(any::to_number(any::string(empty)), 0.0);
	CHECK(!any::to_boolean(fresh) && !any::to_boolean(any::null()) && !any::to_boolean(any::number(-0.0)));
	CHECK(!any::to_boolean(any::number(NAN)) && !any::to_boolean(any::string(empty)));
	CHECK(any::to_boolean(any::string(word)) && any::to_boolean(any::integer(-1)));

	CHECK_EQ(std::string(any::type_of(fresh)), std::string("undefined"));
	CHECK_EQ(std::string(any::type_of(any::null())), std::string("object"));
	CHECK_EQ(std::string(any::type_of(any::integer(3))), std::string("number"));
	CHECK_EQ(std::string(any::type_of(any::string(word))), std::string("string"));
	CHECK_EQ(std::string(any::type_of(any::boolean(true))), std::string("boolean"));

	// === and ==, as ECMAScript has them: node -e 'console.log(null == undefined, null === undefined, ...)'
	Str *other_word = new_string("abc");
	CHECK(*any::strict_equal(any::string(word), any::string(other_word)));
	CHECK(*any::strict_equal(any::integer(5), any::number(5)));
	CHECK(!*any::strict_equal(any::null(), fresh));
	CHECK(*any::loose_equal(any::null(), fresh));
	CHECK(!*any::loose_equal(any::null(), any::number(0)));
	CHECK(*any::loose_equal(any::string(text), any::integer(12)));
	CHECK(*any::loose_equal(any::boolean(true), any::number(1)));
	CHECK(!*any::strict_equal(any::number(NAN), any::number(NAN)));
	CHECK(*any::strict_equal(any::number(0.0), any::number(-0.0)));
	CHECK(!*any::loose_equal(any::string(empty), fresh));
	CHECK(!any::strict_equal(any::godot_value(reinterpret_cast<void *>(0x1000)), any::godot_value(reinterpret_cast<void *>(0x2000))).has_value());

	Wide *big = new_wide(9007199254740993);
	CHECK(any::tag(any::wide(big)) == AnyTag::Wide && any::counted(any::wide(big)) == big);
	CHECK_EQ(any::text(any::wide(big)).value_or("?"), std::string("9007199254740993"));
	CHECK_EQ(std::string(any::type_of(any::wide(big))), std::string("number"));
	free_wide(big);
	for (Str *each : { text, word, empty, other_word }) {
		free_string(each);
	}
}

// Text by UTF-16 unit. A character past U+FFFF is two units; a half held alone is three bytes.
static void test_text_units() {
	const std::string face = "a\xF0\x9F\x98\x80" "b"; // a, U+1F600, b
	const std::string high = "\xED\xA0\xBD", low = "\xED\xB8\x80"; // U+D83D, U+DE00 alone
	CHECK_EQ(unit_at(face, 0).value_or(0), 'a');
	CHECK_EQ(unit_at(face, 1).value_or(0), 0xD83D);
	CHECK_EQ(unit_at(face, 2).value_or(0), 0xDE00);
	CHECK_EQ(unit_at(face, 3).value_or(0), 'b');
	CHECK(!unit_at(face, 4));
	CHECK_EQ(units_between(face, 1, 2), high);
	CHECK_EQ(units_between(face, 2, 4), low + "b");
	CHECK_EQ(units_between(face, 1, 3), std::string("\xF0\x9F\x98\x80"));
	CHECK_EQ(units_between(face, 0, 4), face);
	CHECK_EQ(units_between(face, 3, 3), std::string());
	CHECK_EQ(joined_halves(high, low), (size_t)2);
	CHECK_EQ(joined_halves(low, high), (size_t)0);
	std::string joined = "a" + high;
	append_text(&joined, low + "b");
	CHECK_EQ(joined, face);
	std::string built;
	append_unit(&built, 0xD83D);
	append_unit(&built, 0xDE00);
	CHECK_EQ(built, std::string("\xF0\x9F\x98\x80"));
	CHECK(sizeof(Str) <= 3 * sizeof(uint32_t));
	Str *whole = join_strings("a" + high, low + "b");
	CHECK(whole && whole->view() == face);
	CHECK(whole && whole->length() == 4);
	free_string(whole);
	CHECK_EQ(find_units(face, high, 0), 1);
	CHECK_EQ(find_units(face, low, 0), 2);
	CHECK_EQ(find_units(face, low + "b", 0), 2);
	CHECK_EQ(find_units(face, "b", 2), 3);
	CHECK_EQ(find_units(face, "\xF0\x9F\x98\x80", 2), -1);
	CHECK_EQ(find_units(face, "", 9), 4);
	CHECK_EQ(find_last_units(face, high, 1), 1);
	CHECK_EQ(find_last_units(face, high, 0), -1);
	CHECK_EQ(find_last_units(face, "\xF0\x9F\x98\x80", 2), 1);
	CHECK_EQ(find_last_units(face, "a", 99), 0);
	CHECK_EQ(upper_case("abc \xC3\xA9 \xC3\x9F \xC4\xB1"), std::string("ABC \xC3\x89 \xC3\x9F I")); // é to É, ß stays, ı to I
	CHECK_EQ(lower_case("ABC \xC3\x89 \xD0\x96"), std::string("abc \xC3\xA9 \xD0\xB6"));
	CHECK_EQ(to_upper(0x3B1), (uint32_t)0x391);
	CHECK_EQ(to_lower(0x1C5), (uint32_t)0x1C6);
	CHECK_EQ(to_upper(0xFF41), (uint32_t)0xFF21);
	CHECK_EQ(to_upper(0xD83D), (uint32_t)0xD83D);
	CHECK_EQ(upper_case(face), std::string("A\xF0\x9F\x98\x80" "B"));
}

// A pattern's first match from unit p_from, as "start-end" for the match and each group ("_" for a group
// that took no part); "none", "limit", or "error: why".
static std::string regex_find(std::u16string_view p_pattern, const char *p_flags, std::u16string_view p_text, uint32_t p_from = 0,
		regex::Limits p_limits = regex::Limits()) {
	std::string error;
	const std::optional<regex::Program> program = regex::compile(p_pattern, regex::read_flags(p_flags), &error);
	if (!program) {
		return "error: " + error;
	}
	std::vector<int32_t> captures;
	const regex::Outcome outcome = regex::search(*program, p_text, p_from, &captures, p_limits);
	if (outcome != regex::Outcome::Found) {
		return outcome == regex::Outcome::Limit ? "limit" : "none";
	}
	std::string out;
	for (size_t i = 0; i + 1 < captures.size(); i += 2) {
		out += (i ? " " : "") + (captures[i] < 0 ? std::string("_") : std::to_string(captures[i]) + "-" + std::to_string(captures[i + 1]));
	}
	return out;
}

// Every case but the marked ones was checked against Node (`new RegExp(p, f + "gd")`, lastIndex set to the
// start, then exec's indices), which keeps ECMAScript 3's rules for all of them. Node vouches for
// ECMAScript, not for Flash, whose PCRE parts from it in places, and this engine follows ECMAScript there
// (unverified against Flash): a loop's groups are unset as each turn begins (`((a)|b)+`, `(?:(a)|b)+`,
// `(z)((a+)?(b+)?(c))*`); a reference to a group that took no part takes nothing (`(a)?\1b`); `$` is only
// the end (not before a last line break), `.` and `^`/`$` know four line breaks, `\s` is ECMAScript's
// white space; and the text is matched by UTF-16 unit, where PCRE in UTF-8 takes a character past U+FFFF
// as one.
static void test_regex_matches() {
	struct Case {
		std::u16string_view pattern;
		const char *flags;
		std::u16string_view text;
		uint32_t from;
		const char *expected;
	};
	const Case cases[] = {
		{ u"a+b", "", u"xaaab", 0, "1-5" },
		{ u"a+?", "", u"aaa", 0, "0-1" },
		{ u"a*?b", "", u"aab", 0, "0-3" },
		{ u"(a|ab)(c|bcd)(d*)", "", u"abcd", 0, "0-4 0-1 1-4 4-4" },
		{ u"((a)|b)+", "", u"ab", 0, "0-2 1-2 _" },
		{ u"(z)((a+)?(b+)?(c))*", "", u"zaacbbbcac", 0, "0-10 0-1 8-10 8-9 _ 9-10" },
		{ u"(a*)*", "", u"b", 0, "0-0 _" },
		{ u"(a*)b\\1+", "", u"baaaac", 0, "0-1 0-0" },
		{ u"(?=(a+))", "", u"baaabac", 0, "1-1 1-4" },
		{ u"(?=(a+))a*b\\1", "", u"baaabac", 0, "3-6 3-4" },
		{ u"(.*?)a(?!(a+)b\\2c)\\2(.*)", "", u"baaabaac", 0, "0-8 0-2 _ 3-8" },
		{ u"^abc$", "", u"abc", 0, "0-3" },
		{ u"^b", "m", u"a\nb", 0, "2-3" },
		{ u"a$", "m", u"a\nb", 0, "0-1" },
		{ u"a$", "", u"a\nb", 0, "none" },
		{ u"\\bfoo\\b", "", u"a foo.", 0, "2-5" },
		{ u"\\Boo", "", u"foo", 0, "1-3" },
		{ u"[a-c]+", "", u"xxbcay", 0, "2-5" },
		{ u"[^a-c]+", "", u"abcxyz", 0, "3-6" },
		{ u"[\\d.]+", "", u"v1.25x", 0, "1-5" },
		{ u"\\w+@\\w+\\.com", "", u"mail: joe@site.com!", 0, "6-18" },
		{ u"\\s+", "", u"a \t\nb", 0, "1-4" },
		{ u"\\S+", "", u"  ab ", 0, "2-4" },
		{ u"\\D\\W", "", u"1a!", 0, "1-3" },
		{ u"ABC", "i", u"xabcx", 0, "1-4" },
		{ u"[a-z]+", "i", u"12HeLLo", 0, "2-7" },
		{ u"\\u0041\\x42C", "", u"ABC", 0, "0-3" },
		{ u"\\cJ", "", u"a\nb", 0, "1-2" },
		{ u"a{2}", "", u"aaaa", 0, "0-2" },
		{ u"a{2,}", "", u"aaaa", 0, "0-4" },
		{ u"a{1,3}", "", u"aaaa", 0, "0-3" },
		{ u"a{1,3}?", "", u"aaaa", 0, "0-1" },
		{ u"x{0}y", "", u"xy", 0, "1-2" },
		{ u"a.c", "", u"a\nc abc", 0, "4-7" },
		{ u"a.c", "s", u"a\nc", 0, "0-3" },
		{ u"b", "", u"abcb", 2, "3-4" },
		{ u"", "", u"abc", 1, "1-1" },
		{ u"(a)|(b)", "", u"b", 0, "0-1 _ 0-1" },
		{ u"[]a", "", u"a", 0, "none" },
		{ u"[^]", "", u"\n", 0, "0-1" },
		{ u"(?:ab)+", "", u"ababx", 0, "0-4" },
		{ u"\\1(a)", "", u"aa", 0, "0-1 0-1" },
		{ u"(a)\\1", "i", u"aA", 0, "0-2 0-1" },
		{ u"[\\u00e0-\\u00ff]+", "i", u"Àé", 0, "0-2" },
		{ u"(?<=\\$)\\d+", "", u"cost $42", 0, "6-8" },
		{ u"(?<!\\$)\\b\\d+", "", u"$4 and 5", 0, "7-8" },
		{ u"\\d+(?=%)", "", u"50 60%", 0, "3-5" },
		{ u"s", "i", u"ſ", 0, "none" }, // A long s is not an s: its capital is ASCII, and it is not.
		{ u"ſ", "i", u"S", 0, "none" },
		{ u"(a+)+b", "", u"aaab", 0, "0-4 0-3" },
		{ u"[-a]+", "", u"-a-", 0, "0-3" },
		{ u"[a-]+", "", u"a--", 0, "0-3" },
		{ u"[\\w-]+", "", u"a-b c", 0, "0-3" },
		{ std::u16string_view(u"\\0", 2), "", std::u16string_view(u"a\0", 2), 0, "1-2" },
		{ u"\\$\\.\\^", "", u"$.^", 0, "0-3" },
		{ u"\U0001F600", "", u"x\U0001F600", 0, "1-3" }, // By unit: a character past U+FFFF is two.
		{ u".", "", u"\U0001F600", 0, "0-1" },
		{ u"(a)|b", "", u"b", 0, "0-1 _" },
		{ u"(?:(a)|b)+", "", u"ab", 0, "0-2 _" }, // Each turn of a loop starts with its groups unset.
		{ u"(a)?\\1b", "", u"b", 0, "0-1 _" }, // A group that took no part is empty to refer back to.
		{ u"a|", "", u"b", 0, "0-0" },
		{ u"[\\b]", "", u"a\bb", 0, "1-2" },
		{ u"\\ ", "", u" ", 0, "0-1" },
		{ u"a.b", "", u"a b a\rb axb", 0, "8-11" },
		{ u"x*", "", u"xxa", 3, "3-3" },
		// What AS3 takes from PCRE, which Node does not have: from PCRE's and AS3's documentation, not run.
		{ u"(?P<year>\\d{4})-(?P<month>\\d\\d)", "", u"on 2024-05", 0, "3-10 3-7 8-10" },
		{ u"(?P<twice>a)(?P=twice)", "", u"xaa", 0, "1-3 1-2" },
		{ u"a b # a comment\n c", "x", u"abc", 0, "0-3" },
		{ u"[ ]a\\ b", "x", u" a b", 0, "0-4" },
		{ u"(?>a+)b", "", u"aaab", 0, "0-4" },
		{ u"(?>a+)ab", "", u"aaab", 0, "none" },
		{ u"a++b", "", u"aaab", 0, "0-4" },
		{ u"a++ab", "", u"aaab", 0, "none" },
		{ u"a{,2}", "", u"a{,2}", 0, "0-5" }, // A brace that begins no quantifier is itself.
		{ u"x{", "", u"x{", 0, "0-2" },
	};
	for (const Case &one : cases) {
		check::checks()++;
		const std::string found = regex_find(one.pattern, one.flags, one.text, one.from);
		if (found != one.expected) {
			check::failures()++;
			std::printf("FAIL regex case %d: expected %s, found %s\n", static_cast<int>(&one - cases), one.expected, found.c_str());
		}
	}
}

// Patterns the engine does not read, and searches that would not end.
static void test_regex_limits() {
	CHECK_EQ(regex_find(u"(", "", u""), std::string("error: a group is not closed"));
	CHECK_EQ(regex_find(u"a)", "", u""), std::string("error: a ')' closes no group"));
	CHECK_EQ(regex_find(u"[b-a]", "", u""), std::string("error: a range in a class goes backwards"));
	CHECK_EQ(regex_find(u"[a", "", u""), std::string("error: a class is not closed"));
	CHECK_EQ(regex_find(u"*", "", u""), std::string("error: a quantifier with nothing to repeat"));
	CHECK_EQ(regex_find(u"a**", "", u""), std::string("error: a quantifier with nothing to repeat"));
	CHECK_EQ(regex_find(u"a{3,2}", "", u""), std::string("error: a quantifier's counts are in the wrong order"));
	CHECK_EQ(regex_find(u"a{65536}", "", u""), std::string("error: a quantifier's count is too large"));
	CHECK_EQ(regex_find(u"\\", "", u""), std::string("error: the pattern ends with a backslash"));
	CHECK_EQ(regex_find(u"(?<=a+)b", "", u""), std::string("error: a lookbehind must have a fixed length"));
	CHECK_EQ(regex_find(u"(?i)a", "", u""), std::string("error: this kind of group is not supported"));
	CHECK_EQ(regex_find(u"(?P=none)", "", u""), std::string("error: no group is named 'none'"));
	CHECK_EQ(regex_find(u"(?P<a>x)(?P<a>y)", "", u""), std::string("error: two groups are named 'a'"));
	CHECK_EQ(regex_find(u"(?=a)*", "", u""), std::string("error: a quantifier with nothing to repeat"));
	CHECK_EQ(regex_find(std::u16string(300, u'(') + std::u16string(300, u')'), "", u""), std::string("error: groups are nested too deeply"));
	CHECK_EQ(regex_find(u"(?:a{1000}){1000}", "", u""), std::string("error: the pattern is too large"));
	CHECK_EQ(regex_find(u"(?:(?:(?:a))){2}", "", u"aa"), std::string("0-2"));
	// Backtracking that grows exponentially with the text stops at the limit, in well under a second.
	const std::u16string many(30, u'a');
	CHECK_EQ(regex_find(u"(a+)+b", "", many), std::string("limit"));
	CHECK_EQ(regex_find(u"(x+x+)+y", "", std::u16string(40, u'x')), std::string("limit"));
	CHECK_EQ(regex_find(u"(a|aa)+$", "", many + u"!"), std::string("limit"));
	CHECK_EQ(regex_find(u"(?:x*)*b", "", many), std::string("none")); // A turn that takes nothing ends a loop.
	CHECK_EQ(regex_find(u"a+b", "", many, 0, regex::Limits{ 100, regex::MAX_BACKTRACK }), std::string("limit"));
	CHECK_EQ(regex_find(u"(?:a|b)*", "", many, 0, regex::Limits{ regex::STEPS, 10 }), std::string("limit"));
	CHECK_EQ(regex_find(u"(?:a|b)*", "", many, 0, regex::Limits{ regex::STEPS, 100 }), std::string("0-30"));
	// A letter that is no flag is left out (recollection of Flash, where ECMAScript raises a SyntaxError).
	CHECK_EQ(regex::read_flags("gimsxq"), static_cast<uint8_t>(regex::GLOBAL | regex::IGNORE_CASE | regex::MULTILINE | regex::DOTALL | regex::EXTENDED));
	CHECK_EQ(regex::flags_text(regex::EXTENDED | regex::GLOBAL | regex::DOTALL), std::string("gsx"));
	std::string error;
	// `\s` is the white space that turning text into a number skips, unit for unit.
	const std::optional<regex::Program> space = regex::compile(u"\\s", 0, &error);
	std::string disagree;
	for (uint32_t unit = 0; space && unit <= 0xFFFF; unit++) {
		std::vector<int32_t> captures;
		const char16_t one = static_cast<char16_t>(unit);
		const bool found = regex::search(*space, std::u16string_view(&one, 1), 0, &captures, regex::Limits()) == regex::Outcome::Found;
		disagree += found != is_text_space(unit) ? std::to_string(unit) + " " : "";
	}
	CHECK_EQ(disagree, std::string());
	const std::optional<regex::Program> named = regex::compile(u"(a)(?P<b>c)", 0, &error);
	CHECK(named && named->groups == 2 && named->names.size() == 2 && named->names[0].empty() && named->names[1] == u"b");
}

// Checked against Node, except where AS3 differs (said at each).
static void test_number_methods() {
	CHECK_EQ(radix_text(255, 16), std::string("ff"));
	CHECK_EQ(radix_text(-255, 2), std::string("-11111111"));
	CHECK_EQ(radix_text(1e21, 16), std::string("3635c9adc5dea00000"));
	CHECK_EQ(radix_text(255.5, 16), std::string("ff")); // AS3: the whole part only.
	CHECK_EQ(radix_text(-0.5, 2), std::string("0"));
	CHECK_EQ(radix_text(1.5, 10), std::string("1.5"));
	CHECK_EQ(radix_text(18446744073709551616.0, 16), std::string("10000000000000000"));
	CHECK_EQ(radix_text(1.7976931348623157e308, 36).size(), (size_t)199);
	CHECK_EQ(wide_radix_text(INT64_MIN, 16), std::string("-8000000000000000"));
	CHECK_EQ(wide_radix_text(INT64_MAX, 36), std::string("1y2p0ij32e8e7"));
	CHECK_EQ(wide_radix_text(0, 2), std::string("0"));
	CHECK_EQ(fixed_text(2.5, 0), std::string("3"));
	CHECK_EQ(fixed_text(0.125, 2), std::string("0.13"));
	CHECK_EQ(fixed_text(1.005, 2), std::string("1.00"));
	CHECK_EQ(fixed_text(-0.0001, 2), std::string("-0.00"));
	CHECK_EQ(fixed_text(0.006, 2), std::string("0.01"));
	CHECK_EQ(fixed_text(0.004, 2), std::string("0.00"));
	CHECK_EQ(fixed_text(0.0004, 2), std::string("0.00"));
	CHECK_EQ(fixed_text(999.995, 2), std::string("1000.00"));
	CHECK_EQ(fixed_text(1e21, 2), std::string("1e+21"));
	CHECK_EQ(fixed_text(-0.0, 1), std::string("0.0"));
	CHECK_EQ(exponential_text(1.5, 0), std::string("2e+0"));
	CHECK_EQ(exponential_text(-12.5, 1), std::string("-1.3e+1"));
	CHECK_EQ(exponential_text(99.99, 1), std::string("1.0e+2"));
	CHECK_EQ(exponential_text(0, 2), std::string("0.00e+0"));
	CHECK_EQ(exponential_text(5e-324, 3), std::string("4.941e-324"));
	CHECK_EQ(precision_text(25, 1), std::string("3e+1"));
	CHECK_EQ(precision_text(99.99, 3), std::string("100"));
	CHECK_EQ(precision_text(0.000123, 2), std::string("0.00012"));
	CHECK_EQ(precision_text(1e-7, 1), std::string("1e-7"));
	CHECK_EQ(precision_text(123456, 2), std::string("1.2e+5"));
	CHECK_EQ(precision_text(0, 3), std::string("0.00"));
	CHECK_EQ(wide_fixed_text(INT64_MIN, 1), std::string("-9223372036854775808.0"));
	CHECK_EQ(wide_exponential_text(9007199254740993, 15), std::string("9.007199254740993e+15"));
	CHECK_EQ(wide_precision_text(-95, 1), std::string("-1e+2"));
	CHECK_EQ(wide_precision_text(1000, 2), std::string("1.0e+3"));
	CHECK(std::isnan(parse_int("x", 10)));
	CHECK_EQ(parse_int("  -0x1F", 0), -31.0);
	CHECK_EQ(parse_int("9007199254740993", 0), 9007199254740992.0);
	CHECK_EQ(parse_int("9007199254740995", 0), 9007199254740996.0);
	CHECK_EQ(parse_int("1fffffffffffff1", 16), 144115188075855856.0);
	CHECK_EQ(parse_int("08", 0), 8.0);
	CHECK(std::isinf(parse_int(std::string(400, '9'), 10)));
	CHECK(std::isnan(parse_int("12", 37)));
	CHECK(std::signbit(parse_int("-0", 0)));
	CHECK_EQ(parse_float("  -.5e-3x"), -0.0005);
	CHECK_EQ(parse_float("1e+"), 1.0);
	CHECK(std::isnan(parse_float(".e1")));
	CHECK_EQ(parse_float("\xE2\x80\x83 2.5"), 2.5); // An em space first.
	CHECK_EQ(string_to_number(" \xC2\xA0" "12\xE3\x80\x80"), 12.0); // A no-break space, and an ideographic one.
	CHECK_EQ(string_to_number("\xE3\x80\x80"), 0.0);
}

// What the JSON reader hands its builder, as tokens: `n`, `t`, `f`, numbers, "texts", `key:`, brackets.
class JsonTokens : public json::Builder {
	void add(const std::string &p_token) { tokens += (tokens.empty() ? "" : " ") + p_token; }
	bool next() { return calls++ != stop_at; }

public:
	std::string tokens;
	int stop_at = -1; // Stops at this call, counted from 0.
	int calls = 0;
	double last = 0;
	bool null() override {
		add("n");
		return next();
	}
	bool boolean(bool p_value) override {
		add(p_value ? "t" : "f");
		return next();
	}
	bool number(double p_value) override {
		add(number_to_string(p_value));
		last = p_value;
		return next();
	}
	bool string(std::string_view p_text) override {
		add("\"" + std::string(p_text) + "\"");
		return next();
	}
	bool begin_object() override {
		add("{");
		return next();
	}
	bool key(std::string_view p_text) override {
		add(std::string(p_text) + ":");
		return next();
	}
	bool end_object() override {
		add("}");
		return next();
	}
	bool begin_array() override {
		add("[");
		return next();
	}
	bool end_array() override {
		add("]");
		return next();
	}
};

static std::string json_tokens(std::string_view p_text) {
	JsonTokens tokens;
	switch (json::parse(p_text, tokens)) {
		case json::Fault::None:
			return tokens.tokens;
		case json::Fault::Syntax:
			return "syntax";
		case json::Fault::TooDeep:
			return "deep";
		default:
			return "stopped";
	}
}

// ECMA-262 5th edition's JSON grammar (15.12.1); Node's JSON.parse agrees with every case.
static void test_json_text() {
	CHECK_EQ(json_tokens("[1, -0.5e1, \"a\", true, false, null, {\"k\": {}}, []]"), std::string("[ 1 -5 \"a\" t f n { k: { } } [ ] ]"));
	CHECK_EQ(json_tokens("{\"a\":1,\"b\":[2],\"a\":3}"), std::string("{ a: 1 b: [ 2 ] a: 3 }"));
	CHECK_EQ(json_tokens("\t\r\n 1 \n"), std::string("1"));
	CHECK_EQ(json_tokens("\"\\u0041\\ud83d\\ude00\\n\\/\\\\\\\"\\b\\f\\r\\t\""), std::string("\"A\xF0\x9F\x98\x80\n/\\\"\b\f\r\t\""));
	CHECK_EQ(json_tokens("\"\\ud83d\""), std::string("\"\xED\xA0\xBD\"")); // A half alone is held as its three bytes.
	CHECK_EQ(json_tokens("\"\xED\xA0\xBD\\ude00\""), std::string("\"\xF0\x9F\x98\x80\""));
	CHECK_EQ(json_tokens("\"\\uD83D\xED\xB8\x80 \xC3\xA9\""), std::string("\"\xF0\x9F\x98\x80 \xC3\xA9\""));
	CHECK_EQ(json_tokens("[0, 0.1, 5e-324, 1E+2, 1e400, 123456789012345678901234567890]"),
			std::string("[ 0 0.1 5e-324 100 Infinity 1.2345678901234568e+29 ]"));
	JsonTokens zero;
	CHECK(json::parse("-0", zero) == json::Fault::None && std::signbit(zero.last));
	const char *bad[] = { "", " ", "01", "-01", "-", "1.", ".5", "+1", "1e", "1e+", "0x1", "NaN", "Infinity", "'a'", "\"a", "\"\\x\"",
		"\"\\u12\"", "\"\\u12G4\"", "\"a\tb\"", "[1,]", "[,1]", "{\"a\":1,}", "{a:1}", "{\"a\" 1}", "{\"a\":}", "[1 2]", "tru", "nulll", "1 2",
		"[", "]", "{", "}", "[1}", "{\"a\":1]", "\xC2\xA0" "1", "\v1", "\f1", "1\xE2\x80\xA8", "{1:2}", "[\"a\"\"b\"]" };
	for (const char *text : bad) {
		check::checks()++;
		if (json_tokens(text) != "syntax") {
			check::failures()++;
			std::printf("FAIL JSON read: %s\n", text);
		}
	}
	CHECK_EQ(json_tokens(std::string_view("\"\0\"", 3)), std::string("syntax"));
	CHECK_EQ(json_tokens(std::string(json::MAX_DEPTH, '[') + std::string(json::MAX_DEPTH, ']')).size(), json::MAX_DEPTH * 4 - 1);
	CHECK_EQ(json_tokens(std::string(json::MAX_DEPTH + 1, '[') + std::string(json::MAX_DEPTH + 1, ']')), std::string("deep"));
	CHECK_EQ(json_tokens(std::string(json::MAX_DEPTH, '{')), std::string("syntax"));
	JsonTokens stopping;
	stopping.stop_at = 2;
	CHECK(json::parse("[1, 2, 3]", stopping) == json::Fault::Stopped);
	CHECK_EQ(stopping.tokens, std::string("[ 1 2"));
	std::string quoted;
	json::quote("a\"\\/\b\f\n\r\t\x01\x1f\x7f \xC3\xA9", &quoted);
	CHECK_EQ(quoted, std::string("\"a\\\"\\\\/\\b\\f\\n\\r\\t\\u0001\\u001f\x7f \xC3\xA9\""));
	quoted = "x";
	json::quote("", &quoted);
	CHECK_EQ(quoted, std::string("x\"\""));
}

// A String's methods as functions of plain values, with no interpreter: slots in, a slot out.
static void test_text_methods() {
	using kas3::ir::StringFunction;
	Str *hello = new_string("Hello, World");
	Str *o = new_string("o");
	const auto text_of = [](Slot p_slot) {
		Str *made = static_cast<Str *>(p_slot.p);
		const std::string words(made->view());
		if (release(made)) {
			free_string(made);
		}
		return words;
	};
	const auto number = [](double p_value) {
		Slot slot;
		slot.bits = 0;
		slot.d = p_value;
		return slot;
	};
	Slot self, other;
	self.bits = other.bits = 0;
	self.p = hello;
	other.p = o;
	Slot out;
	out.bits = 0;
	const Slot at[] = { self, number(1) };
	CHECK(text_method(StringFunction::CharAt, Values{ at, nullptr, 2 }, nullptr, &out));
	CHECK_EQ(text_of(out), std::string("e"));
	CHECK(text_method(StringFunction::CharCodeAt, Values{ at, nullptr, 2 }, nullptr, &out));
	CHECK_EQ(out.d, 101.0);
	const Slot find[] = { self, other, number(5) };
	CHECK(text_method(StringFunction::IndexOf, Values{ find, nullptr, 3 }, nullptr, &out));
	CHECK_EQ(out.i, 8);
	const Slot part[] = { self, number(-5), number(3) };
	CHECK(text_method(StringFunction::Substr, Values{ part, nullptr, 3 }, nullptr, &out));
	CHECK_EQ(text_of(out), std::string("Wor"));
	// By a frame's registers and an instruction's list of them, as the interpreter hands them.
	const Slot registers[] = { number(0), other, self };
	const uint32_t order[] = { 2, 1 };
	CHECK(text_method(StringFunction::Concat, Values{ registers, order, 2 }, nullptr, &out));
	CHECK_EQ(text_of(out), std::string("Hello, Worldo"));
	// One character is made once when the caller keeps them.
	Characters kept{};
	CHECK(text_method(StringFunction::CharAt, Values{ at, nullptr, 2 }, &kept, &out));
	CHECK(kept['e'] == out.p);
	CHECK_EQ(text_of(out), std::string("e"));
	CHECK(kas3::ir::reads_text_only(StringFunction::Upper) && !kas3::ir::reads_text_only(StringFunction::Split) &&
			!kas3::ir::reads_text_only(StringFunction::EncodeURI));
	for (Str *each : kept) {
		if (each && release(each)) {
			free_string(each);
		}
	}
	for (Str *each : { hello, o }) {
		if (release(each)) {
			free_string(each);
		}
	}
}

int main() {
	test_text_methods();
	test_json_text();
	test_regex_matches();
	test_regex_limits();
	test_text_units();
	test_number_methods();
	test_tagged_addresses();
	test_long_text();
	test_any();
	test_vectors();
	test_fixed_array_allocation();
	test_math();
	test_integers();
	test_number_text();
	test_text_number();
	test_counts();
	return check::finish();
}
