// Operators and indexing on Godot's value types, against Godot's real declarations.
#include "builtins.h"
#include "check.h"
#include "marked.h"
#include "names.h"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

using namespace kas3;

// Main.as, lines 1 to 8, then p_body starting on line 9.
static std::string program(const std::string &p_body) {
	return "package { import godot.*; public class Main extends Node2D {\n"
		   "public var a:Vector2; public var b:Vector2; public var vi:Vector2i; public var wi:Vector2i;\n"
		   "public var v3:Vector3; public var c:Color; public var d:Color; public var t:Transform2D;\n"
		   "public var rect:Rect2; public var rid:RID; public var q:Quaternion; public var basis:Basis;\n"
		   "public var i:int; public var u:uint; public var n:Number; public var s:String; public var any:*; public var w:Int64;\n"
		   "public var obj:Object; public var node:Node; public var list:Array; public var mine:Mine;\n"
		   "public var points:PackedVector2Array; public var wide:PackedInt64Array; public var bytes:PackedByteArray; public var names:PackedStringArray;\n"
		   "public var ga:GodotArray; public var gd:GodotDictionary; public function run(delta:Number):void {\n" +
			p_body + "\n} } }\nclass Mine {}\n";
}

struct Case {
	const char *expression;
	const char *type;
};

// Each expression is one statement; its type must be the one given, and the program must be clean.
static void types(std::initializer_list<Case> p_cases) {
	std::string body;
	for (const Case &item : p_cases) {
		body += item.expression;
		body += ";\n";
	}
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	Unit *unit = compilation.add("Main.as", program(body));
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	std::vector<std::string> found(p_cases.size(), "(none)");
	for (const TypedExpression &typed : unit->typed) {
		int line = unit->source.position(typed.offset).line - 9;
		if (line >= 0 && line < (int)found.size() && found[line] == "(none)") {
			found[line] = compilation.type_name(typed.type);
		}
	}
	size_t index = 0;
	for (const Case &item : p_cases) {
		check::checks()++;
		if (found[index] != item.type) {
			check::failures()++;
			std::printf("FAIL type of `%s`\n  actual:   %s\n  expected: %s\n", item.expression, found[index].c_str(),
					item.type);
		}
		index++;
	}
}

static void expect(const std::string &p_body, std::initializer_list<const char *> p_messages) {
	MarkedSource source = mark("Main.as", program(p_body), p_messages);
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Main.as", source.text);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), source.expected);
}

#define NO_OPERATOR(op, left, right) "can't use '" op "' on '" left "' and '" right "'"
#define NO_UNARY(op, type) "can't use '" op "' on '" type "'"
#define UNRELATED(from, to) "can't use a value of type '" from "' where '" to "' is expected"

static void test_arithmetic() {
	types({
			{ "a + b", "Vector2" },
			{ "a - b", "Vector2" },
			{ "a * b", "Vector2" },
			{ "a / b", "Vector2" },
			{ "a * 2", "Vector2" },
			{ "a * 0.5", "Vector2" },
			{ "a * n", "Vector2" },
			{ "a / i", "Vector2" },
			{ "2 * a", "Vector2" },
			{ "n * a", "Vector2" },
			{ "position + a * delta", "Vector2" },
			{ "(a + b) * 0.5 - position", "Vector2" },
			{ "c * 2", "Color" },
			{ "c + d", "Color" },
			{ "q * v3", "Vector3" },
			{ "basis * v3", "Vector3" },
			{ "-a", "Vector2" },
			{ "+a", "Vector2" },
			{ "-c", "Color" },
			{ "ga + list", "GodotArray" },
			{ "a + s", "String" },
			{ "s + a", "String" },
	});
}

// Transform2D * Vector2 is a Vector2, and so is Vector2 * Transform2D; a Transform2D times itself is one.
static void test_operators_that_are_not_symmetric() {
	types({
			{ "t * a", "Vector2" },
			{ "a * t", "Vector2" },
			{ "t * t", "Transform2D" },
			{ "t * points", "PackedVector2Array" },
			{ "t * rect", "Rect2" },
			{ "t * 2", "Transform2D" },
	});
	expect("/*!*/2 * t; /*!*/rect * rect; /*!*/t + t;",
			{ NO_OPERATOR("*", "int", "Transform2D"), NO_OPERATOR("*", "Rect2", "Rect2"),
					NO_OPERATOR("+", "Transform2D", "Transform2D") });
}

// Godot gives Vector2i * int a Vector2i and Vector2i * float a Vector2. An AS3 expression made of whole
// numbers is a whole number here, although AS3 itself computes `i + 1` as a Number.
static void test_whole_and_fractional_numbers() {
	types({
			{ "vi * 2", "Vector2i" },
			{ "vi * i", "Vector2i" },
			{ "vi * u", "Vector2i" },
			{ "vi * (i + 1)", "Vector2i" },
			{ "vi * (i * 2 - u % 3)", "Vector2i" },
			{ "vi * -i", "Vector2i" },
			{ "vi * list.length", "Vector2i" },
			{ "vi * int(n)", "Vector2i" },
			{ "vi / 2", "Vector2i" },
			{ "vi % 2", "Vector2i" },
			{ "2 * vi", "Vector2i" },
			{ "vi * 0.5", "Vector2" },
			{ "vi * 2.0", "Vector2" },
			{ "vi * 1e2", "Vector2" },
			{ "vi * n", "Vector2" },
			{ "vi * (i / 2)", "Vector2" },
			{ "vi * (i + n)", "Vector2" },
			{ "n * vi", "Vector2" },
			{ "vi + wi", "Vector2i" },
			// A whole number is also a Number, so it may use a form that Godot has only for float.
			{ "t * i", "Transform2D" },
	});
	// The reverse does not hold: Godot has Vector2i % int and no Vector2i % float.
	expect("/*!*/vi % n; /*!*/vi % 0.5;",
			{ NO_OPERATOR("%", "Vector2i", "Number"), NO_OPERATOR("%", "Vector2i", "Number") });
}

static void test_comparisons() {
	types({
			{ "a < b", "Boolean" },
			{ "vi >= wi", "Boolean" },
			{ "a == b", "Boolean" },
			{ "c != d", "Boolean" },
			{ "c == null", "Boolean" },
	});
	expect("/*!*/c < d; /*!*/a > 1; /*!*/rect <= rect;",
			{ NO_OPERATOR("<", "Color", "Color"), NO_OPERATOR(">", "Vector2", "int"),
					NO_OPERATOR("<=", "Rect2", "Rect2") });
}

static void test_results_are_checked_where_stored() {
	expect("var p:Vector2 = a + b; var m:Vector2i = vi * 2; position = position + a * delta;\n"
		   "var k:Number = /*!*/a + b; var w:Vector2i = /*!*/vi * 0.5; rotation = /*!*/a * 2; addChild(/*!*/a - b);",
			{ UNRELATED("Vector2", "Number"), UNRELATED("Vector2", "Vector2i"), UNRELATED("Vector2", "Number"),
					UNRELATED("Vector2", "Node") });
}

static void test_no_operator() {
	expect("/*!*/gd + 1; /*!*/-gd; /*!*/gd - gd; /*!*/names * 2;", // Types with no operators at all.
			{ NO_OPERATOR("+", "GodotDictionary", "int"), NO_UNARY("-", "GodotDictionary"),
					NO_OPERATOR("-", "GodotDictionary", "GodotDictionary"), NO_OPERATOR("*", "PackedStringArray", "int") });
	expect("/*!*/rid + 1; /*!*/a + node; /*!*/a % b; /*!*/a + 1; /*!*/a - c; /*!*/a * true; /*!*/node * a; /*!*/a + mine;\n"
		   "/*!*/-rect; /*!*/a + list; /*!*/a / s;",
			{ NO_OPERATOR("+", "RID", "int"), NO_OPERATOR("+", "Vector2", "Node"), NO_OPERATOR("%", "Vector2", "Vector2"),
					NO_OPERATOR("+", "Vector2", "int"), NO_OPERATOR("-", "Vector2", "Color"),
					NO_OPERATOR("*", "Vector2", "Boolean"), NO_OPERATOR("*", "Node", "Vector2"),
					NO_OPERATOR("+", "Vector2", "Mine"), NO_UNARY("-", "Rect2"), NO_OPERATOR("+", "Vector2", "Array"),
					NO_OPERATOR("/", "Vector2", "String") });
}

// Operators that no Godot type has: counting up and down, bit operations.
static void test_never_operators() {
	expect("/*!*/a++; /*!*/++a; /*!*/a--; /*!*/~a; /*!*/a <<= 1; /*!*/a &= 3; /*!*/gd++;",
			{ NO_UNARY("++", "Vector2"), NO_UNARY("++", "Vector2"), NO_UNARY("--", "Vector2"), NO_UNARY("~", "Vector2"),
					NO_UNARY("<<=", "Vector2"), NO_UNARY("&=", "Vector2"), NO_UNARY("++", "GodotDictionary") });
	// On the right too: a number shifted by a vector.
	expect("var n:int = 1; /*!*/n >>>= a; /*!*/n |= a; /*!*/n <<= gd;",
			{ NO_UNARY(">>>=", "Vector2"), NO_UNARY("|=", "Vector2"), NO_UNARY("<<=", "GodotDictionary") });
	// Numbers, and values that could be anything, are untouched.
	expect("var n:int = 1; n++; ~n; n <<= 2; any++; ~any; mine++; node++;", {});
}

// Nothing is said where the operand could be anything (the result is then untyped), or where the class says nothing about operators.
static void test_silence() {
	types({
			{ "a + any", "*" },
			{ "any * a", "*" },
			{ "a + obj", "*" },
			{ "a + null", "*" },
			{ "mine + mine", "?" },
			{ "node * 2", "?" },
			{ "-node", "?" },
			{ "rid < rid", "Boolean" },
			{ "node < node", "Boolean" },
			{ "i * n", "Number" },
	});
}

static void test_compound_assignment() {
	types({
			{ "a += b", "Vector2" },
			{ "position += a * delta", "Vector2" },
			{ "a *= 2", "Vector2" },
			{ "vi *= 2", "Vector2i" },
			{ "c *= d", "Color" },
			{ "a /= 2", "Vector2" },
			{ "vi %= 2", "Vector2i" },
			{ "s += a", "String" },
			{ "a += any", "Vector2" },
			{ "any += a", "*" },
	});
	// A result that is not the target's type can't be stored back in it.
	expect("/*!*/vi *= 0.5; /*!*/t *= a; /*!*/a += node; /*!*/rect *= rect; /*!*/n *= a; /*!*/i += a;",
			{ UNRELATED("Vector2", "Vector2i"), UNRELATED("Vector2", "Transform2D"),
					NO_OPERATOR("+", "Vector2", "Node"), NO_OPERATOR("*", "Rect2", "Rect2"),
					UNRELATED("Vector2", "Number"), NO_OPERATOR("+", "int", "Vector2") });
}

// Tags in a program's own files mean nothing: only a declaration file can describe what the engine does.
static void test_tags_outside_declarations() {
	MarkedSource source = mark("Main.as",
			"package { public class Main { public function run(a:Pair, b:Pair):void { a + b; a[0]; } } }\n"
			"[Operator(op=\"+\", right=\"Pair\", returns=\"int\")] [Indexed(element=\"int\")] class Pair {}\n",
			{});
	Compilation compilation;
	Unit *unit = compilation.add("Main.as", source.text);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	CHECK_EQ(compilation.type_name(unit->typed[0].type), std::string("?"));
	CHECK_EQ(compilation.type_name(unit->typed[1].type), std::string("*"));
}

// A declaration file of the program's own may describe operators too. A tag may name any type the file
// can see; one that names nothing is reported where it is written. A subclass has its base's operators.
static void test_other_declarations() {
	Compilation compilation;
	compilation.add("other.as", "package other { public native class Rate {} }\n", true);
	compilation.add("lib.as",
			"package lib {\n"
			"[Operator(op=\"+\", right=\"Money\", returns=\"Money\")] [Operator(op=\"*\", right=\"Ghost\", returns=\"Money\")]\n"
			"[Operator(op=\"*\", right=\"Number\", returns=\"Money\")] [Operator(op=\">\", right=\"uint\", returns=\"Boolean\")]\n"
			"[Operator(op=\"/\", right=\"other.Rate\", returns=\"Money\")] [Operator(op=\"%\", right=\"String\", returns=\"String\")]\n"
			"[Operator(op=\"-\", returns=\"Money\")] [Indexed(element=\"String\")]\n"
			"public native class Money {}\n"
			"public native class Bonus extends Money {} }\n",
			true);
	Unit *unit = compilation.add("Main.as",
			"package { import lib.*; import other.Rate; public class Main {\n"
			"public function run(a:Money, b:Money, r:Rate, u:uint, s:String, x:Bonus):void {\n"
			"a + b;\na * 2;\na / r;\na % s;\na[0];\nx + a;\na + x;\n-x;\nx[1];\na > u;\na - b;\na * r;\n} } }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false),
			std::string("lib.as:2:52: error: unknown type 'Ghost' in an [Operator] tag\n"
						"Main.as:13:1: error: can't use '-' on 'Money' and 'Money'\n"
						"Main.as:14:1: error: can't use '*' on 'Money' and 'Rate'\n"));
	const char *expected[] = { "Money", "Money", "Money", "String", "String", "Money", "Money", "Money", "String", "Boolean" };
	for (int i = 0; i < 10; i++) {
		CHECK_EQ(compilation.type_name(unit->typed[i].type), std::string(expected[i]));
	}
}

// A tag that says two things, or a second rule for the same operator and operand, is an error in the
// declaration file, not a choice made silently.
static void test_malformed_tags() {
	Compilation compilation;
	compilation.add("lib.as",
			"package lib {\n"
			"[Operator(op=\"+\", left=\"int\", right=\"int\", returns=\"Money\")]\n"
			"[Operator(op=\"*\", right=\"int\", returns=\"Money\")] [Operator(op=\"*\", right=\"int\", returns=\"int\")]\n"
			"public native class Money {} }\n",
			true);
	compilation.add("Main.as", "package { import lib.*; public class Main { public function run(a:Money):void { a * 2; } } }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false),
			std::string("lib.as:2:1: error: an [Operator] tag names both a left and a right operand\n"
						"lib.as:3:50: error: a second [Operator] tag for '*' with 'int' on the right\n"));
}

// Godot's value types take only the forms of constructor Godot has, though AS3 sees one that merges them.
static void test_constructor_forms() {
	expect("new Vector3(); new Vector3(1, 2, 3); new Vector3(1.5, 2, 3); new Vector3(new Vector3()); new Vector3(any);"
		   "new Vector3(new Vector3i()); new Vector2(1, 2); new Color(1, 0, 0, 0.5); new Color('red');",
			{});
	expect("new /*!*/Vector3(1, 2); new /*!*/Vector3('a', 1, 2); new /*!*/Vector2(new Vector3());",
			{ "no constructor of 'Vector3' takes 'int', 'int'", "no constructor of 'Vector3' takes 'String', 'int', 'int'",
					"no constructor of 'Vector2' takes 'Vector3'" });
}

static void test_indexing() {
	types({
			{ "points[0]", "Vector2" },
			{ "points[i + 1]", "Vector2" },
			{ "a[0]", "Number" },
			{ "vi[1]", "int" },
			{ "t[0]", "Vector2" },
			{ "basis[0]", "Vector3" },
			{ "c[0]", "Number" },
			{ "bytes[0]", "Int64" },
			{ "wide[0]", "Int64" },
			{ "names[0]", "String" },
			{ "names[0].length", "int" },
			{ "ga[0]", "*" },
			{ "gd[\"k\"]", "*" },
			{ "gd[node]", "*" },
			{ "gd[0]", "*" },
			{ "a[\"x\"]", "*" },
			{ "points[any]", "*" },
			{ "rect[0]", "*" },
			{ "points[0].x", "Number" },
			{ "points[0] + a", "Vector2" },
	});
	expect("points[0] = a; points[i] = new Vector2(1, 2); ga[0] = node; gd[\"k\"] = 1; bytes[0] = 255; a[0] = 1.5;\n"
		   "points[0] = /*!*/\"s\"; points[1] = /*!*/v3; a[0] = /*!*/node; var z:Vector3 = /*!*/points[0];\n"
		   "names[0] = \"x\"; names[1] = /*!*/a; var k:Vector2 = /*!*/names[0];",
			{ UNRELATED("String", "Vector2"), UNRELATED("Vector3", "Vector2"), UNRELATED("Node", "Number"),
					UNRELATED("Vector2", "Vector3"), UNRELATED("Vector2", "String"), UNRELATED("String", "Vector2") });
}

// Int64: Godot's 64-bit integer as an AS3 type.
static void test_int64() {
	types({
			{ "w + 1", "Int64" },
			{ "1 + w", "Int64" },
			{ "w - u", "Int64" },
			{ "w * w", "Int64" },
			{ "w % 2", "Int64" },
			{ "w & 1", "Int64" },
			{ "i | w", "Int64" },
			{ "w ^ w", "Int64" },
			{ "w << 2", "Int64" },
			{ "w >> 2", "Int64" },
			{ "w >>> 1", "Int64" },
			{ "~w", "Int64" },
			{ "-w", "Int64" },
			{ "w++", "Int64" },
			{ "--w", "Int64" },
			{ "w / 2", "Number" },
			{ "w + 1.5", "Number" },
			{ "w * n", "Number" },
			{ "w < 1.5", "Boolean" },
			{ "w == i", "Boolean" },
			{ "w += 1", "Int64" },
			{ "w &= 3", "Int64" },
			{ "w = i", "Int64" },
			{ "i = w", "int" },
			{ "n = w", "Number" },
			{ "u = w", "uint" },
			{ "Int64.MAX_VALUE", "Int64" },
			{ "Int64(\"12\")", "Int64" },
			{ "Int64(n)", "Int64" },
			{ "int(w)", "int" },
			{ "Number(w)", "Number" },
			{ "String(w)", "String" },
			{ "\"\" + w", "String" },
			{ "w = 9223372036854775807", "Int64" },
			{ "w = -9223372036854775808", "Int64" },
			{ "w = 0x7FFFFFFFFFFFFFFF", "Int64" },
			{ "i > 0 ? w : i", "Int64" },
			{ "i > 0 ? w : n", "Number" },
			// A whole number keeps an integer vector on its grid.
			{ "vi * w", "Vector2i" },
			{ "vi * (w + 1)", "Vector2i" },
			{ "vi * (w / 2)", "Vector2" },
			// Whole numbers that AS3 holds in a Number.
			{ "vi * Math.floor(n)", "Vector2i" },
			{ "vi * Math.round(n)", "Vector2i" },
			{ "vi * Math.ceil(n)", "Vector2i" },
			{ "vi * Math.abs(i)", "Vector2i" },
			{ "vi * Math.max(i, u)", "Vector2i" },
			{ "vi * Math.min(i, 2)", "Vector2i" },
			{ "vi * parseInt(s)", "Vector2i" },
			{ "vi * (i > 0 ? i : u)", "Vector2i" },
			{ "vi * Math.abs(n)", "Vector2" },
			{ "vi * Math.max(i, n)", "Vector2" },
	});
	expect("s = /*!*/w; w = /*!*/s; w = /*!*/true; w = /*!*/9223372036854775808; w = /*!*/-9223372036854775809;\n"
		   "w = /*!*/0x10000000000000000; var x:Int64 = /*!*/18446744073709551616; w = 1.5; w = n; w = null;",
			{ UNRELATED("Int64", "String"), UNRELATED("String", "Int64"), UNRELATED("Boolean", "Int64"),
					"9223372036854775808 does not fit an Int64", "-9223372036854775809 does not fit an Int64",
					"0x10000000000000000 does not fit an Int64", "18446744073709551616 does not fit an Int64" });
}

int main() {
	test_int64();
	test_arithmetic();
	test_operators_that_are_not_symmetric();
	test_whole_and_fractional_numbers();
	test_comparisons();
	test_results_are_checked_where_stored();
	test_no_operator();
	test_never_operators();
	test_silence();
	test_compound_assignment();
	test_tags_outside_declarations();
	test_other_declarations();
	test_malformed_tags();
	test_constructor_forms();
	test_indexing();
	return check::finish();
}
