// What the language server asks of the front end: checking through syntax errors, reusing a parse, and
// the queries on a checked program.
#include "check.h"

#include "ast_walk.h"
#include "builtins.h"
#include "names.h"
#include "query.h"

#include <filesystem>
#include <memory>

#include <string>
#include <vector>

using namespace kas3;

struct File {
	const char *path;
	std::string text;
	bool declaration = false;
};

static const Reference *reference(const Unit *p_unit, std::string_view p_name) {
	for (const Reference &item : p_unit->references) {
		if (item.name == p_name) {
			return &item;
		}
	}
	return nullptr;
}

static const char *const BROKEN =
		"package { import flash.utils.Dictionary; public class Main {\n"
		"var count:int;\n"
		"function f(other:Other):void { count = other.size; var d:Dictionary; if (count > }\n"
		"function g():void { count = \"text\"; }\n"
		"} }";
static const char *const OTHER = "package { public class Other { public var size:String; function h():void { size = 1; } } }";

static void test_tolerant() {
	// The plain check stops at a syntax error: nothing is resolved, and only that error is reported.
	Compilation plain;
	Unit *main = plain.add("Main.as", BROKEN);
	Unit *other = plain.add("Other.as", OTHER);
	plain.check();
	CHECK(main->references.empty());
	CHECK(other->references.empty());
	std::string expected = plain.format_diagnostics();
	CHECK_EQ(expected, std::string("Main.as:3:82: error: expected an expression but found '}'\n"));

	// The tolerant check resolves names in both files and reports the same.
	Compilation tolerant;
	main = tolerant.add("Main.as", BROKEN);
	other = tolerant.add("Other.as", OTHER);
	tolerant.check(true);
	CHECK_EQ(tolerant.format_diagnostics(), expected);
	const Reference *count = reference(main, "count");
	CHECK(count && count->symbol && count->symbol->kind == SymbolKind::Field);
	const Reference *type = reference(main, "Other");
	CHECK(type && type->symbol && type->symbol->unit == other);
	CHECK(reference(other, "size") != nullptr);
	CHECK(tolerant.has_errors());

	// With no syntax error the two are the same check.
	Compilation whole;
	whole.add("Other.as", OTHER);
	whole.check(true);
	CHECK_EQ(whole.format_diagnostics(),
			std::string("Other.as:1:83: error: can't use a value of type 'int' where 'String' is expected\n"));
}

static void test_import_symbol() {
	Compilation compilation;
	Unit *main = compilation.add("Main.as", BROKEN);
	compilation.check(true);
	Import *import = main->tree->package->imports[0];
	CHECK(import->symbol != nullptr);
	CHECK(import->symbol && import->symbol->name == "Dictionary");
	Compilation wildcard;
	main = wildcard.add("Main.as", "package { import flash.utils.*; import nowhere.Thing; public class Main {} }");
	wildcard.check();
	CHECK(main->tree->package->imports[0]->symbol == nullptr);
	CHECK(main->tree->package->imports[1]->symbol == nullptr);
}

// How many annotations the passes left on a tree.
static int annotations(Program *p_tree) {
	int count = 0;
	walk(p_tree, [&](Node *p_node) {
		if (is_expression(p_node->kind)) {
			count += static_cast<Expr *>(p_node)->type != nullptr;
		}
		switch (p_node->kind) {
			case NodeKind::TypeRef:
				count += static_cast<TypeRef *>(p_node)->symbol != nullptr;
				break;
			case NodeKind::Name:
				count += static_cast<Name *>(p_node)->symbol != nullptr;
				break;
			case NodeKind::Member:
				count += static_cast<Member *>(p_node)->symbol != nullptr;
				count += static_cast<Member *>(p_node)->member != nullptr;
				break;
			case NodeKind::VarDecl:
				for (const Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
					count += declarator.symbol != nullptr;
				}
				break;
			case NodeKind::FunctionDecl:
				count += static_cast<FunctionDecl *>(p_node)->symbol != nullptr;
				for (const Parameter &parameter : static_cast<FunctionDecl *>(p_node)->parameters) {
					count += parameter.symbol != nullptr;
				}
				break;
			case NodeKind::Try:
				for (const CatchClause &clause : static_cast<Try *>(p_node)->catches) {
					count += clause.symbol != nullptr;
				}
				break;
			case NodeKind::ClassDecl:
				count += static_cast<ClassDecl *>(p_node)->symbol != nullptr;
				break;
			case NodeKind::InterfaceDecl:
				count += static_cast<InterfaceDecl *>(p_node)->symbol != nullptr;
				break;
			case NodeKind::Import:
				count += static_cast<Import *>(p_node)->symbol != nullptr;
				break;
			default:
				break;
		}
	});
	return count;
}

struct Outcome {
	std::string diagnostics, names, types;
	bool operator==(const Outcome &p_other) const {
		return diagnostics == p_other.diagnostics && names == p_other.names && types == p_other.types;
	}
};
static std::ostream &operator<<(std::ostream &p_out, const Outcome &p_outcome) {
	return p_out << p_outcome.diagnostics << p_outcome.names.size() << " " << p_outcome.types.size();
}
static Outcome outcome(Compilation &p_compilation) {
	p_compilation.check(true);
	return { p_compilation.format_diagnostics(), p_compilation.dump_names(), p_compilation.dump_types() };
}

// A parsed file checked a second time gives what a fresh parse gives.
static void test_reuse() {
	std::vector<std::shared_ptr<Parsed>> builtins = parse_builtins();
	std::shared_ptr<Parsed> godot = parse_file(GODOT_DECLARATIONS_PATH, godot_declarations());
	std::vector<std::shared_ptr<Parsed>> corpus;
	for (const auto &entry : std::filesystem::directory_iterator(std::string(KAS3_TESTS_DIR) + "/corpus")) {
		std::string text;
		if (entry.path().extension() == ".as" && read_file(entry.path().string(), &text)) {
			corpus.push_back(parse_file(entry.path().filename().string(), text));
		}
	}
	CHECK_EQ(corpus.size(), (size_t)14);
	Outcome fresh;
	{
		Compilation compilation;
		compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
		for (const std::shared_ptr<Parsed> &file : corpus) {
			compilation.add(file->source.path(), file->source.text());
		}
		fresh = outcome(compilation);
		CHECK(fresh.names.size() > 1000);
		CHECK(fresh.types.size() > 1000);
	}
	for (int round = 0; round < 3; round++) {
		Compilation compilation(builtins);
		compilation.add(godot, true);
		for (const std::shared_ptr<Parsed> &file : corpus) {
			compilation.add(file);
		}
		CHECK_EQ(outcome(compilation), fresh);
	}
	// One file changes: the others keep their parse, and the result is that of a fresh check.
	std::string changed = corpus[0]->source.text() + "\nclass Extra { function f():void { var n:int = \"s\"; } }\n";
	Outcome fresh_changed;
	{
		Compilation compilation;
		compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
		compilation.add(corpus[0]->source.path(), changed);
		for (size_t i = 1; i < corpus.size(); i++) {
			compilation.add(corpus[i]->source.path(), corpus[i]->source.text());
		}
		fresh_changed = outcome(compilation);
		CHECK(fresh_changed.diagnostics.find("can't use a value of type 'String'") != std::string::npos);
	}
	{
		Compilation compilation(builtins);
		compilation.add(godot, true);
		compilation.add(parse_file(corpus[0]->source.path(), changed));
		for (size_t i = 1; i < corpus.size(); i++) {
			compilation.add(corpus[i]);
		}
		CHECK_EQ(outcome(compilation), fresh_changed);
	}
	// A name that resolved last time and does not this time is left unresolved, not pointing at the old
	// compilation's symbol.
	std::shared_ptr<Parsed> user = parse_file("A.as", "package { public class A { var b:B; function f():void { b.go(); } } }");
	auto resolved = [&]() {
		int count = 0;
		walk(user->tree, [&](Node *p_node) {
			count += p_node->kind == NodeKind::TypeRef && static_cast<TypeRef *>(p_node)->symbol != nullptr;
			count += p_node->kind == NodeKind::Member && static_cast<Member *>(p_node)->member != nullptr;
		});
		return count;
	};
	{
		Compilation compilation(builtins);
		compilation.add(user);
		compilation.add("B.as", "package { public class B { public function go():void {} } }");
		compilation.check(true);
		CHECK_EQ(resolved(), 2); // The type `B` and the member `go`.
	}
	{
		Compilation compilation(builtins);
		compilation.add(user);
		compilation.check(true);
		CHECK_EQ(resolved(), 0);
		CHECK(compilation.format_diagnostics().find("'B'") != std::string::npos);
	}
	// Every kind of annotation is wiped: here a name reached through its package, and a catch variable.
	std::shared_ptr<Parsed> odd = parse_file("Odd.as",
			"package { public class Odd { function f():int { try { return flash.utils.getTimer(); } catch (e:Error) {} return 0; } } }");
	auto marks = [&]() {
		int members = 0, catches = 0;
		walk(odd->tree, [&](Node *p_node) {
			members += p_node->kind == NodeKind::Member && static_cast<Member *>(p_node)->symbol != nullptr;
			if (p_node->kind == NodeKind::Try) {
				for (const CatchClause &clause : static_cast<Try *>(p_node)->catches) {
					catches += clause.symbol != nullptr;
				}
			}
		});
		return std::to_string(members) + " " + std::to_string(catches);
	};
	{
		Compilation compilation(builtins);
		compilation.add(odd);
		compilation.check(true);
		CHECK_EQ(compilation.format_diagnostics(), std::string());
		CHECK_EQ(marks(), std::string("1 1"));
	}
	clear_annotations(odd->tree);
	CHECK_EQ(marks(), std::string("0 0"));
	// The wipe leaves nothing behind.
	CHECK(annotations(corpus[1]->tree) > 10);
	CHECK(annotations(godot->tree) > 10000);
	clear_annotations(corpus[1]->tree);
	clear_annotations(godot->tree);
	CHECK_EQ(annotations(corpus[1]->tree), 0);
	CHECK_EQ(annotations(godot->tree), 0);
}

// The offset of the p_nth place (from 1) where p_needle is written.
static uint32_t offset_of(const Unit *p_unit, const char *p_needle, int p_nth = 1) {
	size_t found = std::string::npos;
	for (int i = 0; i < p_nth; i++) {
		found = p_unit->source.text().find(p_needle, found == std::string::npos ? 0 : found + 1);
		if (found == std::string::npos) {
			std::printf("NOT FOUND: %s #%d\n", p_needle, p_nth);
			check::failures()++;
			return 0;
		}
	}
	return (uint32_t)found;
}

static std::string place(const Place &p_place) {
	if (!p_place.unit) {
		return "nowhere";
	}
	Position at = p_place.unit->source.position(p_place.offset);
	return p_place.unit->source.path() + ":" + std::to_string(at.line) + ":" + std::to_string(at.column) + "+" +
			std::to_string(p_place.length);
}

// Where the name at the p_nth p_needle is declared.
static std::string definition(Queries &p_index, const Unit *p_unit, const char *p_needle, int p_nth = 1, uint32_t p_plus = 0) {
	const Occurrence *found = p_index.at(*p_unit, offset_of(p_unit, p_needle, p_nth) + p_plus);
	return found ? place(p_index.declaration(found->symbol)) : "nothing";
}

static const char *const PLAYER =
		"package game {\n" // 1
		"import godot.CharacterBody2D;\n" // 2
		"import godot.Vector2;\n" // 3
		"public class Player extends CharacterBody2D implements Living {\n" // 4
		"	public static const MAX:int = 3;\n" // 5
		"	private var speed:Number = 200;\n" // 6
		"	public var target:Enemy;\n" // 7
		"	public function Player(start:Number = 1) { speed = start; }\n" // 8
		"	public function get health():int { return 1; }\n" // 9
		"	public function set health(value:int):void {}\n" // 10
		"	override public function _physicsProcess(delta:Number):void {\n" // 11
		"		var step:Vector2 = velocity * delta;\n" // 12
		"		this.speed = step.length() + speed;\n" // 13
		"		target.hurt(MAX, \"hit\");\n" // 14
		"		moveAndSlide();\n" // 15
		"		health = Player.MAX + target.armor;\n" // 16
		"		try { helper(); } catch (problem:Error) { trace(problem); }\n" // 17
		"	}\n"
		"	public function alive(...rest):Boolean { return arguments.length > 0; }\n" // 19
		"}\n"
		"}\n"
		"function helper():void {}\n"; // 22
static const char *const ENEMY =
		"package game {\n"
		"public class Enemy extends Base {\n" // 2
		"	public function hurt(amount:int, why:String = \"x\"):void { armor -= amount; }\n" // 3
		"}\n"
		"}\n"
		"class Base { public var armor:int = 5; }\n"; // 6
static const char *const LIVING = "package game { public interface Living { function alive(...rest):Boolean; } }";

static void test_index() {
	Compilation compilation;
	Unit *godot = compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	Unit *player = compilation.add("game/Player.as", PLAYER);
	Unit *enemy = compilation.add("game/Enemy.as", ENEMY);
	Unit *living = compilation.add("game/Living.as", LIVING);
	compilation.check(true);
	CHECK_EQ(compilation.format_diagnostics(), std::string());
	Queries index(compilation);

	// Go to definition.
	CHECK_EQ(definition(index, player, "step.length"), std::string("game/Player.as:12:7+4")); // A local.
	CHECK_EQ(definition(index, player, "delta;"), std::string("game/Player.as:11:43+5")); // A parameter.
	CHECK_EQ(definition(index, player, "speed = start"), std::string("game/Player.as:6:14+5")); // A field.
	CHECK_EQ(definition(index, player, "speed = step"), std::string("game/Player.as:6:14+5")); // Through `this.`.
	CHECK_EQ(definition(index, player, "hurt"), std::string("game/Enemy.as:3:18+4")); // A method of another object.
	CHECK_EQ(definition(index, player, "armor"), std::string("game/Enemy.as:6:25+5")); // Inherited, from a helper class.
	CHECK_EQ(definition(index, player, "Enemy"), std::string("game/Enemy.as:2:14+5")); // A class in a type.
	CHECK_EQ(definition(index, player, "Living"), std::string("game/Living.as:1:33+6"));
	CHECK_EQ(definition(index, player, "MAX + "), std::string("game/Player.as:5:22+3")); // A static through its class.
	CHECK_EQ(definition(index, player, "Player.MAX"), std::string("game/Player.as:4:14+6"));
	CHECK_EQ(definition(index, player, "health = "), std::string("game/Player.as:9:22+6")); // A property: its getter.
	CHECK_EQ(definition(index, player, "helper();"), std::string("game/Player.as:22:10+6")); // A file helper.
	CHECK_EQ(definition(index, player, "problem);"), std::string("game/Player.as:17:28+7")); // A catch variable.
	CHECK_EQ(definition(index, player, "arguments"), std::string("nowhere"));
	// Godot's API: imports, a base class, an inherited property and method.
	std::string body = definition(index, player, "CharacterBody2D;");
	CHECK(body.rfind("<godot>/godot.as:", 0) == 0);
	CHECK_EQ(definition(index, player, "CharacterBody2D implements"), body);
	const Occurrence *move = index.at(*player, offset_of(player, "moveAndSlide"));
	CHECK(move && move->symbol->name == "moveAndSlide" && move->symbol->unit == godot);
	CHECK(move && !move->is_declaration && move->length == 12);
	const Occurrence *velocity = index.at(*player, offset_of(player, "velocity"));
	CHECK(velocity && velocity->symbol->owner && velocity->symbol->owner->name == "CharacterBody2D");
	const Occurrence *length = index.at(*player, offset_of(player, "length()"));
	CHECK(length && length->symbol->owner && length->symbol->owner->name == "Vector2");
	// A declaration is an occurrence of itself.
	const Occurrence *declared = index.at(*player, offset_of(player, "speed:Number"));
	CHECK(declared && declared->is_declaration && declared->symbol->kind == SymbolKind::Field);
	CHECK_EQ(definition(index, player, "set health", 1, 4), std::string("game/Player.as:9:22+6"));
	CHECK_EQ(definition(index, player, "_physicsProcess"), std::string("game/Player.as:11:27+15"));
	// The position right after the last letter still finds the name; one further does not.
	CHECK_EQ(definition(index, player, "hurt", 1, 4), std::string("game/Enemy.as:3:18+4"));
	CHECK_EQ(definition(index, player, "moveAndSlide", 1, 13), std::string("nothing"));
	// Whitespace, a keyword, a literal and the end of the file.
	CHECK_EQ(definition(index, player, "\tpublic static"), std::string("nothing"));
	CHECK_EQ(definition(index, player, "override"), std::string("nothing"));
	CHECK_EQ(definition(index, player, "\"hit\"", 1, 2), std::string("nothing"));
	CHECK(index.at(*player, (uint32_t)player->source.text().size()) == nullptr);
	CHECK(index.at(*player, 1u << 30) == nullptr);

	// Find references.
	auto places = [&](const Unit *p_unit, const char *p_needle, bool p_with_declaration) {
		const Occurrence *found = index.at(*p_unit, offset_of(p_unit, p_needle));
		std::string out;
		if (found) {
			for (const Place &item : index.references(found->symbol, p_with_declaration)) {
				out += place(item) + " ";
			}
		}
		return out;
	};
	CHECK_EQ(places(enemy, "armor -=", true),
			std::string("game/Player.as:16:32+5 game/Enemy.as:3:60+5 game/Enemy.as:6:25+5 "));
	CHECK_EQ(places(enemy, "armor -=", false), std::string("game/Player.as:16:32+5 game/Enemy.as:3:60+5 "));
	CHECK_EQ(places(player, "speed:Number", false),
			std::string("game/Player.as:8:45+5 game/Player.as:13:8+5 game/Player.as:13:32+5 "));
	// A Godot method: its uses in the program, and its declaration in Godot's file.
	std::string uses = places(player, "moveAndSlide", true);
	CHECK(uses.rfind("game/Player.as:15:3+12 <godot>/godot.as:", 0) == 0);
	// A property: both accessors are declarations.
	CHECK_EQ(places(player, "health = ", true),
			std::string("game/Player.as:9:22+6 game/Player.as:10:22+6 game/Player.as:16:3+6 "));
	(void)living;
}

// `super(...)` goes to the base class's constructor, and a method's references include the methods that
// override it and the one it overrides.
static void test_super_and_overrides() {
	Compilation compilation;
	Unit *unit = compilation.add("Zoo.as",
			"package { public class Zoo { function f(a:Animal, d:Dog, p:Puppy):void { a.speak(); d.speak(); p.speak(); a.other(); } } }\n" // 1
			"class Animal {\n" // 2
			"	public function Animal(name:String) {}\n" // 3
			"	public function speak():void {}\n" // 4
			"	public function other():void {}\n" // 5
			"}\n"
			"class Dog extends Animal {\n" // 7
			"	public function Dog() { super(\"dog\"); }\n" // 8
			"	override public function speak():void { super.speak(); }\n" // 9
			"}\n"
			"class Puppy extends Dog {\n" // 11
			"	override public function speak():void {}\n" // 12
			"}\n"
			"class Plain extends Bare { public function Plain() { super(); } }\n" // 14
			"class Bare {}\n" // 15
			"class Stranger { public function speak():void {} function g(s:Stranger):void { s.speak(); } }\n"); // 16
	compilation.check(true);
	CHECK_EQ(compilation.format_diagnostics(), std::string());
	Queries index(compilation);
	// The constructor the call runs; the class, where it declares none.
	CHECK_EQ(definition(index, unit, "super(\"dog\")", 1, 2), std::string("Zoo.as:3:18+6"));
	CHECK_EQ(definition(index, unit, "super();", 1, 1), std::string("Zoo.as:15:7+4"));
	auto places = [&](const char *p_needle, int p_nth, bool p_with_declaration) {
		const Occurrence *found = index.at(*unit, offset_of(unit, p_needle, p_nth));
		std::string out;
		if (found) {
			for (const Place &item : index.references(found->symbol, p_with_declaration)) {
				Position at = unit->source.position(item.offset);
				out += std::to_string(at.line) + ":" + std::to_string(at.column) + " ";
			}
		}
		return out;
	};
	// From any of the three: every call that may reach it, and with declarations all three of those.
	const std::string calls = "1:76 1:87 1:98 9:48 ";
	const std::string all = "1:76 1:87 1:98 4:18 9:27 9:48 12:27 ";
	CHECK_EQ(places("speak():void {}", 1, false), calls); // Animal's.
	CHECK_EQ(places("speak():void { super", 1, false), calls); // Dog's.
	CHECK_EQ(places("speak():void {}", 2, true), all); // Puppy's.
	CHECK_EQ(places("speak(); d.", 1, true), all);
	// A method of the same name in an unrelated class is another method.
	CHECK_EQ(places("speak():void {} function g", 1, true), std::string("16:34 16:82 "));
	CHECK_EQ(places("other():void", 1, false), std::string("1:109 "));
}

static void test_describe() {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	Unit *player = compilation.add("game/Player.as", PLAYER);
	Unit *enemy = compilation.add("game/Enemy.as", ENEMY);
	compilation.add("game/Living.as", LIVING);
	compilation.check(true);
	Queries index(compilation);
	auto describe = [&](const Unit *p_unit, const char *p_needle, int p_nth = 1) {
		const Occurrence *found = index.at(*p_unit, offset_of(p_unit, p_needle, p_nth));
		return found ? index.describe(found->symbol) + " | " + index.context(found->symbol) : std::string("nothing");
	};
	CHECK_EQ(describe(player, "Player extends"),
			std::string("class Player extends CharacterBody2D implements Living | class in package game"));
	CHECK_EQ(describe(player, "Living"), std::string("interface Living | interface in package game"));
	CHECK_EQ(describe(player, "MAX"), std::string("static const MAX:int = 3 | field of game.Player"));
	CHECK_EQ(describe(player, "speed"), std::string("var speed:Number | field of game.Player"));
	CHECK_EQ(describe(player, "hurt"),
			std::string("function hurt(amount:int, why:String = \"x\"):void | method of game.Enemy"));
	CHECK_EQ(describe(player, "health = "), std::string("function get health():int | property of game.Player"));
	CHECK_EQ(describe(player, "alive"), std::string("function alive(...rest):Boolean | method of game.Player"));
	CHECK_EQ(describe(player, "Player(start"), std::string("function Player(start:Number = 1) | constructor of game.Player"));
	CHECK_EQ(describe(player, "step.length"), std::string("var step:Vector2 | local variable"));
	CHECK_EQ(describe(player, "delta;"), std::string("delta:Number | parameter"));
	CHECK_EQ(describe(player, "problem);"), std::string("problem:Error | catch variable"));
	CHECK_EQ(describe(player, "arguments"), std::string("arguments:Array | the arguments of this call"));
	CHECK_EQ(describe(player, "helper();"), std::string("function helper():void | function in game/Player.as"));
	CHECK_EQ(describe(enemy, "Base"), std::string("class Base | class in game/Enemy.as"));
	CHECK_EQ(describe(player, "moveAndSlide"),
			std::string("function moveAndSlide():Boolean | method of godot.CharacterBody2D"));
	CHECK_EQ(describe(player, "trace"), std::string("function trace(...rest):void | function, built in"));
	CHECK_EQ(describe(player, "velocity"), std::string("function get velocity():Vector2 | property of godot.CharacterBody2D"));
}

static std::string outline_text(const std::vector<OutlineItem> &p_items, const Unit *p_unit, int p_depth = 0) {
	std::string out;
	for (const OutlineItem &item : p_items) {
		CHECK(!item.name.empty());
		CHECK(item.offset <= item.name_offset && item.name_offset + item.name_length <= item.end);
		CHECK(item.end <= p_unit->source.text().size());
		out += std::string((size_t)p_depth * 2, ' ') + symbol_kind_name(item.kind) + (item.is_const ? " const" : "") +
				(item.is_constructor ? " constructor" : "") + " " + item.name + "\n";
		out += outline_text(item.children, p_unit, p_depth + 1);
	}
	return out;
}

static void test_outline() {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	Unit *player = compilation.add("game/Player.as", PLAYER);
	Unit *enemy = compilation.add("game/Enemy.as", ENEMY);
	Unit *living = compilation.add("game/Living.as", LIVING);
	Unit *loose = compilation.add("loose.as", "package { public function loose(a:int):void {} }\nvar counter:int, other:int;");
	Unit *broken = compilation.add("Broken.as", "package { public class Broken { var a:int; function (");
	compilation.check(true);
	Queries index(compilation);
	CHECK_EQ(outline_text(index.outline(*player), player),
			std::string("class Player\n"
						"  field const MAX\n"
						"  field speed\n"
						"  field target\n"
						"  method constructor Player\n"
						"  property get health\n"
						"  property set health\n"
						"  method _physicsProcess\n"
						"  method alive\n"
						"function helper\n"));
	CHECK_EQ(outline_text(index.outline(*enemy), enemy), std::string("class Enemy\n  method hurt\nclass Base\n  field armor\n"));
	CHECK_EQ(outline_text(index.outline(*living), living), std::string("interface Living\n  method alive\n"));
	CHECK_EQ(outline_text(index.outline(*loose), loose), std::string("function loose\nvariable counter\nvariable other\n"));
	// A half-typed file: what has a name is listed, what has none is left out.
	CHECK_EQ(outline_text(index.outline(*broken), broken), std::string("class Broken\n  field a\n"));

	// Finding definitions by name.
	auto names = [&](const char *p_query, size_t p_limit) {
		std::string out;
		for (const FoundSymbol &found : index.find(p_query, p_limit)) {
			out += (found.container.empty() ? "" : found.container + ".") + found.name + " ";
			CHECK(found.unit && !found.unit->is_declaration);
		}
		return out;
	};
	CHECK_EQ(names("HEALTH", 10), std::string("game.Player.health ")); // Once, though it has a getter and a setter.
	CHECK_EQ(names("play", 10), std::string("game.Player game.Player.Player "));
	CHECK_EQ(names("a", 3), std::string("game.Player game.Player.MAX game.Player.target "));
	CHECK(index.find("", 1000).size() > 15);
	CHECK_EQ(names("Node", 10), std::string()); // Godot's own classes are not listed.
}

// Every query answers on files that do not parse.
static void test_queries_on_broken_files() {
	const std::string text = PLAYER;
	for (size_t length = 0; length <= text.size(); length += 7) {
		Compilation compilation;
		Unit *player = compilation.add("game/Player.as", text.substr(0, length));
		Unit *enemy = compilation.add("game/Enemy.as", ENEMY);
		compilation.check(true);
		Queries index(compilation);
		for (uint32_t offset = 0; offset <= length; offset += 3) {
			const Occurrence *found = index.at(*player, offset);
			if (found) {
				index.declaration(found->symbol);
				index.describe(found->symbol);
				index.context(found->symbol);
				index.references(found->symbol, true);
			}
			check::checks()++;
		}
		index.outline(*player);
		index.outline(*enemy);
		index.find("e", 50);
	}
}

// Every prefix of every corpus file, and every file with one byte missing: the tolerant check and the
// queries must get through all of them. With Godot's API for some.
static void test_typing() {
	std::vector<std::shared_ptr<Parsed>> builtins = parse_builtins();
	std::shared_ptr<Parsed> godot = parse_file(GODOT_DECLARATIONS_PATH, godot_declarations());
	int files = 0;
	for (const auto &entry : std::filesystem::directory_iterator(std::string(KAS3_TESTS_DIR) + "/corpus")) {
		std::string text;
		if (entry.path().extension() != ".as" || !read_file(entry.path().string(), &text)) {
			continue;
		}
		files++;
		std::string name = entry.path().filename().string();
		for (size_t i = 0; i < text.size() * 2; i++) {
			size_t at = i / 2;
			std::string edited = i % 2 == 0 ? text.substr(0, at) : text.substr(0, at) + text.substr(at + 1);
			Compilation compilation(builtins);
			if (i % 211 == 0) {
				compilation.add(godot, true);
			}
			Unit *unit = compilation.add(name, edited);
			compilation.check(true);
			Queries queries(compilation);
			for (uint32_t back = 0; back < 4 && back <= at; back++) {
				const Occurrence *found = queries.at(*unit, (uint32_t)at - back);
				if (found) {
					queries.declaration(found->symbol);
					queries.describe(found->symbol);
					queries.context(found->symbol);
					queries.references(found->symbol, true);
				}
			}
			if (i % 16 == 0) {
				queries.outline(*unit);
				queries.find("e", 20);
			}
			check::checks()++;
		}
	}
	CHECK_EQ(files, 14);
}

int main() {
	test_typing();
	test_index();
	test_super_and_overrides();
	test_describe();
	test_outline();
	test_queries_on_broken_files();
	test_tolerant();
	test_import_symbol();
	test_reuse();
	return check::finish();
}
