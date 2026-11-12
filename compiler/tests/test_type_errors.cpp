// Type errors: a value that does not fit where it is used.
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
	expect(program("var v:Vector.</*!*/Gone> = null; ints = v; v = ints; list = /*!*/v;"),
			{ "unknown type 'Gone'", UNRELATED("Vector.<?>", "Array") });
}

int main() {
	test_conversions_accepted();
	test_conversions_rejected();
	test_stores();
	test_unknown_is_silent();
	return check::finish();
}
