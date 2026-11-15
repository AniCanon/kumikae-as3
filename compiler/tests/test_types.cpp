// The types pass: the static type of every expression, and what `a.b` refers to.
#include "check.h"
#include "names.h"

#include <initializer_list>
#include <string>
#include <vector>

using namespace kas3;

static const char *const GODOT_STUB =
		"package godot {\n"
		"	public native class Node {\n"
		"		public var name:String;\n"
		"		private var secret:int;\n"
		"		public native function add_child(node:Node):void;\n"
		"		public native function get position():Number;\n"
		"		public native function get_parent():Node;\n"
		"		public static native function make():Node;\n"
		"	}\n"
		"}\n";

// Main.as: a class with a member of every kind, and a method whose body is p_body.
static std::string fixture(const std::string &p_body) {
	return "package { import godot.*; public class Main extends Base implements Shape {\n"
		   "public var count:int; public var ratio:Number; public var label:String; public var flag:Boolean;\n"
		   "public var big:uint; public var list:Array; public var ints:Vector.<int>;\n"
		   "public var grid:Vector.<Vector.<String>>; public var any:*; public var obj:Object; public var other:Main;\n"
		   "public var shape:Shape; public var fn:Function; public var node:Node; private var mine:int;\n"
		   "public static var total:int; public static function make():Main { return null; }"
		   " public static var twin:int; public var twin:String;\n"
		   "public function get size():Number { return 0; } public function set size(v:Number):void {}\n"
		   "public function get width():Number { return 0; }\n"
		   "public function area():Number { return 0; }\n"
		   "public function untyped() {} public function set only(v:String):void {} public var cls:Class;\n"
		   "public function run(param:int, ...rest):void {\n" +
			p_body +
			"\n}\n} }\n"
			"class Base { public var inherited:String; protected var guarded:int; private var secret:int;\n"
			"internal var near:int; public static var base_total:int;\n"
			"public function base_method():Base { return this; } }\n"
			"interface Shape extends Sized { function area():Number; function get size():Number; }\n"
			"interface Sized { function get width():Number; }\n";
}

struct Case {
	const char *expression;
	const char *type;
};

// Each expression becomes one statement of the method; its type must be the one given.
static void check_types(std::initializer_list<Case> p_cases, bool p_clean = true) {
	std::string body;
	for (const Case &item : p_cases) {
		body += item.expression;
		body += ";\n";
	}
	Compilation compilation;
	compilation.add("godot.as", GODOT_STUB, true);
	Unit *unit = compilation.add("Main.as", fixture(body));
	compilation.check();
	if (p_clean) {
		CHECK_EQ(compilation.format_diagnostics(false), std::string());
	}
	// Lines 1 to 11 are the fixture's head; statement i is on line 12 + i.
	std::vector<std::string> types(p_cases.size(), "(none)");
	for (const TypedExpression &typed : unit->typed) {
		int line = unit->source.position(typed.offset).line - 12;
		if (line >= 0 && line < (int)types.size() && types[line] == "(none)") {
			types[line] = compilation.type_name(typed.type);
		}
	}
	size_t index = 0;
	for (const Case &item : p_cases) {
		check::checks()++;
		if (types[index] != item.type) {
			check::failures()++;
			std::printf("FAIL type of `%s`\n  actual:   %s\n  expected: %s\n", item.expression, types[index].c_str(),
					item.type);
		}
		index++;
	}
}

// The source may mark where errors are expected with `/*!*/`, as in test_names.cpp.
static void check_errors(const std::string &p_body, std::initializer_list<const char *> p_messages,
		const std::string &p_extra = std::string()) {
	std::string source = fixture(p_body) + p_extra;
	std::string text;
	std::string expected;
	const std::string marker = "/*!*/";
	int line = 1;
	int column = 1;
	auto message = p_messages.begin();
	for (size_t i = 0; i < source.size();) {
		if (source.compare(i, marker.size(), marker) == 0) {
			expected += "Main.as:" + std::to_string(line) + ":" + std::to_string(column) + ": error: " +
					(message != p_messages.end() ? *message++ : "MISSING MESSAGE") + "\n";
			i += marker.size();
			continue;
		}
		if (source[i] == '\n') {
			line++;
			column = 1;
		} else {
			column++;
		}
		text += source[i++];
	}
	Compilation compilation;
	compilation.add("godot.as", GODOT_STUB, true);
	compilation.add("Main.as", text);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), expected);
}

static void test_literals_and_names() {
	check_types({
			{ "1", "int" },
			{ "2147483647", "int" },
			{ "2147483648", "uint" },
			{ "4294967295", "uint" },
			{ "4294967296", "Number" },
			{ "1.5", "Number" },
			{ "0xFF", "int" },
			{ "0xFFFFFFFF", "uint" },
			{ "1e3", "Number" },
			{ "\"s\"", "String" },
			{ "true", "Boolean" },
			{ "null", "null" },
			{ "/a/g", "RegExp" },
			{ "[1, 2]", "Array" },
			{ "({ a: 1 })", "Object" },
			{ "new <int>[1, 2]", "Vector.<int>" },
			{ "(function():void {})", "Function" },
			{ "param", "int" },
			{ "rest", "Array" },
			{ "count", "int" },
			{ "big", "uint" },
			{ "label", "String" },
			{ "any", "*" },
			{ "total", "int" },
			{ "inherited", "String" },
			{ "size", "Number" },
			{ "area", "Function" },
			{ "Main", "Class.<Main>" },
			{ "Math", "Class.<Math>" },
			{ "Shape", "Class.<Shape>" },
			{ "trace", "Function" },
			{ "NaN", "Number" },
			{ "undefined", "*" },
			{ "arguments", "*" },
			{ "this", "Main" },
			{ "grid", "Vector.<Vector.<String>>" },
	});
}

static void test_operators() {
	check_types({
			{ "count + 1", "Number" },
			{ "count + big", "Number" },
			{ "label + 1", "String" },
			{ "1 + label", "String" },
			{ "any + 1", "*" },
			{ "count - any", "Number" },
			{ "count * ratio", "Number" },
			{ "count / 2", "Number" },
			{ "count % 2", "Number" },
			{ "count & 1", "int" },
			{ "count | big", "int" },
			{ "count << 2", "int" },
			{ "count >> 2", "int" },
			{ "count >>> 1", "uint" },
			{ "~count", "int" },
			{ "-1", "int" },
			{ "-2147483648", "int" },
			{ "-count", "Number" },
			{ "+label", "Number" },
			{ "!flag", "Boolean" },
			{ "count < 1", "Boolean" },
			{ "label == null", "Boolean" },
			{ "any === other", "Boolean" },
			{ "any is Main", "Boolean" },
			{ "\"x\" in obj", "Boolean" },
			{ "any as Main", "Main" },
			{ "any as Shape", "Shape" },
			{ "typeof any", "String" },
			{ "delete obj.x", "Boolean" },
			{ "count++", "int" },
			{ "--ratio", "Number" },
			{ "flag ? count : ratio", "Number" },
			{ "flag ? other : null", "Main" },
			{ "flag ? null : label", "String" },
			{ "flag ? label : count", "*" },
			{ "other || other", "Main" },
			{ "flag && count", "*" },
			{ "count = 1", "int" },
			{ "ratio += 1", "Number" },
			{ "label += 1", "String" },
			{ "(count, label)", "String" },
			// An object of the program becomes a primitive as the line runs: beside `+` a String or a Number, else a Number.
			{ "other + other", "*" },
			{ "other * 2", "Number" },
			{ "-other", "?" },
	});
}

static void test_calls_and_new() {
	check_types({
			{ "area()", "Number" },
			{ "untyped()", "*" },
			{ "make()", "Main" },
			{ "Main.make()", "Main" },
			{ "other.area()", "Number" },
			{ "base_method()", "Base" },
			{ "super.base_method()", "Base" },
			{ "trace(1)", "void" },
			{ "int(ratio)", "int" },
			{ "String(count)", "String" },
			{ "Main(any)", "Main" },
			{ "Shape(any)", "Shape" },
			{ "Vector.<int>(list)", "Vector.<int>" },
			{ "fn()", "*" },
			{ "any()", "*" },
			{ "any.thing(1)", "*" },
			{ "Math.max(1, 2)", "Number" },
			{ "Math.floor(ratio)", "Number" },
			{ "label.charAt(0)", "String" },
			{ "label.split(\",\")", "Array" },
			{ "count.toString()", "String" },
			{ "ratio.toFixed(2)", "String" },
			{ "new Main()", "Main" },
			{ "new Main", "Main" },
			{ "new Vector.<String>()", "Vector.<String>" },
			{ "new Array()", "Array" },
			{ "new any()", "*" },
			{ "new Error(\"x\").message", "String" },
			{ "parseInt(\"1\")", "Number" },
			{ "isNaN(ratio)", "Boolean" },
			{ "String.fromCharCode(65)", "String" },
			{ "JSON.parse(label)", "Object" },
	});
}

static void test_members() {
	// A static and an instance member of one name.
	check_types({ { "Main.twin", "int" }, { "this.twin", "String" }, { "twin", "String" }, { "other.twin", "String" } });
	check_types({
			{ "other.count", "int" },
			{ "other.other.label", "String" },
			{ "other.inherited", "String" },
			{ "other.size", "Number" },
			{ "other.mine", "int" },
			{ "other.guarded", "int" },
			{ "other.near", "int" },
			{ "this.count", "int" },
			{ "shape.area()", "Number" },
			{ "shape.size", "Number" },
			{ "shape.toString()", "String" },
			{ "label.length", "int" },
			{ "list.length", "uint" },
			{ "list[0]", "*" },
			{ "list.whatever", "*" },
			{ "list.pop()", "*" },
			{ "obj.anything", "*" },
			{ "obj.anything.more", "*" },
			{ "obj[label]", "*" },
			{ "any.thing.more", "*" },
			{ "ints[0]", "int" },
			{ "ints.pop()", "int" },
			{ "ints.length", "uint" },
			{ "ints.concat()", "Vector.<int>" },
			{ "ints.indexOf(1)", "int" },
			{ "grid[0]", "Vector.<String>" },
			{ "grid[0][1]", "String" },
			{ "grid[0].pop()", "String" },
			{ "grid.pop().join()", "String" },
			{ "grid.slice()[0].shift().length", "int" },
			{ "Main.total", "int" },
			{ "Base.base_total", "int" },
			{ "Math.PI", "Number" },
			{ "int.MAX_VALUE", "int" },
			{ "node.name", "String" },
			{ "node.position", "Number" },
			{ "node.get_parent().get_parent()", "Node" },
			{ "Node.make()", "Node" },
			{ "godot.Node.make().name", "String" },
			{ "fn.call(null)", "*" },
			{ "area.length", "int" },
			{ "new Date().fullYear", "Number" },
			{ "/a/.test(label)", "Boolean" },
	});
	// `this` in a closure is whatever the caller makes it; in the method it is the class.
	check_types({
			{ "var f:Function = function():void {\nthis;\n}", "Function" },
			{ "", "*" },
	});
}

static void test_errors() {
	check_errors("other./*!*/nope; other./*!*/nope(); label./*!*/nope; shape./*!*/nope; node./*!*/nope; count./*!*/nope;",
			{ "'Main' has no member 'nope'", "'Main' has no member 'nope'", "'String' has no member 'nope'",
					"'Shape' has no member 'nope'", "'Node' has no member 'nope'", "'int' has no member 'nope'" });
	check_errors("Main./*!*/nope; Main./*!*/base_total; Math./*!*/nope;",
			{ "'Main' has no static member 'nope'", "'Main' has no static member 'base_total'",
					"'Math' has no static member 'nope'" });
	check_errors("other./*!*/secret; node./*!*/secret;", { "'secret' is private in Base", "'secret' is private in Node" });
	check_errors("other./*!*/total; other./*!*/make();",
			{ "'total' is a static member of Main; write Main.total", "'make' is a static member of Main; write Main.make" });
	check_errors("Main./*!*/count; Main./*!*/area();", { "'count' is an instance member of Main; it needs an object",
															   "'area' is an instance member of Main; it needs an object" });
	check_errors("", { "'guarded' is protected in Base", "'mine' is private in Main" },
			"class Outsider { function f(m:Main):void { m./*!*/guarded; m./*!*/mine; m.near; m.count; } }\n");
	// Nothing follows from a name that failed to resolve, or from an unknown base.
	check_errors("/*!*/missing.a.b.c(); var x:/*!*/Gone; x.a.b;", { "undefined name 'missing'", "unknown type 'Gone'" });
	check_errors("", { "unknown type 'Gone'" },
			"class Broken extends /*!*/Gone { function f(b:Broken):void { b.anything; this.other.thing; } }\n");
	// A subclass of a dynamic class is sealed.
	check_errors("", { "'Sub' has no member 'nope'" },
			"class Sub extends Array { function f(s:Sub):void { s.length; s.push(1); s./*!*/nope; } }\n");

	// Internal across packages.
	Compilation compilation;
	compilation.add("a/Thing.as", "package a { public class Thing { internal var near:int; public var far:int; } }");
	compilation.add("Main.as", "package { import a.Thing; public class Main { function f(t:Thing):void { t.far; t.near; } } }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false),
			std::string("Main.as:1:83: error: 'near' in Thing is not visible from this package\n"));
}

static void test_review_findings() {
	check_types({
			// Only a number indexes a vector's elements.
			{ "ints[label]", "*" },
			{ "ints[\"length\"]", "*" },
			{ "ints[any]", "*" },
			{ "ints[count]", "int" },
			{ "ints[big]", "int" },
			// `as` gives null when the value is not of the type, so it is never a bare number or Boolean.
			// As in Adobe's compiler; as3corelib stores `x as Number` in a Number.
			{ "any as int", "int" },
			{ "any as Number", "Number" },
			{ "any as Boolean", "Boolean" },
			{ "any as String", "String" },
			// Compound bitwise assignment.
			{ "any &= 1", "int" },
			{ "any <<= 1", "int" },
			{ "any >>>= 1", "uint" },
			{ "flag |= true", "int" },
			{ "count &= 3", "int" },
			// Literals.
			{ "0x00000000000000000001", "int" },
			{ "0x000000000000000000FF", "int" },
			{ "-0", "Number" },
			{ "-0x80000000", "int" },
			{ "-2147483649", "Number" },
			// A string plus anything is a string.
			{ "\"\" + obj", "String" },
			{ "\"at \" + this", "String" },
			{ "other + \"!\"", "String" },
			{ "label += obj", "String" },
			// Calls and `new` whose result is decided when the program runs.
			{ "new cls()", "*" },
			{ "cls(obj)", "*" },
			{ "new fn()", "*" },
			{ "obj()", "*" },
			{ "cls.NAME", "*" },
			{ "cls.create()", "*" },
			// More of what the plan names.
			{ "shape.width", "Number" },
			{ "only", "String" },
			{ "count += 1", "int" },
			{ "++count", "int" },
			{ "flag &&= flag", "Boolean" },
			{ "flag ? 1 : null", "*" },
			{ "void 0", "*" },
			{ "super.inherited", "String" },
	});
	// `this` in a nested named function, as in a function expression, is the caller's.
	check_types({ { "function inner():void {\nthis;\n}", "(none)" }, { "", "*" } });

	// Unknown is not `*`: after an error, and on void and null.
	check_types({
			{ "missing_name", "?" },
			{ "missing_name.a.b", "?" },
			{ "other.nope", "?" },
			{ "other.nope.more", "?" },
			{ "other.secret", "?" },
			{ "Main.nope", "?" },
			{ "trace(1).x", "?" },
			{ "null.x", "?" },
			{ "trace(1)[0]", "?" },
			{ "missing_name + 1", "?" },
			{ "flag ? missing_name : 1", "?" },
			{ "missing_name || other", "?" },
			{ "missing_name[0]", "?" },
			{ "any as missing_name", "?" },
			{ "new missing_name()", "?" },
			{ "missing_name()", "?" },
			{ "var gone:Gone = null; gone", "null" },
			{ "gone", "?" },
			{ "gone.x", "?" },
	}, false);

	// A static of a subclass does not hide an inherited instance member of the same name.
	check_errors("", {},
			"class Low { public var count:String; }\n"
			"class High extends Low { public static var count:int; public static function toString():String { return \"\"; }\n"
			"function f(h:High):void { h.count.charAt(0); h.toString(); High.count.toFixed(1); } }\n");
	// `super` is the base class; `super(...)` has no value.
	check_errors("super./*!*/mine;", { "'Base' has no member 'mine'" });
	// Every place an expression can appear is typed.
	check_errors("try {} catch (e:Error) { other./*!*/a1; }\n"
				 "switch (count) { case other./*!*/a2: other./*!*/a3; }\n"
				 "for (var i:int = 0; i < 1; other./*!*/a4) {}\n"
				 "var f:Function = function(x:* = other./*!*/a5):* { return other./*!*/a6; };\n"
				 "do { throw other./*!*/a7; } while (other./*!*/a8);\n"
				 "for each (other./*!*/a9 in other./*!*/a10) {}\n"
				 "if (other./*!*/a11) {} else { lab: { other./*!*/a12; } }",
			{ "'Main' has no member 'a1'", "'Main' has no member 'a2'", "'Main' has no member 'a3'",
					"'Main' has no member 'a4'", "'Main' has no member 'a5'", "'Main' has no member 'a6'",
					"'Main' has no member 'a7'", "'Main' has no member 'a8'", "'Main' has no member 'a9'",
					"'Main' has no member 'a10'", "'Main' has no member 'a11'", "'Main' has no member 'a12'" });
	// Code after a class is outside it.
	check_errors("", { "'mine' is private in Main" }, "function peek(m:Main):void { m./*!*/mine; }\n");

	Compilation compilation;
	Unit *unit = compilation.add("Main.as", "package { public class Main extends Error {\n"
											"public function Main() {\nsuper();\n}\n"
											"override public function getStackTrace():String { return \"\"; }\n"
											"public static function last():void {} } }\n"
											"function later():void {\nthis;\n}\n"
											"class List extends Array { public var length:int; public function push(x:*):void {} }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	CHECK_EQ(unit->typed.size(), (size_t)2);
	if (unit->typed.size() == 2) {
		CHECK_EQ(compilation.type_name(unit->typed[0].type), std::string("void"));
		CHECK_EQ(compilation.type_name(unit->typed[1].type), std::string("*"));
	}
}

static void test_limits() {
	// A long chain on an unknown root, and on a dynamic one.
	std::string chain = "any";
	for (int i = 0; i < 440; i++) {
		chain += ".link";
	}
	check_types({ { chain.c_str(), "*" } });
}

int main() {
	test_literals_and_names();
	test_operators();
	test_calls_and_new();
	test_members();
	test_errors();
	test_review_findings();
	test_limits();
	return check::finish();
}
