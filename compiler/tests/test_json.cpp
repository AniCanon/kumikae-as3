#include "check.h"

#include "json.h"

#include <string>

using namespace kas3;

static std::string round_trip(const char *p_text) {
	Json value;
	if (!parse_json(p_text, &value)) {
		return "INVALID";
	}
	return write_json(value);
}

static void test_values() {
	CHECK_EQ(round_trip("null"), std::string("null"));
	CHECK_EQ(round_trip(" true "), std::string("true"));
	CHECK_EQ(round_trip("false"), std::string("false"));
	CHECK_EQ(round_trip("0"), std::string("0"));
	CHECK_EQ(round_trip("-12"), std::string("-12"));
	CHECK_EQ(round_trip("1.5"), std::string("1.5"));
	CHECK_EQ(round_trip("1e3"), std::string("1000"));
	CHECK_EQ(round_trip("2147483648"), std::string("2147483648"));
	CHECK_EQ(round_trip("\"\""), std::string("\"\""));
	CHECK_EQ(round_trip("[]"), std::string("[]"));
	CHECK_EQ(round_trip("{}"), std::string("{}"));
	CHECK_EQ(round_trip("[1, [2, {\"a\": null}], \"x\"]"), std::string("[1,[2,{\"a\":null}],\"x\"]"));
	CHECK_EQ(round_trip("{ \"jsonrpc\" : \"2.0\", \"id\" : 1, \"params\" : { \"x\" : [ true ] } }"),
			std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"params\":{\"x\":[true]}}"));
}

static void test_strings() {
	CHECK_EQ(round_trip("\"a\\n\\t\\\"\\\\\\/b\""), std::string("\"a\\n\\t\\\"\\\\/b\""));
	CHECK_EQ(round_trip("\"\\u00e9\""), std::string("\"\xC3\xA9\""));
	CHECK_EQ(round_trip("\"\\ud83d\\ude00\""), std::string("\"\xF0\x9F\x98\x80\"")); // A surrogate pair.
	CHECK_EQ(round_trip("\"\\ud83d\""), std::string("\"\xEF\xBF\xBD\"")); // Half of one.
	CHECK_EQ(round_trip("\"\\ude00x\""), std::string("\"\xEF\xBF\xBDx\""));
	CHECK_EQ(round_trip("\"\\u0001\""), std::string("\"\\u0001\""));
	CHECK_EQ(round_trip("\"\xC3\xA9\""), std::string("\"\xC3\xA9\""));
	// Bytes that are not UTF-8 never reach the output.
	Json bad(std::string("a\xFF\xC3"));
	CHECK_EQ(write_json(bad), std::string("\"a\xEF\xBF\xBD\xEF\xBF\xBD\""));
	Json value;
	CHECK(parse_json("\"\\u0041\"", &value));
	CHECK_EQ(value.string(), std::string("A"));
}

static void test_malformed() {
	Json value;
	const char *cases[] = { "", " ", "{", "[1,", "[1,]", "{\"a\":1,}", "{\"a\"}", "{a:1}", "nul", "tru", "01", "1.", "-", "\"abc",
		"\"\\x\"", "\"\\u12\"", "[1] 2", "{\"a\":1}}", "'a'", "+1", "1e", "[1 2]", "\"a\nb\"" };
	for (const char *text : cases) {
		check::checks()++;
		if (parse_json(text, &value)) {
			check::failures()++;
			std::printf("FAIL accepted: %s\n", text);
		}
	}
	// Deep nesting is refused, not followed.
	std::string deep(10000, '[');
	CHECK(!parse_json(deep, &value));
	std::string closed = std::string(10000, '[') + std::string(10000, ']');
	CHECK(!parse_json(closed, &value));
	std::string fine = std::string(150, '[') + std::string(150, ']');
	CHECK(parse_json(fine, &value));
}

static void test_access() {
	Json value;
	CHECK(parse_json("{\"a\":{\"b\":[10,\"x\",2.5,true]},\"n\":-3}", &value));
	CHECK_EQ(value.get("a").get("b").at(0).integer(), (int64_t)10);
	CHECK_EQ(value.get("a").get("b").at(1).string(), std::string("x"));
	CHECK_EQ(value.get("a").get("b").at(2).number(), 2.5);
	CHECK_EQ(value.get("a").get("b").at(2).integer(7), (int64_t)7); // Not whole.
	CHECK(value.get("a").get("b").at(3).boolean());
	CHECK_EQ(value.get("a").get("b").size(), (size_t)4);
	CHECK_EQ(value.get("n").integer(), (int64_t)-3);
	// Missing keys and wrong kinds give defaults, however deep.
	CHECK(value.get("zzz").get("y").at(5).get("q").is_null());
	CHECK_EQ(value.get("a").string(), std::string());
	CHECK_EQ(value.get("a").integer(42), (int64_t)42);
	CHECK(!value.get("n").boolean());
	CHECK(value.has("n") && !value.has("m"));
	CHECK(value.at(0).is_null());

	Json built = Json::object();
	built.set("id", 7).set("text", "hi").set("list", Json::array().push(1).push("two").push(Json()));
	built.set("id", 8);
	CHECK_EQ(write_json(built), std::string("{\"id\":8,\"text\":\"hi\",\"list\":[1,\"two\",null]}"));
	CHECK_EQ(write_json(Json(0.1)), std::string("0.1"));
	CHECK_EQ(write_json(Json((uint32_t)4000000000u)), std::string("4000000000"));
}

int main() {
	test_values();
	test_strings();
	test_malformed();
	test_access();
	return check::finish();
}
