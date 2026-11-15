// Number conversions and formatting. Every expected value was checked against Node, which follows the
// same ECMAScript rules:  node -e 'console.log(String(x), x | 0, x >>> 0, Number(s))'
#include "check.h"

#include "any.h"
#include "numeric.h"
#include "runtime.h"

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
	CHECK_EQ(character_count("abc"), 3);
	CHECK_EQ(character_count("\xC3\xA9t\xC3\xA9"), 3);
	CHECK_EQ(character_count("\xF0\x9F\x98\x80"), 1);
	CHECK_EQ(character_count(""), 0);
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
	Object *object = new_object(3, 2);
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
	Vector *vector = new_vector(7, 3, false);
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
	Vector *empty = new_vector(7, 0, true);
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
	for (int32_t value : { 0, -1, 7, INT32_MIN, INT32_MAX }) {
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

int main() {
	test_any();
	test_vectors();
	test_math();
	test_integers();
	test_number_text();
	test_text_number();
	test_counts();
	return check::finish();
}
