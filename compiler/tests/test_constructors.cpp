// What a class's constructor chain depends on (ir::constructor_chain): whether objects of the class can be
// updated in place by a reload, why not, and which constants the chain reads.
#include "check.h"

#include "builtins.h"
#include "ir.h"
#include "lower.h"
#include "names.h"

#include <string>

using namespace kas3;

static ir::Module compiled(const std::string &p_source, bool p_godot = false) {
	ir::Module module;
	Compilation compilation;
	if (p_godot) {
		compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	}
	compilation.add("Game.as", p_source);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	return module;
}

static ir::ConstructorChain chain(const ir::Module &p_module, const char *p_class) {
	for (uint32_t i = 0; i < p_module.classes.size(); i++) {
		if (p_module.classes[i].name == p_class) {
			return ir::constructor_chain(p_module, i);
		}
	}
	std::printf("no class named %s\n", p_class);
	CHECK(false);
	return {};
}

static std::string joined(const std::vector<std::string> &p_names) {
	std::string out;
	for (const std::string &name : p_names) {
		out += name + ";";
	}
	return out;
}

// Why a class is refused: nothing, for one that is not.
static std::string refusal(const std::string &p_source, const char *p_class = "Game", bool p_godot = false) {
	return chain(compiled(p_source, p_godot), p_class).refusal.value_or("");
}

// A game's own shape: a constant of another class, read by a constructor that hands it up through two
// `super(...)` calls to the class that sets the fields.
static const char *const SHAPE =
		"package { public class Game { public var token:Token = new Token(100, 200); } }\n"
		"class Tuning { public static const TOKEN_W:Number = 24; public static const TOKEN_H:Number = 90;\n"
		"  public static const UNUSED:Number = 1; public static var plays:int = 0; }\n"
		"class Box { public var x:Number; public var y:Number; public var w:Number; public var h:Number;\n"
		"  public function Box(p_x:Number, p_y:Number, p_w:Number, p_h:Number) { x = p_x; this.y = p_y; w = p_w; h = p_h; }\n"
		"  public function centerX():Number { return x + w / 2; } }\n"
		"class Thing extends Box { public var active:Boolean = true;\n"
		"  public function Thing(p_x:Number, p_y:Number, p_w:Number, p_h:Number) { super(p_x, p_y, p_w, p_h); } }\n"
		"class Token extends Thing {\n"
		"  public function Token(sx:Number, sy:Number) { super(sx - Tuning.TOKEN_W / 2, sy - Tuning.TOKEN_H / 2, Tuning.TOKEN_W, Tuning.TOKEN_H); } }\n";

static void test_shape() {
	const ir::Module module = compiled(SHAPE);
	const ir::ConstructorChain token = chain(module, "Token");
	CHECK(!token.refusal);
	CHECK_EQ(joined(token.constants), std::string("Tuning.TOKEN_W;Tuning.TOKEN_H;"));
	const ir::ConstructorChain box = chain(module, "Box");
	CHECK(!box.refusal);
	CHECK_EQ(joined(box.constants), std::string());
	// The class that makes one in a field's first value is not one that can be made again itself.
	CHECK_EQ(chain(module, "Game").refusal.value_or(""), std::string("Game() makes an object"));
	// A class in a package: its constants are named with it, as the module's globals are.
	std::string packaged = SHAPE;
	packaged.replace(packaged.find("package {"), 9, "package game.play {");
	const ir::ConstructorChain in_package = chain(compiled(packaged), "game.play.Token");
	CHECK(!in_package.refusal);
	CHECK_EQ(joined(in_package.constants), std::string("game.play.Tuning.TOKEN_W;game.play.Tuning.TOKEN_H;"));
}

// What the chain's code is, to compare two builds by: the constructor, the fields' first values, the
// constructors above and what they call; nothing else of the class.
static void test_text() {
	const auto text = [](const char *p_from, const char *p_to) {
		std::string source = SHAPE;
		source.replace(source.find(p_from), std::string(p_from).size(), p_to);
		return chain(compiled(source), "Token").text;
	};
	const std::string same = chain(compiled(SHAPE), "Token").text;
	CHECK(!same.empty());
	CHECK(text("w = p_w;", "w = p_w * 2;") != same);
	CHECK(text("active:Boolean = true", "active:Boolean = false") != same);
	CHECK(text("sx - Tuning.TOKEN_W / 2", "sx - Tuning.TOKEN_W") != same);
	CHECK_EQ(text("return x + w / 2;", "return x + w;"), same);
	// A constant's value is not the chain's code: the reload compares the values themselves.
	CHECK_EQ(text("TOKEN_W:Number = 24", "TOKEN_W:Number = 30"), same);
}

static std::string game(const std::string &p_members, const std::string &p_rest = "") {
	return "package { public class Game { public var n:Number = 1;\n" + p_members + "\n} }\n" + p_rest;
}

static void test_allowed() {
	// Arithmetic, text, Math's functions of numbers, a branch, a loop, a default, a field read back.
	CHECK_EQ(refusal(game("public var label:String = \"a\"; public var big:Boolean; public var k:int; public var u:uint = 3;\n"
						  "public function Game(a:Number = 2, name:String = null) {\n"
						  "  n = Math.max(a, Math.sqrt(n)) + K; label = label + name + a.toFixed(1) + label.charAt(0);\n"
						  "  for (var i:int = 0; i < 3; i++) { k += i; } if (n > 3) { big = true; } }\n"
						  "public static const K:Number = 4;")),
			std::string());
	// Its own methods, an override among them, a getter and a static function of numbers.
	const std::string helpers = "public function Game() { setup(2); n = twice(size); }\n"
								"protected function setup(by:Number):void { n += by; }\n"
								"public function get size():Number { return n; }\n"
								"public static function twice(v:Number):Number { return v * 2; }";
	const std::string sub = "class Sub extends Game { public var m:Number; override protected function setup(by:Number):void { m = by; } }\n";
	CHECK_EQ(refusal(game(helpers, sub)), std::string());
	CHECK_EQ(refusal(game(helpers, sub), "Sub"), std::string());
	// A class with no constructor written, and one whose fields hold objects it never makes.
	CHECK_EQ(refusal(game("public var other:Game; public var any:*; public var text:String;")), std::string());
}

static void test_refused() {
	const std::string counter = "class Counter { public static var made:int = 0; public static const NAMES:Array = [1];\n"
								"  public static function next():int { return made + 1; } public var n:int;\n"
								"  public function bump():void { n++; } }\n";
	const auto why = [&](const char *p_members) { return refusal(game(p_members, counter)); };
	CHECK_EQ(why("public function Game() { n = Counter.made; }"), std::string("Game() reads the static variable Counter.made"));
	CHECK_EQ(why("public var k:int = Counter.made;"), std::string("Game() reads the static variable Counter.made"));
	CHECK_EQ(why("public function Game() { setup(); } private function setup():void { n = Counter.made; }"),
			std::string("Game/setup() reads the static variable Counter.made"));
	CHECK_EQ(why("public function Game() { n = Counter.next(); }"), std::string("Counter/next() reads the static variable Counter.made"));
	CHECK_EQ(why("public function Game() { Counter.made = 2; }"), std::string("Game() writes the static variable Counter.made"));
	CHECK_EQ(why("public function Game() { n = Counter.NAMES.length; }"),
			std::string("Game() reads the constant Counter.NAMES, which holds an object"));
	CHECK_EQ(why("public var held:Counter = new Counter();"), std::string("Game() makes an object"));
	CHECK_EQ(why("public var list:Array = [];"), std::string("Game() makes an object"));
	CHECK_EQ(why("public var list:Vector.<int> = new Vector.<int>();"), std::string("Game() makes an object"));
	CHECK_EQ(why("public var bag:Object = {};"), std::string("Game() makes an object"));
	CHECK_EQ(why("public var f:Function = function():void {};"), std::string("Game() makes an object"));
	CHECK_EQ(why("public function Game() { n = Math.random(); }"), std::string("Game() asks for a random number"));
	CHECK_EQ(why("public function Game() { n = new Date().getTime(); }"), std::string("Game() asks for the date"));
	CHECK_EQ(why("public function Game() { trace(n); }"), std::string("Game() prints"));
	CHECK_EQ(why("public function Game(c:Counter) { c.bump(); }"), std::string("Game() calls a method of another object"));
	CHECK_EQ(why("public function Game(c:Counter) { n = c.n; }"), std::string("Game() reads a field of another object"));
	CHECK_EQ(why("public function Game(c:Counter) { c.n = 2; }"), std::string("Game() writes a field of another object"));
	CHECK_EQ(why("public function Game(...rest) { n = 2; }"), std::string("Game() takes any number of arguments"));
	// The object itself must not leave the chain: what holds it could change it, or outlive the reload's copy.
	CHECK_EQ(why("public static var last:Game; public function Game() { last = this; }"), std::string("Game() hands the object on"));
	CHECK_EQ(why("public var me:Game; public function Game() { me = this; }"), std::string("Game() hands the object on"));
	CHECK_EQ(why("public function Game() { keep(this); } private static function keep(g:Game):void {}"), std::string("Game() hands the object on"));
	CHECK_EQ(why("public function Game() { setup(this); } private function setup(g:Game):void {}"), std::string("Game() hands the object on"));
	CHECK_EQ(why("public var any:*; public function Game() { any = this; }"), std::string("Game() hands the object on"));
	// Anything not known to be repeatable is refused, by the operation's name.
	CHECK_EQ(why("public var any:*; public function Game(v:*) { n = v.size; }"), std::string("Game() does what can't be done again (DynGet)"));
	// An override that a subclass's objects run decides for the subclass alone.
	const std::string sub = "class Sub extends Game { override protected function setup():void { n = Counter.made; } }\n";
	const std::string base = game("public function Game() { setup(); } protected function setup():void { n = 2; }", sub + counter);
	CHECK_EQ(refusal(base), std::string());
	CHECK_EQ(refusal(base, "Sub"), std::string("Sub/setup() reads the static variable Counter.made"));
	// A class's base decides for it too.
	CHECK_EQ(refusal(game("", "class Sub extends Game { public var held:Game = new Game(); }\nclass Leaf extends Sub {}\n"), "Leaf"),
			std::string("Sub() makes an object"));
}

static void test_not_a_script_class() {
	const std::string node = "package { import godot.*; public class Game extends Node2D { public var n:Number = 1; } }\n"
							 "class Sub extends Game {}\ninterface Doer { function act():void; }\n";
	CHECK_EQ(refusal(node, "Game", true), std::string("it extends Godot's Node2D"));
	CHECK_EQ(refusal(node, "Sub", true), std::string("it extends Godot's Node2D"));
	CHECK_EQ(refusal(node, "Doer", true), std::string("it is not a class with a constructor"));
	const ir::Module module = compiled(game("public var list:Vector.<int> = new Vector.<int>(); public var e:Error;"));
	CHECK_EQ(chain(module, "Vector.<int>").refusal.value_or(""), std::string("it is not a class with a constructor"));
	CHECK_EQ(chain(module, "Error").refusal.value_or(""), std::string("it is a class of the library"));
	CHECK_EQ(ir::constructor_chain(module, 9999).refusal.value_or(""), std::string("it is not a class with a constructor"));
}

// The constants are found whether or not the class can be updated: a reload says which class reads one that
// changed and is left as it was.
static void test_constants() {
	const std::string tuning = "class Tuning { public static const A:Number = 1; public static const B:String = \"b\"; public static const C:int = 3;\n"
							   "  public static const D:Number = 4; public static var v:Number = 5; }\n"
							   "class Helper { public var n:Number; public function Helper() { n = Tuning.D; } }\n";
	const ir::Module module = compiled(game("public var held:Helper = new Helper(); public var label:String = Tuning.B;\n"
											"public function Game() { n = Tuning.A + Tuning.A + pick(); }\n"
											"private function pick():Number { return Tuning.C; } public function later():Number { return Tuning.v; }",
			tuning));
	const ir::ConstructorChain found = chain(module, "Game");
	CHECK_EQ(found.refusal.value_or(""), std::string("Game() makes an object"));
	CHECK_EQ(joined(found.constants), std::string("Tuning.B;Tuning.A;Tuning.D;Tuning.C;"));
}

int main() {
	test_shape();
	test_text();
	test_allowed();
	test_refused();
	test_not_a_script_class();
	test_constants();
	return check::finish();
}
