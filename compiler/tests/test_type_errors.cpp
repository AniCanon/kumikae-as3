// Type errors: a value that does not fit where it is used.
#include "builtins.h"
#include "check.h"
#include "marked.h"
#include "names.h"

#include <initializer_list>
#include <string>

using namespace kas3;

// Main.as: a class with a field of every kind, extra members, and a method whose body is p_body.
static std::string program(const std::string &p_body, const std::string &p_members = std::string(),
		const std::string &p_extra = std::string()) {
	return "package { public class Main extends Base {\n"
		   "public var count:int; public var ratio:Number; public var label:String; public var flag:Boolean;\n"
		   "public var any:*; public var obj:Object; public var base:Base; public var sub:Sub; public var other:Other;\n"
		   "public var shape:Shape; public var round:Round; public var ints:Vector.<int>; public var subs:Vector.<Sub>;\n"
		   "public var list:Array; public var fn:Function; public static var total:int;\n"
		   "public function nothing():void {}\n" +
			p_members + "\npublic function run():void {\n" + p_body +
			"\n}\n} }\n"
			"class Base {} class Sub extends Base implements Shape {} final class Other {}\n"
			"interface Shape {} interface Round extends Shape {} class Ball implements Round {}\n" +
			p_extra;
}

static void expect(const std::string &p_source, std::initializer_list<const char *> p_messages) {
	MarkedSource source = mark("Main.as", p_source, p_messages);
	Compilation compilation;
	compilation.add("Main.as", source.text);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(), source.expected);
}

#define UNRELATED(from, to) "can't use a value of type '" from "' where '" to "' is expected"
#define NEEDS_CAST(from, to) "a value of type '" from "' may not be a '" to "'; use 'as' or a cast"

static void test_conversions_accepted() {
	expect(program("var a:int = 1.5; var b:Number = 1; var c:uint = -1; var d:Boolean = \"x\"; var e:Object = 1;\n"
				   "var f:* = \"s\"; var g:String = any; var h:Base = new Sub(); var i:Shape = new Sub();\n"
				   "var j:Shape = new Ball(); var k:Round = new Ball(); var l:Object = new Sub(); var m:String = null;\n"
				   "var n:int = null; var o:Vector.<int> = new Vector.<int>(); var p:Class = Sub; var q:Object = Sub;\n"
				   "var r:Function = run; var s:Object = shape; var t:Array = []; var u:Shape = round; var v:Base = null;\n"
				   "var w:Object = new <int>[1]; var x:Number = count + 1; var y:String = \"a\" + 1; var z:Boolean = base;\n"
				   "var aa:Base = this; var ab:* = nothing(); var ac:Object = fn; var ad:Number = count; var ae:int = ratio;\n"
				   "var af:Vector.<Sub> = subs; var ag:Function = function():void {}; var ah:Object = null;\n"
				   "var ai:Boolean = obj; var aj:String = flag ? \"a\" : null; var ak:Base = flag ? sub : base;"),
			{});
}

static void test_conversions_rejected() {
	expect(program("var a:String = /*!*/1; var b:Number = /*!*/\"5\"; var c:int = /*!*/true; var d:Sub = /*!*/base;\n"
				   "var e:String = /*!*/obj; var f:Base = /*!*/other; var g:Shape = /*!*/base; var h:Sub = /*!*/shape;\n"
				   "var i:Other = /*!*/shape; var j:Round = /*!*/shape; var k:Vector.<Base> = /*!*/subs;\n"
				   "var l:Array = /*!*/ints; var m:int = /*!*/nothing(); var n:String = /*!*/Sub; var o:Function = /*!*/obj;\n"
				   "var p:Sub = /*!*/new Base(); var q:Vector.<int> = /*!*/list; var r:Vector.<int> = /*!*/obj;\n"
				   "var s:String = /*!*/ratio; var t:Class = /*!*/sub; var u:Number = /*!*/label;"),
			{ UNRELATED("int", "String"), UNRELATED("String", "Number"), UNRELATED("Boolean", "int"),
					NEEDS_CAST("Base", "Sub"), NEEDS_CAST("Object", "String"), UNRELATED("Other", "Base"),
					UNRELATED("Base", "Shape"), NEEDS_CAST("Shape", "Sub"), UNRELATED("Shape", "Other"),
					NEEDS_CAST("Shape", "Round"), UNRELATED("Vector.<Sub>", "Vector.<Base>"),
					UNRELATED("Vector.<int>", "Array"), UNRELATED("void", "int"), UNRELATED("Class.<Sub>", "String"),
					NEEDS_CAST("Object", "Function"), NEEDS_CAST("Base", "Sub"), UNRELATED("Array", "Vector.<int>"),
					NEEDS_CAST("Object", "Vector.<int>"), UNRELATED("Number", "String"), UNRELATED("Sub", "Class"),
					UNRELATED("String", "Number") });
}

static void test_stores() {
	// Plain assignment, to a name and to a member.
	expect(program("label = /*!*/1; this.label = /*!*/count; count = 1.5; sub = /*!*/base; Main.total = /*!*/\"x\";\n"
				   "list[0] = 1; any = 1; obj = label; ints[0] = 2; base = sub; label = any;"),
			{ UNRELATED("int", "String"), UNRELATED("int", "String"), NEEDS_CAST("Base", "Sub"),
					UNRELATED("String", "int") });
	// Compound assignment is not checked.
	expect(program("label += 1; count += 1.5; count -= ratio; flag &&= count;"), {});
	// Fields, static fields and package variables.
	expect(program("", "public var bad:String = /*!*/1; public static var worse:int = /*!*/\"x\"; public var fine:Number = 2;\n"
					   "public const LIMIT:Sub = /*!*/new Base();",
				   "var loose:Other = /*!*/new Base();\n"),
			{ UNRELATED("int", "String"), UNRELATED("String", "int"), NEEDS_CAST("Base", "Sub"),
					UNRELATED("Base", "Other") });
}

static void test_unknown_is_silent() {
	expect(program("var x:/*!*/Gone = 1; var s:String = /*!*/missing; var t:String = x; x = 1;"),
			{ "unknown type 'Gone'", "undefined name 'missing'" });
	// A class whose base or interface did not resolve may be anything.
	expect(program("var k:Lost = null; var b:Other = k; var s:Shape = k; var h:Half = null; var r:Round = h; k = other;", "",
				   "class Lost extends /*!*/Gone {} class Half implements /*!*/Nope {}\n"),
			{ "unknown type 'Gone'", "unknown type 'Nope'" });
	// An unknown element type.
	expect(program("var v:Vector.</*!*/Gone> = null; ints = v; v = ints; list = v;"), { "unknown type 'Gone'" });
}

static const char *const CALLABLES =
		"public function none():void {} public function one(a:int):void {} public function two(a:int, b:String):void {}\n"
		"public function opt(a:int, b:String = null, c:Number = 0):void {} public function rest(a:int, ...more):void {}\n"
		"public function loose(a, b:*):void {} public static function fixed(a:Base):void {}\n"
		"public function get size():int { return 0; } public function takes(s:Shape, v:Vector.<int> = null):void {}\n";

static void test_argument_counts() {
	expect(program("none(); one(1); two(1, \"a\"); opt(1); opt(1, \"a\"); opt(1, \"a\", 2); rest(1); rest(1, 2, 3, \"x\");\n"
				   "loose(1, 2); Main.fixed(sub); fixed(null); this.one(1); sub2.one(2); fn(1, 2, 3); any.one(); obj.one(1, 2);\n"
				   "helper(1); new Thing(1); new Thing(1, \"a\"); new Plain(); new Plain; new Base(); int(1.5); String(1);\n"
				   "Sub(base); new Array(1, 2, 3); new Vector.<int>(3, true); new Date(1, 2, 3); new Error(\"x\", 2); trace(1, 2);\n"
				   "var local:Function = function(a:int):void {}; local(); local(1, 2); nested(1);\n"
				   "function nested(a:int):void {}",
				   std::string(CALLABLES) + "public var sub2:Main;",
				   "function helper(a:int, b:int = 0):void {} class Thing { public function Thing(a:int, b:String = null) {} }\n"
				   "class Plain {}\n"),
			{});
	expect(program("/*!*/none(1); /*!*/one(); /*!*/one(1, 2); /*!*/two(1); /*!*/opt(); /*!*/opt(1, \"a\", 2, 3); /*!*/rest();\n"
				   "Main./*!*/fixed(); this./*!*/two(); /*!*/helper(); /*!*/helper(1, 2, 3); new /*!*/Thing(); new /*!*/Thing(1, \"a\", 3);\n"
				   "new /*!*/Plain(1); new /*!*/Thing; /*!*/nested(); /*!*/nested(1, 2); sub2./*!*/one();\n"
				   "function nested(a:int):void {}",
				   std::string(CALLABLES) + "public var sub2:Main;",
				   "function helper(a:int, b:int = 0):void {} class Thing { public function Thing(a:int, b:String = null) {} }\n"
				   "class Plain {}\n"),
			{ "'none' takes no arguments but 1 was given", "'one' takes 1 argument but 0 were given",
					"'one' takes 1 argument but 2 were given", "'two' takes 2 arguments but 1 was given",
					"'opt' takes at least 1 argument but 0 were given", "'opt' takes at most 3 arguments but 4 were given",
					"'rest' takes at least 1 argument but 0 were given", "'fixed' takes 1 argument but 0 were given",
					"'two' takes 2 arguments but 0 were given", "'helper' takes at least 1 argument but 0 were given",
					"'helper' takes at most 2 arguments but 3 were given", "'Thing' takes at least 1 argument but 0 were given",
					"'Thing' takes at most 2 arguments but 3 were given", "'Plain' takes no arguments but 1 was given",
					"'Thing' takes at least 1 argument but 0 were given", "'nested' takes 1 argument but 0 were given",
					"'nested' takes 1 argument but 2 were given", "'one' takes 1 argument but 0 were given" });
	// `super(...)` is a call of the base class's constructor.
	expect("package { public class Main extends Base { public function Main() { /*!*/super(); } } }\n"
		   "class Base { public function Base(a:int) {} }\n"
		   "class Fine extends Base { public function Fine() { super(1); } }\n"
		   "class Wrong extends Base { public function Wrong() { super(/*!*/\"x\"); } }\n"
		   "class Bare extends Plain { public function Bare() { /*!*/super(1); } } class Plain {}\n",
			{ "'Base' takes 1 argument but 0 were given", UNRELATED("String", "int"),
					"'Plain' takes no arguments but 1 was given" });
}

static void test_argument_types() {
	expect(program("one(1.5); one(any); two(count, label); two(1, null); opt(1, null, count); rest(1, \"x\", obj);\n"
				   "loose(\"a\", sub); fixed(sub); fixed(this); takes(sub); takes(round, ints); takes(null, null);\n"
				   "ints.push(1, 2); ints.push(ratio); subs.push(sub); subs.push(null); subs.indexOf(sub); list.push(1, \"a\");\n"
				   "label.charAt(1); Math.max(1, 2, 3); new Thing(count); helper(ratio);",
				   CALLABLES, "function helper(a:int):void {} class Thing { public function Thing(a:int) {} }\n"),
			{});
	expect(program("one(/*!*/\"a\"); two(/*!*/label, /*!*/count); opt(1, /*!*/2); rest(/*!*/label, label); fixed(/*!*/other);\n"
				   "fixed(/*!*/obj); takes(/*!*/base); takes(sub, /*!*/subs); ints.insertAt(0, /*!*/\"a\"); subs.insertAt(0, /*!*/base);\n"
				   "subs.indexOf(sub, /*!*/other); ints.push(\"unchecked\"); label.charAt(/*!*/label); new Thing(/*!*/label); helper(/*!*/flag);\n"
				   "one(/*!*/nothing());",
				   CALLABLES, "function helper(a:int):void {} class Thing { public function Thing(a:int) {} }\n"),
			{ UNRELATED("String", "int"), UNRELATED("String", "int"), UNRELATED("int", "String"),
					UNRELATED("int", "String"), UNRELATED("String", "int"), UNRELATED("Other", "Base"),
					NEEDS_CAST("Object", "Base"), UNRELATED("Base", "Shape"),
					UNRELATED("Vector.<Sub>", "Vector.<int>"), UNRELATED("String", "int"), NEEDS_CAST("Base", "Sub"),
					UNRELATED("Other", "int"), UNRELATED("String", "Number"), UNRELATED("String", "int"),
					UNRELATED("Boolean", "int"), UNRELATED("void", "int") });
	// A vector whose element type is unknown takes anything.
	expect(program("var v:Vector.</*!*/Gone> = null; v.push(1, \"a\", sub);"), { "unknown type 'Gone'" });
}

#define VOID_RETURN "a function declared 'void' can't return a value"

static void test_returns() {
	// What fits the declared type.
	expect(program("", "public function a():int { return 1.5; } public function b():String { return null; }\n"
					   "public function c():Base { return sub; } public function d():* { return 1; }\n"
					   "public function e() { return 1; } public function f():void { return; }\n"
					   "public function g():Shape { if (flag) { return sub; } return null; }\n"
					   "public function get h():Number { return count; } public function i():Object { return 1; }\n"
					   "public function j():void { nothing(); } public function k():int { throw new Error(\"no\"); }\n"
					   "public function l():void { return nothing(); } public function set m(v:int):void { return; }\n"
					   "public function n():int { while (true) { if (flag) { return 1; } } }\n"
					   "public function o():Function { return function():int { return 1; }; }\n"
					   "public function p():void { var q:Function = function():int { return 1; }; }"),
			{});
	// What does not.
	expect(program("", "public function a():String { return /*!*/1; } public function b():Sub { return /*!*/base; }\n"
					   "public function c():int { return /*!*/label; } public function get d():String { return /*!*/count; }\n"
					   "public function e():Vector.<int> { return /*!*/subs; }\n"
					   "public function f():int { var g:Function = function():String { return /*!*/1; }; return /*!*/\"x\"; }"),
			{ UNRELATED("int", "String"), NEEDS_CAST("Base", "Sub"), UNRELATED("String", "int"),
					UNRELATED("int", "String"), UNRELATED("Vector.<Sub>", "Vector.<int>"), UNRELATED("int", "String"),
					UNRELATED("String", "int") });
	// A value from a function that has none to give.
	expect(program("", "public function a():void { return /*!*/1; } public function set b(v:int):void { return /*!*/v; }\n"
					   "public function c():void { var d:Function = function():int { return 1; }; return /*!*/label; }\n"
					   "public function e():int { var f:Function = function():void { return /*!*/2; }; return 1; }"),
			{ VOID_RETURN, VOID_RETURN, VOID_RETURN, VOID_RETURN });
	expect("package { public class Main { public function Main() { return /*!*/1; } } }\n"
		   "class Other { public function Other() { return; } }\n",
			{ "a constructor can't return a value" });
	// A declared type and nothing returned at all.
	expect(program("", "public function /*!*/a():int {} public function get /*!*/b():String { nothing(); }\n"
					   "public function /*!*/c():Base { var d:Function = function():int { return 1; }; }\n"
					   "public function e():int { var f:Function = /*!*/function():String {}; return 1; }\n"
					   "public function /*!*/g():int { return; }",
				   "function /*!*/loose():Number {}\n"),
			{ "'a' is declared to return 'int' but never returns a value",
					"'b' is declared to return 'String' but never returns a value",
					"'c' is declared to return 'Base' but never returns a value",
					"this function is declared to return 'String' but never returns a value",
					"'g' is declared to return 'int' but never returns a value",
					"'loose' is declared to return 'Number' but never returns a value" });
	// Declarations without bodies return nothing and say nothing.
	expect("package { public class Main implements Shape { public function area():Number { return 1; } } }\n"
		   "interface Shape { function area():Number; }\n",
			{});
}

static void test_parameter_defaults() {
	expect(program("", "public function a(x:int = 1, y:String = null, z:Number = 2, w:Object = \"s\", v:Boolean = false, u:* = 1,"
					   " t = 2, s:Array = null, r:uint = 0xFF):void {}\n"
					   "public function b(x:int = /*!*/\"a\", y:String = /*!*/1, z:Sub = null):void {}"),
			{ UNRELATED("String", "int"), UNRELATED("int", "String") });
}

#define SAME_AS(name, owner) "'" name "' must have the same parameters and return type as '" name "' in " owner

static void test_override_signatures() {
	const std::string base = "class Base { public function f(a:int, b:String = null):Number { return 0; }\n"
							 "public function g(...rest):void {} public function h():Base { return null; } public function k(a) {}\n"
							 "public function get size():int { return 0; } public function set size(v:int):void {}\n"
							 "private function hidden(a:int):void {} }\n";
	expect("package { public class Main extends Base {\n"
		   "override public function f(x:int, y:String = \"\"):Number { return 1; }\n"
		   "override public function g(...more):void {} override public function h():Base { return this; }\n"
		   "override public function k(a:*):* {} override public function get size():int { return 1; }\n"
		   "override public function set size(value:int):void {} } }\n" +
					base + "class Deep extends Main { override public function f(a:int, b:String = null):Number { return 2; } }\n",
			{});
	expect("package { public class Main extends Base {\n"
		   "override public function /*!*/f(a:int):Number { return 1; }\n"
		   "override public function /*!*/g(a:int):void {}\n"
		   "override public function /*!*/h():Main { return this; }\n"
		   "override public function /*!*/k(a:int) {}\n"
		   "override public function get /*!*/size():Number { return 1; }\n"
		   "override public function set /*!*/size(value:Number):void {} } }\n" +
					base +
					"class A extends Base { override public function /*!*/f(a:int, b:String):Number { return 1; } }\n"
					"class B extends Base { override public function /*!*/f(a:Number, b:String = null):Number { return 1; } }\n"
					"class C extends Base { override public function /*!*/f(a:int, b:String = null):int { return 1; } }\n"
					"class D extends Base { override public function /*!*/f(a:int, b:String = null, c:int = 0):Number { return 1; } }\n"
					"class E extends Base { override public function /*!*/h():void {} }\n",
			{ SAME_AS("f", "Base"), SAME_AS("g", "Base"), SAME_AS("h", "Base"), SAME_AS("k", "Base"), SAME_AS("size", "Base"),
					SAME_AS("size", "Base"), SAME_AS("f", "Base"), SAME_AS("f", "Base"), SAME_AS("f", "Base"),
					SAME_AS("f", "Base"), SAME_AS("h", "Base") });
	// The same type written two ways, and types that did not resolve.
	Compilation compilation;
	compilation.add("lib/Thing.as", "package lib { public class Thing {} }");
	compilation.add("lib/Base.as", "package lib { public class Base { public function f(t:Thing):lib.Thing { return t; }\n"
								   "public function g(t:Gone):void {} } }");
	compilation.add("Main.as", "package { import lib.*; public class Main extends Base {\n"
							   "override public function f(t:lib.Thing):Thing { return t; }\n"
							   "override public function g(t:int):void {} } }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(), std::string("lib/Base.as:2:21: error: unknown type 'Gone'\n"));
}

static void test_interfaces() {
	const std::string shapes = "interface Shape extends Sized { function area(scale:Number = 1):Number; function get name():String;\n"
							   "function set name(value:String):void; }\n"
							   "interface Sized { function get width():int; }\n";
	expect("package { public class Main implements Shape {\n"
		   "public function area(scale:Number = 1):Number { return 0; } public function get name():String { return null; }\n"
		   "public function set name(v:String):void {} public function get width():int { return 0; } } }\n" +
					shapes +
					"class Half { public function area(scale:Number = 1):Number { return 0; } public function get width():int { return 0; } }\n"
					"class Whole extends Half implements Shape { public function get name():String { return null; }\n"
					"public function set name(v:String):void {} }\n"
					"class Child extends Main {} class Again extends Main implements Sized {}\n"
					"class Unsure extends /*!*/Gone implements Shape {} class Odd implements Shape, /*!*/Nope {}\n",
			{ "unknown type 'Gone'", "unknown type 'Nope'" });
	expect("package { public class /*!*//*!*//*!*//*!*/Main implements Shape {} }\n" + shapes +
					"class /*!*/A implements Sized { public var width:int; }\n"
					"class B implements Sized { function get /*!*/width():int { return 0; } }\n"
					"class C implements Sized { public function get /*!*/width():Number { return 0; } }\n"
					"class D implements Shape { public function /*!*/area(scale:Number):Number { return 0; }\n"
					"public function get name():String { return null; } public function set /*!*/name(v:Object):void {}\n"
					"public function get width():int { return 0; } }\n"
					"class /*!*/E implements Sized { public static function get width():int { return 0; } }\n"
					"class /*!*/F implements Shape { public function area(scale:Number = 1):Number { return 0; }\n"
					"public function get name():String { return null; } public function get width():int { return 0; } }\n",
			{ "'Main' must implement 'area' from Shape", "'Main' must implement the getter 'name' from Shape",
					"'Main' must implement the setter 'name' from Shape", "'Main' must implement the getter 'width' from Sized",
					"'A' must implement the getter 'width' from Sized", "'width' must be public to implement Sized",
					SAME_AS("width", "Sized"), SAME_AS("area", "Shape"), SAME_AS("name", "Shape"),
					"'E' must implement the getter 'width' from Sized", "'F' must implement the setter 'name' from Shape" });
	// A class in a declaration file is taken at its word.
	Compilation compilation;
	compilation.add("lib.as", "package lib { public interface Shape { function area():Number; }\n"
							  "public native class Disc implements Shape {} }",
			true);
	compilation.add("Main.as", "package { import lib.*; public class Main extends Disc {} }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(), std::string());
}

static void test_constants() {
	const std::string members = "public const LIMIT:int = 3; public static const MAX:int = 9; public var free:int;\n"
								"public function get size():int { return 0; } public function get both():int { return 0; }\n"
								"public function set both(v:int):void {} public function set only(v:int):void {}\n"
								"public function method():void {} public static function make():void {}\n";
	expect(program("free = 1; both = 2; only = 3; this.both++; count += 1; var a:int = LIMIT + MAX + size; list.length = 0;\n"
				   "const local:int = 1; var b:int = local; ints.length = 3; any.LIMIT = 1; obj.size = 2; ints[0]++;",
				   members),
			{});
	expect(program("/*!*/LIMIT = 1; this./*!*/LIMIT = 2; /*!*/MAX += 1; Main./*!*/MAX = 3; /*!*/LIMIT++; --/*!*/MAX;\n"
				   "const local:int = 1; /*!*/local = 2; /*!*/local -= 1; /*!*/size = 4; this./*!*/size += 1; base2./*!*/size = 1;\n"
				   "/*!*/method = null; this./*!*/method = fn; Main./*!*/make = null; /*!*/helper = null; /*!*/Main = null;\n"
				   "/*!*/Shape = null; /*!*/PACKAGE_LIMIT = 2; label./*!*/length = 1; /*!*/size++;",
				   members + "public var base2:Main;", "function helper():void {} const PACKAGE_LIMIT:int = 1;\n"),
			{ "can't assign to the constant 'LIMIT'", "can't assign to the constant 'LIMIT'",
					"can't assign to the constant 'MAX'", "can't assign to the constant 'MAX'",
					"can't assign to the constant 'LIMIT'", "can't assign to the constant 'MAX'",
					"can't assign to the constant 'local'", "can't assign to the constant 'local'", "'size' is read-only",
					"'size' is read-only", "'size' is read-only", "can't assign to the method 'method'",
					"can't assign to the method 'method'", "can't assign to the method 'make'",
					"can't assign to the function 'helper'", "can't assign to the class 'Main'",
					"can't assign to the class 'Shape'", "can't assign to the constant 'PACKAGE_LIMIT'",
					"'length' is read-only", "'size' is read-only" });
	// An instance constant may be given its value in the constructor of its own class, and nowhere else.
	expect("package { public class Main { public const LIMIT:int; public static const MAX:int = 1;\n"
		   "public function Main(other:Main) { LIMIT = 1; this.LIMIT = 2; /*!*/MAX = 3; other./*!*/LIMIT = 4;\n"
		   "var f:Function = function():void { /*!*/LIMIT = 5; }; } } }\n"
		   "class Sub extends Main { public function Sub() { super(null); /*!*/LIMIT = 6; } }\n",
			{ "can't assign to the constant 'MAX'", "can't assign to the constant 'LIMIT'",
					"can't assign to the constant 'LIMIT'", "can't assign to the constant 'LIMIT'" });
	// A property whose other half may be in a base that did not resolve, or in an interface, is left alone.
	expect("package { public class Main extends /*!*/Gone { public function get size():int { return 0; }\n"
		   "public function f(s:Sized):void { size = 1; s.width = 2; } } }\n"
		   "interface Sized { function get width():int; }\n",
			{ "unknown type 'Gone'" });
	// A setter inherited beside a getter declared here.
	expect("package { public class Main extends Base { override public function get size():int { return 0; }\n"
		   "public function f():void { size = 1; } } }\n"
		   "class Base { public function get size():int { return 0; } public function set size(v:int):void {} }\n",
			{});
}

// Valid programs that the first version of these checks rejected, found in review.
static void test_review_findings() {
	// Built-in declarations as loose as Adobe's.
	expect(program("var m:Array = /re/.exec(label); if (obj.hasOwnProperty(obj)) {} obj.isPrototypeOf(1);\n"
				   "obj.propertyIsEnumerable(count); label.split(); label.indexOf(); label.match(); label.replace();\n"
				   "label.search(); label.lastIndexOf(); Math.max(ratio); Math.min(); Math.max(); new RegExp(/a/);\n"
				   "new RegExp(); var n:Number = obj.valueOf(); subs.indexOf(base); subs.lastIndexOf(obj);"),
			{});
	// Built-in methods live on the prototype and may be replaced.
	expect(program("obj.toString = function():String { return \"x\"; }; list.toString = null; obj.valueOf = fn;"), {});
	// The value of an assignment used as a value: which type Adobe gives it is not known.
	expect(program("label = obj = \"x\"; sub = base = new Sub(); var s:String = (obj = \"y\"); one(obj = 1);",
				   "public function one(a:int):void {}"),
			{});
	// `return` of something that may be nothing, in a function declared void.
	expect(program("", "public function a():void { return undefined; } public function b():void { return fn(); }\n"
					   "public function c():void { return any; } public function d():void { return void 0; }\n"
					   "public function e():void { return /*!*/obj; }"),
			{ VOID_RETURN });
	// An unknown type anywhere inside a type.
	expect(program("var y:Vector.<Vector.</*!*/Gone>> = null; grids = y; y = grids; take(y);",
				   "public var grids:Vector.<Vector.<Main>>; public function take(v:Vector.<Vector.<Main>>):void {}"),
			{ "unknown type 'Gone'" });
	// Gaps the review's deliberate breakages showed.
	expect(program("var s:Shape = /*!*/obj;"), { NEEDS_CAST("Object", "Shape") });
	expect("package { public class Main { public const LIMIT:int;\n"
		   "public function Main() { var f:Function = function():void { /*!*/LIMIT = 5; }; } } }\n",
			{ "can't assign to the constant 'LIMIT'" });
	expect(program("", "public function a():int { return 1; function /*!*/g():int {} }"),
			{ "'g' is declared to return 'int' but never returns a value" });
	expect(program("flash.utils./*!*/getTimer = null; flash.utils./*!*/getTimer(1);"),
			{ "can't assign to the function 'getTimer'", "'getTimer' takes no arguments but 1 was given" });
	expect("package { public class /*!*/Main extends Base implements Shape { override public function /*!*/f(r:Array):void {} } }\n"
		   "class Base { public function f(...r):void {} private function area():void {} }\n"
		   "class /*!*/Sub extends Base implements Shape {}\n"
		   "interface Shape { function area():void; }\n"
		   "class Fine implements Shape { public function area() {} }\n",
			{ "'Main' must implement 'area' from Shape", SAME_AS("f", "Base"), "'Sub' must implement 'area' from Shape" });
}

// A program that uses Godot's real API.
static void expect_godot(const std::string &p_source, std::initializer_list<const char *> p_messages) {
	MarkedSource source = mark("Main.as", p_source, p_messages);
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Main.as", source.text);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(), source.expected);
}

// AS3's own collections convert when they are passed to the engine (performance redesign, section 7).
// What comes back from the engine stays a Godot collection.
static void test_engine_collections() {
	const std::string head = "package { import godot.*; public class Main extends Line2D {\n"
							 "public function run(list:Array, v2:Vector.<Vector2>, names:Vector.<String>, "
							 "numbers:Vector.<Number>, ints:Vector.<int>, o:Object, node:Node):void {\n";
	expect_godot(head + "points = v2; points = list; var a:GodotArray = list; a = names; a = [1, 2];\n"
						"var d:GodotDictionary = o; d = {x: 1}; d = new flash.utils.Dictionary();\n"
						"var f:PackedFloat32Array = numbers; f = ints; var b:PackedByteArray = ints;\n"
						"var s:PackedStringArray = names; setMeta(\"k\", list); addChild(node);\n"
						"var c:PackedColorArray = new <Color>[]; var i:PackedInt64Array = numbers; } } }\n",
			{});
	expect_godot(head + "points = /*!*/names; var f:PackedFloat32Array = /*!*/names; var a:GodotArray = /*!*/node;\n"
						"var d:GodotDictionary = /*!*/list; var back:Array = /*!*/a; var v:Vector.<Vector2> = /*!*/points;\n"
						"var s:PackedStringArray = /*!*/numbers; d = /*!*/node; var o2:flash.utils.Dictionary = /*!*/d;\n"
						"var c:PackedColorArray = /*!*/v2; } } }\n",
			{ UNRELATED("Vector.<String>", "PackedVector2Array"), UNRELATED("Vector.<String>", "PackedFloat32Array"),
					UNRELATED("Node", "GodotArray"), UNRELATED("Array", "GodotDictionary"),
					UNRELATED("GodotArray", "Array"), UNRELATED("PackedVector2Array", "Vector.<Vector2>"),
					UNRELATED("Vector.<Number>", "PackedStringArray"), UNRELATED("Node", "GodotDictionary"),
					UNRELATED("GodotDictionary", "Dictionary"), UNRELATED("Vector.<Vector2>", "PackedColorArray") });
	// A class of the same name outside the `godot` package gets no such treatment.
	expect("package { public class Main { public function run(list:Array):void { var a:GodotArray = /*!*/list; } } }\n"
		   "class GodotArray {}\n",
			{ UNRELATED("Array", "GodotArray") });
}

// What Godot's declarations say about creating and overriding.
static void test_godot_classes() {
	expect_godot("package { import godot.*; public class Main extends Node {\n"
				 "override public function _notification(what:int):void {}\n"
				 "public function toString():String { return \"main\"; }\n"
				 "public function run(o:GodotObject):void { o.free(); free(); var a:Node = new Node(); var m:Mine = new Mine(); } } }\n"
				 "import godot.CanvasItem;\nclass Mine extends CanvasItem {}\n",
			{});
	expect_godot("package { import godot.*; public class Main extends Node {\n"
				 "public function run():void { new /*!*/CanvasItem(); new /*!*/Input(); new /*!*/Key(); new godot./*!*/Viewport(); }\n"
				 "override public function /*!*/addChild(node:Node, forceReadableName:Boolean = false, internal_:int = 0):void {}\n"
				 "override public function get /*!*/name():String { return \"\"; }\n"
				 "override public function /*!*/_notification(what:String):void {} } }\n",
			{ "'CanvasItem' can't be created with 'new'", "'Input' can't be created with 'new'",
					"'Key' can't be created with 'new'", "'Viewport' can't be created with 'new'",
					"can't override 'addChild': it is final in Node", "can't override 'name': it is final in Node",
					"'_notification' must have the same parameters and return type as '_notification' in GodotObject" });
	// toString() is AS3's own: a script writes it as in any AS3 class, and Godot's two methods are not declared.
	expect_godot("package { import godot.*; public class Main extends Node {\n"
				 "override public function toString():String { return name; }\n"
				 "public function run(n:Node, o:GodotObject):String { return n.toString() + o.toString() + String(n); } } }\n",
			{});
	expect_godot("package { import godot.*; public class Main extends Node {\n"
				 "override public function /*!*/_toString():String { return \"\"; } } }\n",
			{ "'_toString' is marked 'override' but no base class has a method to override" });
	// The tag means nothing in a program's own files.
	expect("package { public class Main { public function run():void { new Helper(); } } }\n[Abstract] class Helper {}\n", {});
}

int main() {
	test_godot_classes();
	test_engine_collections();
	test_review_findings();
	test_constants();
	test_override_signatures();
	test_interfaces();
	test_returns();
	test_parameter_defaults();
	test_argument_counts();
	test_argument_types();
	test_conversions_accepted();
	test_conversions_rejected();
	test_stores();
	test_unknown_is_silent();
	return check::finish();
}
