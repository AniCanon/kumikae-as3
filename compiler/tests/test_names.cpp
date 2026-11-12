// Name resolution: what `kas3c check` reports.
#include "check.h"
#include "names.h"

#include <filesystem>
#include <initializer_list>
#include <string>

using namespace kas3;

struct File {
	const char *path;
	std::string text;
	bool declaration = false;
};

static std::string check_files(std::initializer_list<File> p_files) {
	Compilation compilation;
	for (const File &file : p_files) {
		compilation.add(file.path, file.text, file.declaration);
	}
	compilation.check();
	return compilation.format_diagnostics();
}

// The source may mark where errors are expected with `/*!*/`, placed just before the offending text. The
// markers are removed before checking, and each becomes "<path>:<line>:<column>: error: <message>".
struct Marked {
	std::string text;
	std::string expected;
};

static Marked marked(const char *p_path, const std::string &p_text, std::initializer_list<const char *> p_messages) {
	Marked result;
	const std::string marker = "/*!*/";
	int line = 1;
	int column = 1;
	auto message = p_messages.begin();
	for (size_t i = 0; i < p_text.size();) {
		if (p_text.compare(i, marker.size(), marker) == 0) {
			result.expected += std::string(p_path) + ":" + std::to_string(line) + ":" + std::to_string(column) +
					": error: " + (message != p_messages.end() ? *message++ : "MISSING MESSAGE") + "\n";
			i += marker.size();
			continue;
		}
		if (p_text[i] == '\n') {
			line++;
			column = 1;
		} else {
			column++;
		}
		result.text += p_text[i++];
	}
	return result;
}

// One file, Main.as, with its expected errors marked.
#define CHECK_MAIN(source, ...) \
	do { \
		Marked m_ = marked("Main.as", source, { __VA_ARGS__ }); \
		CHECK_EQ(check_files({ { "Main.as", m_.text } }), m_.expected); \
	} while (0)

// Main.as plus a second file that must itself be clean.
#define CHECK_WITH(other_path, other_text, source, ...) \
	do { \
		Marked m_ = marked("Main.as", source, { __VA_ARGS__ }); \
		CHECK_EQ(check_files({ { other_path, other_text }, { "Main.as", m_.text } }), m_.expected); \
	} while (0)

static void test_model() {
	CHECK_MAIN("package { public class Main {} }");
	CHECK_MAIN("");

	// The built-in declarations are themselves clean.
	Compilation builtins;
	builtins.check();
	CHECK(!builtins.has_errors());
	CHECK_EQ(builtins.format_diagnostics(), std::string());

	// A syntax error anywhere means only syntax errors are reported.
	std::string output = check_files({
			{ "A.as", "package { public class A { var x:Missing; } }" },
			{ "B.as", "package { public class B { var x = ; } }" },
	});
	CHECK(output.find("B.as:1:36: error:") == 0);
	CHECK(output.find("Missing") == std::string::npos);
}

static void test_declare() {
	// Duplicates at package level.
	CHECK_MAIN("package { public class Main {} }\nclass Helper {}\nclass /*!*/Helper {}",
			"'Helper' is already defined at line 2");
	CHECK_MAIN("package { public class Main {} }\nfunction f():void {}\nvar /*!*/f:int;",
			"'f' is already defined at line 2");
	CHECK_EQ(check_files({
					 { "a/Thing.as", "package game { public class Thing {} }" },
					 { "b/Thing.as", "package game { public class Thing {} }" },
			 }),
			std::string("b/Thing.as:1:29: error: 'Thing' is already defined at a/Thing.as:1:29\n"));
	CHECK_MAIN("package { public class /*!*//*!*/String {} }", "'String' must be defined in a file named String.as",
			"'String' is already defined at the built-in library");
	// The same name in two packages is fine.
	CHECK_EQ(check_files({
					 { "a/Thing.as", "package a { public class Thing {} }" },
					 { "b/Thing.as", "package b { public class Thing {} }" },
			 }),
			std::string());

	// One definition per package block, named after the file.
	CHECK_MAIN("package { public class Main {}\npublic class /*!*/Other {} }",
			"a file can define only one class, interface, function or variable inside its package block");
	CHECK_MAIN("package { public class /*!*/Player {} }", "'Player' must be defined in a file named Player.as");
	CHECK_EQ(check_files({ { "src/game/Player.as", "package game { public class Player {} }" } }), std::string());
	CHECK_EQ(check_files({ { "util.as", "package { public function util():void {} }" } }), std::string());
	// Declaration files are exempt from both rules.
	CHECK_EQ(check_files({ { "stubs.as", "package lib { public native class A {} public native class B {} }",
					 true } }),
			std::string());

	// Unsupported or misplaced modifiers.
	CHECK_MAIN("package { public dynamic class /*!*/Main {} }", "dynamic classes are not supported");
	CHECK_MAIN("package { public native class /*!*/Main {} }", "'native' is only allowed in declaration files");
	CHECK_MAIN("package { public class Main { native function /*!*/f():void; } }",
			"'native' is only allowed in declaration files");
	CHECK_MAIN("package { public native function /*!*/Main():void; }",
			"'native' is only allowed in declaration files");

	// Members.
	CHECK_MAIN("package { public class Main {\nvar x:int;\nvar /*!*/x:int;\n} }",
			"'x' is already defined at line 2");
	CHECK_MAIN("package { public class Main {\nvar x:int;\nfunction /*!*/x():void {}\n} }",
			"'x' is already defined at line 2");
	CHECK_MAIN("package { public class Main {\nstatic var x:int;\nvar /*!*/x:int;\n} }",
			"'x' is already defined at line 2");
	CHECK_MAIN("package { public class Main {\nvar a:int, b:int, /*!*/a:int;\n} }",
			"'a' is already defined at line 2");
	// A getter and a setter are one property.
	CHECK_MAIN("package { public class Main {\n"
			   "function get x():int { return 0; }\n"
			   "function set x(value:int):void {}\n} }");
	CHECK_MAIN("package { public class Main {\n"
			   "function get x():int { return 0; }\n"
			   "function get /*!*/x():int { return 0; }\n} }",
			"'x' is already defined at line 2");
	CHECK_MAIN("package { public class Main {\n"
			   "function get x():int { return 0; }\n"
			   "static function set /*!*/x(value:int):void {}\n} }",
			"'x' is already defined at line 2");
	CHECK_MAIN("package { public class Main {\nvar x:int;\nfunction get /*!*/x():int { return 0; }\n} }",
			"'x' is already defined at line 2");
	// The constructor.
	CHECK_MAIN("package { public class Main {\nfunction Main() {}\nfunction /*!*/Main() {}\n} }",
			"'Main' is already defined at line 2");
	CHECK_MAIN("package { public class Main {\nfunction Main() {}\nvar main:int;\n} }");
	// Interfaces.
	CHECK_MAIN("package { public interface Main {\nfunction f():void;\nfunction /*!*/f():void;\n} }",
			"'f' is already defined at line 2");
	CHECK_MAIN("package { public interface Main {\nfunction get x():int;\nfunction set x(v:int):void;\n} }");
}


static const char *const GODOT_STUB =
		"package godot {\n"
		"	public native class Node {\n"
		"		public var name:String;\n"
		"		private var secret:int;\n"
		"		public native function add_child(node:Node):void;\n"
		"		public native function get position():Number;\n"
		"		public native function set position(value:Number):void;\n"
		"		public final native function get_parent():Node;\n"
		"		public static native function make():Node;\n"
		"	}\n"
		"	public final native class Sealed {}\n"
		"	public native interface Drawable { function draw():void; }\n"
		"	public native function print(...rest):void;\n"
		"	internal native class Hidden {}\n"
		"}\n";

// Main.as plus the stand-in for Godot's classes.
#define CHECK_GODOT(source, ...) \
	do { \
		Marked m_ = marked("Main.as", source, { __VA_ARGS__ }); \
		CHECK_EQ(check_files({ { "godot.as", GODOT_STUB, true }, { "Main.as", m_.text } }), m_.expected); \
	} while (0)

static void test_imports() {
	CHECK_GODOT("package { import godot.Node; public class Main extends Node {} }");
	CHECK_GODOT("package { import godot.*; public class Main extends Node {} }");
	CHECK_GODOT("package { import flash.utils.Dictionary; import flash.utils.getTimer; public class Main {} }");
	CHECK_MAIN("package { import /*!*/godot.Node; public class Main {} }", "can't find 'godot.Node'");
	CHECK_MAIN("package { import /*!*/godot.*; public class Main {} }", "can't find package 'godot'");
	CHECK_GODOT("package { import /*!*/godot.Nope; public class Main {} }", "can't find 'godot.Nope'");
	CHECK_GODOT("package { import /*!*/godot.Hidden; public class Main {} }", "'godot.Hidden' is not public");
	// Importing a top-level name, and importing twice, are harmless.
	CHECK_GODOT("package { import String; import godot.Node; import godot.Node; import godot.*;\n"
				"public class Main extends Node {} }");
	// A project checked one file at a time: one error per import, and nothing else.
	CHECK_MAIN("package { import /*!*/a.B; import /*!*/c.D;\n"
			   "public class Main extends B { var d:D; var e:D; function f():void { g(); new D(); D.x; } } }",
			"can't find 'a.B'", "can't find 'c.D'");
	CHECK_MAIN("package { import /*!*/nowhere.*;\n"
			   "public class Main { var x:Thing; function f():void { other(); var y:/*!*/int.<int>; } } }",
			"can't find package 'nowhere'", "only Vector takes a type parameter");
	// Only the names the failed import could have supplied are excused.
	CHECK_MAIN("package { import /*!*/a.B; public class Main { var c:/*!*/C; function f():void { /*!*/g(); } } }",
			"can't find 'a.B'", "unknown type 'C'", "undefined name 'g'");
	// Imports outside the package block serve the helpers; those inside serve the block.
	CHECK_GODOT("package { public class Main { var n:/*!*/Node; } }\nimport godot.Node;\nclass Helper extends Node {}",
			"unknown type 'Node'");
	CHECK_GODOT("package { import godot.Node; public class Main {} }\nclass Helper extends /*!*/Node {}",
			"unknown type 'Node'");
	// An import in a class body.
	CHECK_GODOT("package { public class Main { import godot.Node; var n:Node; } }");
}

static void test_type_names() {
	CHECK_MAIN("package { public class Main {\n"
			   "var a:int, b:uint, c:Number, d:Boolean, e:String, f:Array, g:Object, h:*, i:Function, j:Class;\n"
			   "var k:Vector.<int>, l:Vector.<Vector.<String>>, m:Error, n:Main, o:Date, p:RegExp;\n"
			   "function q(a:int, ...rest):void {}\n} }");
	CHECK_MAIN("package { public class Main { var x:/*!*/Missing; } }", "unknown type 'Missing'");
	CHECK_MAIN("package { public class Main { function f(a:/*!*/Missing):/*!*/Gone {} } }", "unknown type 'Missing'",
			"unknown type 'Gone'");
	CHECK_MAIN("package { public class Main { function f():void { var x:/*!*/Missing; } } }",
			"unknown type 'Missing'");
	CHECK_MAIN("package { public class Main { function f():void {\n"
			   "try {} catch (e:/*!*/Missing) {}\n"
			   "var v:Vector.</*!*/Missing> = new </*!*/Missing>[];\n"
			   "var w:* = new Vector.</*!*/Missing>();\n"
			   "for each (var x:/*!*/Missing in v) {}\n"
			   "var g:Function = function(a:/*!*/Missing):/*!*/Missing { return a; };\n} } }",
			"unknown type 'Missing'", "unknown type 'Missing'", "unknown type 'Missing'", "unknown type 'Missing'",
			"unknown type 'Missing'", "unknown type 'Missing'", "unknown type 'Missing'");
	CHECK_MAIN("package { public class Main { var x:/*!*/trace; } }", "'trace' is not a type");
	CHECK_MAIN("package { public class Main { var x:/*!*/Array.<int>; } }", "only Vector takes a type parameter");
	CHECK_MAIN("package { public class Main { var x:/*!*/Main.<int>; } }", "only Vector takes a type parameter");
	// Named by package.
	CHECK_GODOT("package { import godot.Node; import flash.utils.*;\n"
				"public class Main { var a:godot.Node; var b:flash.utils.Dictionary; } }");
	CHECK_GODOT("package { import godot.*; public class Main { var a:/*!*/godot.Nope; var b:/*!*/nowhere.Node; } }",
			"unknown type 'godot.Nope'", "unknown type 'nowhere.Node'");
	CHECK_GODOT("package { import godot.*; public class Main { var a:/*!*/godot.Hidden; } }",
			"'godot.Hidden' is not public");
	// As in Adobe's compiler, a name written with its package still needs the import.
	CHECK_GODOT("package { public class Main { var a:/*!*/godot.Node; function f():void {\n"
				"new /*!*/godot.Node(); /*!*/flash.utils.getTimer(); } } }",
			"'godot.Node' must be imported before it is used", "'godot.Node' must be imported before it is used",
			"'flash.utils.getTimer' must be imported before it is used");
	CHECK_EQ(check_files({
					 { "lib/Thing.as", "package lib { public class Thing {} }" },
					 { "lib/User.as", "package lib { public class User { var t:lib.Thing; } }" },
			 }),
			std::string());

	// Ambiguity: nothing shadows anything at package level.
	CHECK_EQ(check_files({
					 { "a/Thing.as", "package a { public class Thing {} }" },
					 { "b/Thing.as", "package b { public class Thing {} }" },
					 { "Main.as", "package { import a.*; import b.*; public class Main { var x:Thing; } }" },
			 }),
			std::string("Main.as:1:61: error: 'Thing' is ambiguous: it could be a.Thing or b.Thing\n"));
	CHECK_EQ(check_files({
					 { "a/Object.as", "package a { public class Object {} }" },
					 { "Main.as", "package { import a.Object; public class Main { var x:Object; var y:a.Object; } }" },
			 }),
			std::string("Main.as:1:54: error: 'Object' is ambiguous: it could be Object or a.Object\n"));
	// The same definition reached two ways is not ambiguous.
	CHECK_EQ(check_files({
					 { "a/Thing.as", "package a { public class Thing {} }" },
					 { "Main.as", "package { import a.*; import a.Thing; public class Main { var x:Thing; } }" },
			 }),
			std::string());

	// Visibility.
	CHECK_EQ(check_files({
					 { "a/Inner.as", "package a { internal class Inner {} }" },
					 { "a/User.as", "package a { public class User { var x:Inner; } }\nclass Helper { var x:Inner; }" },
					 { "Main.as", "package { import a.*; public class Main { var x:Inner; } }" },
			 }),
			std::string("Main.as:1:49: error: unknown type 'Inner'\n"));
	CHECK_EQ(check_files({
					 { "A.as", "package { public class A { var h:Helper; } }\nclass Helper {}" },
					 { "Main.as", "package { public class Main { var h:Helper; } }" },
			 }),
			std::string("Main.as:1:37: error: unknown type 'Helper'\n"));
}

static void test_hierarchy() {
	CHECK_MAIN("package { public class Main extends /*!*/Missing {} }", "unknown type 'Missing'");
	CHECK_MAIN("package { public class Main extends Error implements IA, IB {} }\n"
			   "interface IA {}\ninterface IB extends IA {}");
	CHECK_MAIN("package { public class Main extends /*!*/IA {} }\ninterface IA {}",
			"a class can't extend the interface 'IA'; use 'implements'");
	CHECK_MAIN("package { public class Main extends /*!*/String {} }", "can't extend 'String': it is final");
	CHECK_MAIN("package { public class Main {} }\nfinal class A {}\nclass B extends /*!*/A {}",
			"can't extend 'A': it is final");
	CHECK_MAIN("package { public class Main implements /*!*/Error {} }", "'Error' is not an interface");
	CHECK_MAIN("package { public class Main {} }\ninterface IA extends /*!*/Main {}", "'Main' is not an interface");
	CHECK_MAIN("package { public class Main extends /*!*/trace {} }", "'trace' is not a type");

	// Cycles: one error each, and no hang.
	CHECK_MAIN("package { public class Main extends /*!*/Main {} }", "'Main' inherits from itself");
	CHECK_MAIN("package { public class Main extends /*!*/B {} }\nclass B extends Main {}",
			"'Main' inherits from itself");
	CHECK_MAIN("package { public class Main extends /*!*/B {} }\nclass B extends C {}\nclass C extends Main {}\n"
			   "class D extends C { function f():void { anything(); } }",
			"'Main' inherits from itself");
	CHECK_MAIN("package { public interface Main extends /*!*/Main {} }", "'Main' inherits from itself");
	CHECK_MAIN("package { public interface Main extends /*!*/IB {} }\ninterface IB extends IC {}\n"
			   "interface IC extends Main, ID {}\ninterface ID {}",
			"'Main' inherits from itself");

	// Extending a class from a declaration file.
	CHECK_GODOT("package { import godot.Node; import godot.Drawable;\n"
				"public class Main extends Node implements Drawable { public function draw():void {} } }");
	CHECK_GODOT("package { import godot.*; public class Main extends /*!*/Sealed {} }",
			"can't extend 'Sealed': it is final");
}

static void test_overrides() {
	const std::string head = "package { import godot.Node; public class Main extends Node {\n";
	CHECK_GODOT(head + "override public function add_child(node:Node):void {}\n} }");
	CHECK_GODOT(head + "public function /*!*/add_child(node:Node):void {}\n} }",
			"'add_child' overrides a method of Node and must be marked 'override'");
	CHECK_GODOT(head + "override public function /*!*/nothing():void {}\n} }",
			"'nothing' is marked 'override' but no base class has a method to override");
	CHECK_GODOT(head + "override public function /*!*/get_parent():Node { return null; }\n} }",
			"can't override 'get_parent': it is final in Node");
	CHECK_GODOT(head + "override public function /*!*/name():void {}\n} }",
			"can't override 'name': it is not a method in Node");
	CHECK_GODOT(head + "override public function /*!*/position():void {}\n} }",
			"can't override 'position': it is not a method in Node");
	CHECK_GODOT(head + "override public function get /*!*/add_child():int { return 0; }\n} }",
			"can't override 'add_child': it is not a method in Node");
	CHECK_GODOT(head + "public var /*!*/name:String;\n} }", "'name' conflicts with 'name' inherited from Node");
	CHECK_GODOT(head + "public var /*!*/add_child:int;\n} }", "'add_child' conflicts with 'add_child' inherited from Node");
	CHECK_GODOT(head + "public function /*!*/name():void {}\n} }", "'name' conflicts with 'name' inherited from Node");
	// Accessors override accessors, one at a time.
	CHECK_GODOT(head + "override public function get position():Number { return 0; }\n} }");
	CHECK_GODOT(head + "public function set /*!*/position(value:Number):void {}\n} }",
			"'position' overrides a method of Node and must be marked 'override'");
	// A private member of the base is invisible.
	CHECK_GODOT(head + "public var secret:int;\n} }");
	CHECK_GODOT(head + "override public function /*!*/secret():void {}\n} }",
			"'secret' is marked 'override' but no base class has a method to override");
	// Static members neither override nor conflict.
	CHECK_GODOT(head + "public static function make():Node { return null; }\n"
					   "public static function add_child():void {}\n} }");
	CHECK_GODOT(head + "public function make():void {}\n} }");
	// Through two levels.
	CHECK_GODOT("package { import godot.Node; public class Main extends Middle {\n"
				"public function /*!*/add_child(node:Node):void {}\n"
				"override public function own():void {}\n} }\n"
				"import godot.Node;\nclass Middle extends Node { public function own():void {} }",
			"'add_child' overrides a method of Node and must be marked 'override'");
	// A broken hierarchy silences these rules.
	CHECK_MAIN("package { public class Main extends /*!*/Missing { override public function f():void {} } }",
			"unknown type 'Missing'");
	// Object declares nothing, so toString needs no override.
	CHECK_MAIN("package { public class Main { public function toString():String { return \"\"; } } }");
	// Adding a setter beside an inherited getter is not an override.
	CHECK_MAIN("package { public class Main extends Base { public function set x(v:int):void {} } }\n"
			   "class Base { public function get x():int { return 0; } }");
}


// A method body inside `package { public class Main extends Base { ... } }`, with Base declared below.
static std::string in_method(const std::string &p_body) {
	return "package { public class Main extends Base {\n"
		   "public var field:int;\n"
		   "public static var shared:int;\n"
		   "public function method():void {}\n"
		   "public static function helper():void {}\n"
		   "public function run(param:int, ...rest):void {\n" +
			p_body +
			"\n}\n} }\n"
			"class Base {\n"
			"public var inherited:int;\n"
			"private var hidden:int;\n"
			"public static var base_shared:int;\n"
			"protected function base_method():void {}\n"
			"}\n"
			"function package_function():void {}\n"
			"var package_variable:int;\n";
}

static void test_bodies_resolve() {
	CHECK_MAIN(in_method(
			"var local:int = param + rest.length;\n"
			"early = 1; var early:int;\n"
			"if (local) { var nested:int = 2; }\n"
			"local = nested + early;\n"
			"for (var i:int = 0; i < 3; i++) { local += i; }\n"
			"for each (var item:* in rest) { local += item; }\n"
			"for (var key:String in rest) { trace(key); }\n"
			"try { later(); } catch (e:Error) { trace(e.message); var from_catch:int; }\n"
			"from_catch = 1;\n"
			"function later():void { local++; later(); }\n"
			"var closure:Function = function():int { return local + param + field; };\n"
			"var named:Function = function again(n:int):int { return n ? again(n - 1) : 0; };\n"
			"trace(arguments.length);\n"
			"field = inherited + shared + base_shared;\n"
			"method(); helper(); base_method(); package_function(); package_variable = 1;\n"
			"trace(Math.PI, NaN, undefined, Infinity, int(1.5), String(1), Main, Base);\n"
			"var o:Object = { field: 1, nothing: 2 };\n"
			"var v:Vector.<int> = new <int>[1, 2]; var w:* = new Vector.<int>(3);\n"
			"var r:RegExp = /a+/g; var a:Array = [1, , 2];\n"
			"this.field = 1; this.anything(); o.whatever.deeper = 2; o[local] = 3;\n"
			"label: for (;;) { while (true) { continue label; } break label; }\n"
			"switch (local) { case 1: break; default: break; }\n"));
	// Shadowing: the innermost wins, silently.
	CHECK_MAIN(in_method("var field:String; var Base:int; var trace:int; field = \"a\"; Base = trace;"));
	CHECK_MAIN(in_method("var f:Function = function(field:String, param:String):void { field = param; };"));
	// Field initializers and parameter defaults.
	CHECK_MAIN("package { public class Main {\n"
			   "static const LIMIT:int = 3;\n"
			   "var a:int = LIMIT; var b:int = a + 1;\n"
			   "function f(x:int = LIMIT, y:int = -1, z:Object = null):void {}\n} }");
	// Package-level code.
	CHECK_MAIN("package { public class Main {} }\nvar counter:int = start();\n"
			   "function start():int { return counter + 1; }\nfunction other():void { this; start(); }");
	// A catch variable ends with its block.
	CHECK_MAIN(in_method("try {} catch (e:Error) {}\ntrace(/*!*/e);"), "undefined name 'e'");
	// Dotted names.
	CHECK_GODOT("package { import flash.utils.*; import godot.*; public class Main { function f():void {\n"
				"var t:int = flash.utils.getTimer(); var n:* = new godot.Node(); godot.print(1);\n"
				"godot.Node.make().add_child(null); var d:* = new flash.utils.Dictionary(true);\n} } }");
	CHECK_GODOT("package { public class Main { function f():void { /*!*/godot.Nope.x(); /*!*/nowhere.Node; } } }",
			"undefined name 'godot'", "undefined name 'nowhere'");
	// A local named like a package wins over the package.
	CHECK_GODOT("package { public class Main { function f(godot:Object):void { godot.Node.anything(); } } }");
	// Imports inside a function body.
	CHECK_GODOT("package { public class Main { function f():void { import godot.Node; var n:Node = Node.make(); } } }");
	CHECK_GODOT("package { public class Main { function f():void { import godot.print; print(1); }\n"
				"function g():void { /*!*/print(2); } } }",
			"undefined name 'print'");
}

static void test_bodies_errors() {
	CHECK_MAIN(in_method("/*!*/missing = 1; trace(/*!*/gone, param);"), "undefined name 'missing'",
			"undefined name 'gone'");
	CHECK_MAIN("package { public class Main { var x:int = /*!*/missing; function f(a:int = /*!*/gone):void {} } }",
			"undefined name 'missing'", "undefined name 'gone'");
	// Not reported inside a class whose base failed to resolve.
	CHECK_MAIN("package { public class Main extends /*!*/Missing { function f():void { anything(); } } }",
			"unknown type 'Missing'");
	// Ambiguity.
	CHECK_EQ(check_files({
					 { "a/go.as", "package a { public function go():void {} }" },
					 { "b/go.as", "package b { public function go():void {} }" },
					 { "Main.as", "package { import a.*; import b.*; public class Main { function f():void { go(); } } }" },
			 }),
			std::string("Main.as:1:75: error: 'go' is ambiguous: it could be a.go or b.go\n"));
	// Private members of a base.
	CHECK_MAIN(in_method("/*!*/hidden = 1;"), "'hidden' is private in Base");
	// Static contexts.
	const std::string statics = "package { public class Main {\nvar field:int; static var shared:int;\n"
								"function method():void {}\n";
	CHECK_MAIN(statics + "static function f():void { shared = 1; /*!*/field = 1; /*!*/method(); }\n} }",
			"can't use the instance member 'field' in a static context",
			"can't use the instance member 'method' in a static context");
	CHECK_MAIN(statics + "static var copy:int = /*!*/field;\n} }",
			"can't use the instance member 'field' in a static context");
	CHECK_MAIN(statics + "static function f():void { var g:Function = function():void { /*!*/field = 1; /*!*/this; }; }\n} }",
			"can't use the instance member 'field' in a static context", "'this' can't be used in a static context");
	CHECK_MAIN(statics + "function f():void { var g:Function = function():void { field = 1; this.field = 2; }; }\n} }");
	CHECK_MAIN(statics + "static function f():void { /*!*/this.field = 1; /*!*/super.f(); }\n} }",
			"'this' can't be used in a static context", "'super' can only be used in an instance method");
	CHECK_MAIN("package { public class Main { function Main() { super(); } function f():void { super.f(); } } }\n"
			   "function g():void { /*!*/super.g(); }",
			"'super' can only be used in an instance method");
	// A local of the same name hides the instance member, so there is nothing to complain about.
	CHECK_MAIN(statics + "static function f(field:int):void { field = 1; }\n} }");

	// Declarations inside one function.
	CHECK_MAIN(in_method("var f:Function = function(a:int, /*!*/a:int):void {};"), "duplicate parameter 'a'");
	CHECK_MAIN(in_method("var x:int;\nvar x:int;\nfor (var i:int = 0;;) {}\nfor (var i:int = 0;;) {}\nvar u; var u:*;"));
	CHECK_MAIN(in_method("var x:int;\nvar /*!*/x:String;"),
			"'x' is already defined in this function with a different type (line 7)");
	CHECK_MAIN(in_method("var /*!*/param:String;"),
			"'param' is already defined in this function with a different type (line 6)");
	CHECK_MAIN(in_method("var param:int;"));
	CHECK_MAIN(in_method("var x:int;\nfunction /*!*/x():void {}"),
			"'x' is already defined in this function with a different type (line 7)");

	// Labels.
	CHECK_MAIN(in_method("/*!*/break;"), "'break' is only allowed in a loop or switch");
	CHECK_MAIN(in_method("/*!*/continue;"), "'continue' is only allowed in a loop");
	CHECK_MAIN(in_method("switch (param) { case 1: /*!*/continue; }"), "'continue' is only allowed in a loop");
	CHECK_MAIN(in_method("while (true) { break /*!*/nowhere; }"), "undefined label 'nowhere'");
	CHECK_MAIN(in_method("block: { while (true) { continue /*!*/block; } }"), "'block' does not label a loop");
	CHECK_MAIN(in_method("block: { if (param) break block; }"));
	CHECK_MAIN(in_method("outer: while (true) { var f:Function = function():void { break /*!*/outer; }; }"),
			"undefined label 'outer'");
	CHECK_MAIN(in_method("while (true) { var f:Function = function():void { /*!*/break; }; }"),
			"'break' is only allowed in a loop or switch");
	CHECK_MAIN(in_method("do { if (param) continue; else break; } while (true);"));

	// Assignment targets.
	CHECK_MAIN(in_method("/*!*/1 = param; /*!*/method() = 2; /*!*/param + 1 = 3; /*!*/method()++; --/*!*/this;\n"
						 "/*!*/[1] += 2;"),
			"can't assign to this expression", "can't assign to this expression", "can't assign to this expression",
			"can't assign to this expression", "can't assign to this expression", "can't assign to this expression");
	// The parser already rejects a `for in` target that can't be assigned.
	CHECK_MAIN(in_method("for (/*!*/method() in rest) {}"), "invalid left-hand side in for-in");
	CHECK_MAIN(in_method("param = 1; this.field = 2; rest[0] = 3; (param) = 4; (rest[0])++; ++this.field;\n"
						 "for (param in rest) {} for each (this.field in rest) {} param += 1;"));
}

static void test_bodies_limits() {
	// A long dotted chain with an unknown root: one error, no deep recursion.
	std::string chain = "/*!*/root";
	for (int i = 0; i < 440; i++) {
		chain += ".link";
	}
	CHECK_MAIN(in_method(chain + ";"), "undefined name 'root'");
	// A long inheritance chain.
	std::string classes = "package { public class Main extends C0 { function f():void { deep = 1; } } }\n";
	for (int i = 0; i < 3000; i++) {
		classes += "class C" + std::to_string(i) + " extends C" + std::to_string(i + 1) + " {}\n";
	}
	classes += "class C3000 { var deep:int; }\n";
	CHECK_MAIN(classes);
	// A trailing comma in call arguments is a syntax error.
	CHECK_MAIN(in_method("method(1,/*!*/);"), "expected an expression but found ')'");
}


static std::string names_of(std::initializer_list<File> p_files) {
	Compilation compilation;
	for (const File &file : p_files) {
		compilation.add(file.path, file.text, file.declaration);
	}
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(), std::string());
	return compilation.dump_names();
}

// True if some line of the dump ends with p_binding, as in "trace -> field Main.trace @2:5".
static bool binds(const std::string &p_dump, const std::string &p_binding) {
	bool found = p_dump.find(p_binding + " ") != std::string::npos || p_dump.find(p_binding + "\n") != std::string::npos;
	if (!found) {
		std::printf("  missing \"%s\" in:\n%s", p_binding.c_str(), p_dump.c_str());
	}
	return found;
}

// What a name binds to, not only that it binds.
static void test_bindings() {
	// The innermost wins: a local over a class and over a package-level name.
	std::string dump = names_of({ { "Main.as", in_method("var Base:int; Base = 1; var package_variable:String; "
															"package_variable = null;") } });
	CHECK(binds(dump, "Base -> local Base"));
	CHECK(binds(dump, "package_variable -> local package_variable"));
	// A class member over a package-level name.
	dump = names_of({ { "Main.as", "package { public class Main { var trace:int; static var counter:int;\n"
								   "function f():void { trace = counter; } } }\nvar counter:String;" } });
	CHECK(binds(dump, "trace -> field Main.trace"));
	CHECK(binds(dump, "counter -> field Main.counter"));
	// A parameter over a package path at the root of a dotted name.
	dump = names_of({ { "godot.as", GODOT_STUB, true },
			{ "Main.as", "package { import godot.*; public class Main { function f(godot:Object):void { godot.Node; } } }" } });
	CHECK(binds(dump, "godot -> parameter godot"));
	// The longest package prefix that has the definition.
	dump = names_of({
			{ "a/b.as", "package a { public var b:Object; }" },
			{ "a/b/C.as", "package a.b { public class C { public static var n:int; } }" },
			{ "Main.as", "package { import a.*; import a.b.*; public class Main { function f():void { a.b.C.n = 1; } } }" },
	});
	CHECK(binds(dump, "C -> class a.b.C"));
	// A static method does not see instance members: the name binds further out.
	dump = names_of({ { "Main.as", "package { import flash.utils.getTimer; public class Main {\n"
								   "public function getTimer():int { return 0; }\n"
								   "public static function now():int { return getTimer(); }\n"
								   "public function later():int { return getTimer(); } } }" } });
	CHECK(binds(dump, "3:43 getTimer -> function flash.utils.getTimer (built in)"));
	CHECK(binds(dump, "4:38 getTimer -> method Main.getTimer"));
	dump = names_of({ { "Main.as", "package { public class Main extends Mid { static function f():void { sbase = 1; } } }\n"
								   "class Mid extends Base { var sbase:String; }\nclass Base { static var sbase:int; }" } });
	CHECK(binds(dump, "sbase -> field Base.sbase"));
	// A user's class does not change what a declaration file's names mean.
	dump = names_of({
			{ "godot.as", "package godot { public native class Node { public native function add_child(n:Node):void; }\n"
						  "public native class Sprite extends Node {} }", true },
			{ "Node.as", "package { public class Node { public var next:Node; } }" },
			{ "Main.as", "package { import godot.Sprite; public class Main extends Sprite { var n:Node; } }" },
	});
	CHECK(binds(dump, "Node -> class Node @Node.as:1:24"));
	CHECK_EQ(check_files({
					 { "godot.as", "package godot { public native class Object {}\npublic native class Node extends Object {\n"
								   "public native function get_class():String; } }", true },
					 { "Main.as", "package { import godot.Node; public class Main extends Node {\n"
								  "function f():void { get_class(); } } }" },
			 }),
			std::string());
	CHECK_EQ(check_files({ { "flash/utils/Boolean.as", "package flash.utils { public class Boolean {} }" } }), std::string());

	// A setter added beside an inherited getter: the property has both halves.
	Compilation compilation;
	Unit *unit = compilation.add("Main.as", "package { public class Main extends Mid {\n"
											"public function set v(x:int):void {} function f():int { return v; } } }\n"
											"class Mid extends Base {}\nclass Base { public function get v():int { return 0; } }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(), std::string());
	const Symbol *property = nullptr;
	for (const Reference &reference : unit->references) {
		if (reference.name == "v") {
			property = reference.symbol;
		}
	}
	CHECK(property && property->getter && property->setter);
}

static void test_review_findings() {
	// An instance member at the root of a dotted name, in static code.
	const std::string statics = "package { public class Main {\nvar items:Array;\n";
	CHECK_MAIN(statics + "static function f():void { /*!*/items.length; }\nstatic var s:int = /*!*/items.length;\n} }",
			"can't use the instance member 'items' in a static context",
			"can't use the instance member 'items' in a static context");
	// `override` only belongs on instance methods.
	CHECK_MAIN("package { public class Main extends Base {\noverride static function /*!*/sf():void {}\n"
			   "override var /*!*/v:int;\n} }\nclass Base { static function sf():void {} }\n"
			   "override function /*!*/pf():void {}",
			"'override' can only be used on an instance method", "'override' can only be used on an instance method",
			"'override' can only be used on an instance method");
	// A named function expression's name is outside its body, so a local may reuse it.
	CHECK_MAIN(in_method("var g:Function = function f():void { var f:int; f = 1; };\n"
						 "var h:Function = function k():void { function k():void {} k(); };"));
	// Stacked labels.
	CHECK_MAIN(in_method("a: b: for (;;) { continue a; }"));
	CHECK_MAIN(in_method("a: b: { for (;;) { continue /*!*/a; } }"), "'a' does not label a loop");
	// Object's methods can be called by bare name, and defined without `override`.
	CHECK_MAIN("package { public class Main { function f():void { hasOwnProperty(\"x\"); toString(); valueOf(); } } }");
	CHECK_MAIN("package { public class Main extends Error { function f():String { return toString() + message; } } }");
	CHECK_MAIN("package { public class Main { override public function /*!*/toString():String { return \"\"; } } }",
			"'toString' is marked 'override' but no base class has a method to override");
	// Members of built-in classes are found by bare name in a subclass.
	CHECK_MAIN("package { public class Main extends Array { function top():* { return this[length - 1]; }\n"
			   "function add(x:*):void { push(x); } } }");
	CHECK_MAIN("package { import flash.utils.Dictionary; public class Main extends Dictionary {\n"
			   "function f():void { /*!*/anything(); toJSON(\"\"); } } }",
			"undefined name 'anything'");
	// Built-in members are exempt from the override rules.
	CHECK_MAIN("package { public class Main extends Array { public function push(x:*):void {} public var length:int; } }");
	// A base member that is internal to another package.
	CHECK_EQ(check_files({
					 { "a/Base.as", "package a { public class Base { internal var secret:int; var plain:int; public var open:int; } }" },
					 { "Main.as", "package { import a.Base; public class Main extends Base {\n"
								  "function f():void { open = 1; secret = 2; } } }" },
			 }),
			std::string("Main.as:2:31: error: 'secret' in Base is not visible from this package\n"));
	// Past the limit of 100, the errors kept are the first in the file.
	std::string many = "package { public class Main extends Base {\n";
	std::string base = "class Base {\n";
	for (int i = 0; i < 150; i++) {
		many += "public function m" + std::to_string(i) + "():void {}\n";
		base += "public function m" + std::to_string(i) + "():void {}\n";
	}
	std::string output = check_files({ { "Main.as", many + "} }\n" + base + "}\n" } });
	CHECK(output.find("Main.as:2:17: error: 'm0' overrides") == 0);
	CHECK(output.find("'m99' overrides") != std::string::npos);
	CHECK(output.find("'m100' overrides") == std::string::npos);
}

// Truncated files mostly fail to parse, but many prefixes are whole programs with missing pieces. None may
// crash or hang the checker, alone or beside the complete corpus.
static void test_corpus_prefixes() {
	std::string stub;
	CHECK(read_file(std::string(KAS3_TESTS_DIR) + "/stubs/godot.as", &stub));
	int files = 0;
	Compilation whole; // The complete corpus as one program: its files refer to each other.
	whole.add("godot.as", stub, true);
	for (const auto &entry : std::filesystem::directory_iterator(std::string(KAS3_TESTS_DIR) + "/corpus")) {
		if (entry.path().extension() != ".as") {
			continue;
		}
		std::string text;
		CHECK(read_file(entry.path().string(), &text));
		std::string name = entry.path().filename().string();
		files++;
		whole.add(name, text);
		for (size_t length = 0; length < text.size(); length++) {
			Compilation compilation;
			compilation.add("godot.as", stub, true);
			compilation.add(name, text.substr(0, length));
			compilation.check();
			check::checks()++;
		}
	}
	CHECK_EQ(files, 14);
	whole.check();
	CHECK_EQ(whole.format_diagnostics(), std::string());
}

int main() {
	test_model();
	test_declare();
	test_imports();
	test_type_names();
	test_hierarchy();
	test_overrides();
	test_bodies_resolve();
	test_bodies_errors();
	test_bodies_limits();
	test_bindings();
	test_review_findings();
	test_corpus_prefixes();
	return check::finish();
}
