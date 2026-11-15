// Number conversions and formatting. Every expected value was checked against Node, which follows the
// same ECMAScript rules:  node -e 'console.log(String(x), x | 0, x >>> 0, Number(s))'
#include "check.h"

#include "any.h"
#include "number_text.h"
#include "numeric.h"
#include "runtime.h"
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
	CHECK_EQ(object->fields()[1].bits, (uint64_t)0);
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
	Vector *vector = new_vector(7, 3, false, nullptr);
	CHECK(vector != nullptr);
	CHECK_EQ(vector->klass, 7u);
	CHECK_EQ(vector->references, 1u);
	CHECK_EQ(vector->length, 3u);
	CHECK(!vector->fixed);
	CHECK(vector->capacity >= 3);
	for (uint32_t i = 0; i < 3; i++) {
		CHECK_EQ(vector->data[i].bits, (uint64_t)0);
		vector->data[i].i = (int32_t)i + 1;
	}
	CHECK(vector_reserve(vector, 1000));
	CHECK(vector->capacity >= 1000);
	CHECK_EQ(vector->length, 3u);
	CHECK_EQ(vector->data[2].i, 3);
	CHECK_EQ(vector->data[999].bits, (uint64_t)0); // Room that was added is empty.
	CHECK(vector_reserve(vector, 2)); // Never shrinks.
	CHECK(vector->capacity >= 1000);
	CHECK_EQ(live_objects(), objects + 1);
	free_vector(vector);
	CHECK_EQ(live_objects(), objects);
	Vector *empty = new_vector(7, 0, true, nullptr);
	CHECK(empty && empty->length == 0 && empty->fixed);
	free_vector(empty);
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
	Str *whole = join_strings("a" + high, low + "b");
	CHECK(whole && whole->view() == face);
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

int main() {
	test_text_units();
	test_number_methods();
	test_tagged_addresses();
	test_long_text();
	test_any();
	test_vectors();
	test_math();
	test_integers();
	test_number_text();
	test_text_number();
	test_counts();
	return check::finish();
}
