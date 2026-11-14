// Number conversions and formatting. Every expected value was checked against Node, which follows the
// same ECMAScript rules:  node -e 'console.log(String(x), x | 0, x >>> 0, Number(s))'
#include "check.h"

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

int main() {
	test_math();
	test_integers();
	test_number_text();
	test_text_number();
	test_counts();
	return check::finish();
}
