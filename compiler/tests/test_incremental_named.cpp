// Named tests of incremental checking: what an edit makes the workspace check again, and sequences that
// random edits seldom reach. After each step the workspace must say what a fresh one says of the same text
// (test_incremental does the same after random edits).
#include "check.h"
#include "scratch.h"
#include "oracle_answers.h"
#include "oracle_project.h"

#include "ast_walk.h"
#include "workspace.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace kas3;
namespace fs = std::filesystem;
using oracle::answers;
using oracle::first_difference;
using oracle::options;
using oracle::program_state;

namespace {

// A ring of classes, each holding the next and calling it, and a hub that every one of them calls.
oracle::Files ring(int p_count) {
	oracle::Files files;
	files["/project/game/Hub.as"] = "package game {\n\tpublic class Hub {\n\t\tpublic function ping(n:int):int { return n; }\n\t}\n}\n";
	for (int i = 0; i < p_count; ++i) {
		const std::string name = "Thing" + std::to_string(i);
		const std::string next = "Thing" + std::to_string((i + 1) % p_count);
		files["/project/game/" + name + ".as"] = "package game {\n\tpublic class " + name + " {\n\t\tprivate var next:" + next +
				";\n\t\tprivate var hub:Hub;\n\t\tpublic function step(delta:Number):Number {\n\t\t\tvar total:Number = delta;\n"
				"\t\t\tif (next != null) { total += next.step(delta); }\n\t\t\treturn total + hub.ping(1);\n\t\t}\n\t}\n}\n";
	}
	return files;
}

// What an edit makes the workspace check again: the files whose declarations, and whose bodies, it checks.
struct Work {
	size_t declarations;
	size_t bodies;
	bool operator==(const Work &p_other) const { return declarations == p_other.declarations && bodies == p_other.bodies; }
};

std::ostream &operator<<(std::ostream &p_out, const Work &p_work) {
	return p_out << p_work.declarations << " declarations, " << p_work.bodies << " bodies";
}

Work work_of(Workspace &p_workspace, const std::string &p_path, const std::string &p_text) {
	const Compilation::Counts before = p_workspace.compilation().counts();
	p_workspace.change(p_path, p_text);
	const Compilation::Counts after = p_workspace.compilation().counts();
	return { after.declarations - before.declarations, after.bodies - before.bodies };
}

std::string replaced(std::string p_text, const std::string &p_from, const std::string &p_to) {
	const size_t at = p_text.find(p_from);
	CHECK(at != std::string::npos);
	return at == std::string::npos ? p_text : p_text.replace(at, p_from.size(), p_to);
}

// An edit checks again the file it is in, and only the files that read what it changed.
void test_scope() {
	oracle::Files files = ring(20);
	Workspace workspace(options(false));
	for (const auto &[path, text] : files) {
		workspace.open(path, text);
	}
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	// Each edit, and then its undo, checks the same files again.
	const auto both_ways = [&](const std::string &p_path, const std::string &p_from, const std::string &p_to, Work p_work) {
		const std::string &text = files[p_path];
		CHECK_EQ(work_of(workspace, p_path, replaced(text, p_from, p_to)), p_work);
		CHECK_EQ(work_of(workspace, p_path, text), p_work);
	};
	const std::string thing = "/project/game/Thing5.as";
	const std::string hub = "/project/game/Hub.as";
	// A body: that file alone.
	both_ways(thing, "var total:Number = delta;", "var total:Number = delta * 2;", { 1, 1 });
	// A method's signature: the file, and the bodies of Thing4, which calls it.
	both_ways(thing, "step(delta:Number)", "step(delta:Number, extra:int = 0)", { 1, 2 });
	// A new method nobody calls: the file alone.
	both_ways(thing, "\t}\n}", "\t\tpublic function extra():void {}\n\t}\n}", { 1, 1 });
	// A syntax error in a body changes no declaration: the file alone. The others are only judged again.
	both_ways(thing, "var total:Number = delta;", "var total:Number = ;", { 1, 1 });
	// The hub's method, which every file calls: the bodies of all of them.
	both_ways(hub, "ping(n:int):int", "ping(n:Number):int", { 1, 21 });
	// A new member of the hub: the hub alone.
	both_ways(hub, "\t}\n}", "\t\tpublic var count:int;\n\t}\n}", { 1, 1 });
	CHECK_EQ(workspace.full_checks(), 1);
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
}

// A named sequence for what random edits seldom reach. After each step the workspace, asked about p_asked
// first (only that file checked), then about everything, says what a fresh workspace says.
void directed(const char *p_name, bool p_godot, oracle::Files p_files,
		const std::vector<std::pair<std::string, std::string>> &p_steps, const std::string &p_asked, const char *p_marker,
		bool p_tagged = false) {
	Workspace workspace(options(p_godot, p_tagged));
	for (const auto &[path, text] : p_files) {
		workspace.open(path, text);
	}
	workspace.compilation();
	int step = 0;
	const int rebuilds = 1; // The first check only: a file that joins or leaves the program is an edit.
	for (const auto &[path, text] : p_steps) {
		++step;
		if (text.empty()) {
			workspace.close(path);
			p_files.erase(path);
		} else {
			workspace.open(path, text);
			p_files[path] = text;
		}
		Workspace reference(options(p_godot, p_tagged));
		for (const auto &[file, written] : p_files) {
			reference.open(file, written);
		}
		reference.compilation();
		const std::string &asked = p_files[p_asked];
		const size_t at = asked.find(p_marker);
		const std::vector<uint32_t> offsets = { (uint32_t)(at == std::string::npos ? 0 : at + std::strlen(p_marker)) };
		const std::string got = answers(workspace, p_asked, offsets);
		const std::string wanted = answers(reference, p_asked, offsets);
		const std::string state = program_state(workspace);
		const std::string expected = program_state(reference);
		CHECK(got == wanted && state == expected);
		if (got != wanted || state != expected) {
			std::printf("directed %s, step %d: %s\n", p_name, step,
					got != wanted ? first_difference(got, wanted).c_str() : first_difference(state, expected).c_str());
		}
	}
	CHECK_EQ(workspace.full_checks(), rebuilds);
}

void test_directed() {
	const auto file = [](const std::string &p_package, const std::string &p_body) {
		return "package " + p_package + (p_package.empty() ? "" : " ") + "{\n" + p_body + "\n}\n";
	};
	// Override completion writes a base class's default values, which the base file's bodies resolve.
	directed("override defaults", false,
			{ { "/p/Base.as", file("", "public class Base { public static const VALUE:int = 3;\n"
									   "public function go(n:int = Base.VALUE, m:int = VALUE):void {} }") },
					{ "/p/Main.as", file("", "public class Main extends Base { override function \n}") } },
			{ { "/p/Base.as", file("", "public class Base { public static const VALUE:int = 4;\n"
									   "public function go(n:int = Base.VALUE, m:int = VALUE):void {} }") },
					{ "/p/Main.as", file("", "public class Main extends Base { override function g\n}") } },
			"/p/Main.as", "override function ");
	// An earlier interface comes to extend one in a cycle: where the cycle is cut depends on it.
	directed("interface cycle", false,
			{ { "/p/A.as", file("", "public interface A {}") }, { "/p/B.as", file("", "public interface B extends C {}") },
					{ "/p/C.as", file("", "public interface C extends B {}") } },
			{ { "/p/A.as", file("", "public interface A extends C {}") }, { "/p/A.as", file("", "public interface A extends B {}") },
					{ "/p/C.as", file("", "public interface C {}") }, { "/p/A.as", file("", "public interface A extends A {}") } },
			"/p/A.as", "interface ");
	// The first file's definition of a name wins; another file defining it says so, until it is first.
	directed("first definition", false,
			{ { "/p/b/One.as", file("x", "public class One {}") }, { "/p/c/One.as", file("x", "public class One {}") },
					{ "/p/User.as", file("", "import x.*;\npublic class User { var one:One; }") } },
			{ { "/p/a/One.as", file("x", "public class One { public var v:int; }") }, { "/p/b/One.as", file("x", "public class Two {}") },
					{ "/p/a/One.as", file("x", "public class One {") }, { "/p/a/One.as", file("x", "public class Three {}") } },
			"/p/User.as", "one:");
	// The halves of a property overridden along a chain, each in another class: the getter's override added to
	// the class in the middle is not an error in the class below, in whatever order the files are checked.
	directed("halves along a chain", false,
			{ { "/p/A_D.as", file("", "public class A_D extends M { override public function set x(v:int):void { } }") },
					{ "/p/B.as", file("", "public class B { public function get x():int { return 1; } public function set x(v:int):void { } }") },
					{ "/p/M.as", file("", "public class M extends B { }") } },
			{ { "/p/M.as", file("", "public class M extends B { override public function get x():int { return 2; } }") },
					{ "/p/B.as", file("", "public class B { public function get x():int { return 1; } }") },
					{ "/p/M.as", file("", "public class M extends B { }") },
					{ "/p/B.as", file("", "public class B { public function get x():int { return 1; } public function set x(v:int):void { } }") },
					{ "/p/M.as", file("", "public class M extends B { override public function get x():int { return 2; } }") } },
			"/p/A_D.as", "set ");
	// A setter inherited across files points into the base file's tree: a body edit there moves it.
	directed("inherited half", false,
			{ { "/p/Base.as", file("", "public class Base { public function get v():int { return 1; } public function set v(x:int):void {} }") },
					{ "/p/Sub.as", file("", "public class Sub extends Base { override public function get v():int { return 2; }\n"
											"function f():void { v = 3; v; } }") } },
			{ { "/p/Base.as", file("", "public class Base { public function get v():int { return 1; } public function set v(x:int):void { x++; } }") },
					{ "/p/Base.as", file("", "public class Base { public function get v():int { return 1; } }") },
					{ "/p/Base.as", file("", "public class Base { public function get v():int { return 1; } public function set v(x:Number):void {} }") } },
			"/p/Sub.as", "v = ");
	// More than a hundred errors in one file, and edits around them.
	std::string many = "public class Many {\n";
	for (int i = 0; i < 130; ++i) {
		many += "var f" + std::to_string(i) + ":Nope" + std::to_string(i % 3) + ";\n";
	}
	directed("many errors", false, { { "/p/Many.as", file("", many + "}") }, { "/p/Nope0.as", file("", "public class Nope1 {}") } },
			{ { "/p/Nope0.as", file("", "public class Nope0 {}") }, { "/p/Many.as", file("", "var early:Nope2;\n" + many + "}") },
					{ "/p/Nope0.as", file("", "public class Nope1 {}") } },
			"/p/Many.as", "var f1");
	// Two files that do not parse, then one repaired: what the other's errors make doubtful changes.
	directed("two broken", false,
			{ { "/p/A.as", file("", "public class A { public function f(t:T):void {} }") },
					{ "/p/T.as", file("", "public class T extends B {}") }, { "/p/B.as", file("", "public class B { public var x:int; }") },
					{ "/p/U.as", file("", "public class U { function g(a:A):void { a.f(null); a.h(); missing(); } }") } },
			{ { "/p/B.as", file("", "public class B { public var x:int; ") }, { "/p/A.as", file("", "public class A { public function f(t:T):void { ") },
					{ "/p/T.as", file("", "public class T {}") }, { "/p/B.as", file("", "public class B { public var x:int; }") },
					{ "/p/A.as", file("", "public class A { public function f(t:T):void {} }") } },
			"/p/U.as", "a.");
	// An import inside a local function is the bodies'; once it finds what it names, a broken file elsewhere
	// takes back only the unresolved errors, not all of them.
	directed("local import", false,
			{ { "/p/C.as", file("", "public class C { var x:int = \"s\"; function f():void { function g():void { import p.Missing; } } }") },
					{ "/p/p/Missin.as", file("p", "public class Missin {}") }, { "/p/B.as", file("", "public class B {") } },
			{ { "/p/p/Missin.as", file("p", "public class Missing {}") }, { "/p/p/Missin.as", file("p", "public class Missin {}") } },
			"/p/C.as", "x:");
	// A type argument in a field's type is looked up too: another file defining the name makes it ambiguous.
	directed("type argument", false,
			{ { "/p/A.as", file("", "public class A { var v:Vector.<P> = new Vector.<P>(); }") + "class P {}\n" },
					{ "/p/Q.as", file("", "public class Q {}") } },
			{ { "/p/Q.as", file("", "public class P {}") }, { "/p/Q.as", file("", "public class Q {}") } },
			"/p/A.as", "v:");
	// A declaration file's tag names a type no declaration file has. A class of the program by that name is
	// not what it means, when it joins or when it leaves: a declaration file sees declarations only.
	directed("tag and program class", false,
			{ { "/p/A.as", file("", "import api.V2;\npublic class A { function f(a:V2, b:V2):V2 { return a - b; } }") } },
			{ { "/p/Nope.as", file("", "public class Nope {}") }, { "/p/api/Nope.as", file("api", "public class Nope {}") },
					{ "/p/Nope.as", "" }, { "/p/api/Nope.as", "" } },
			"/p/A.as", "a - ", true);
	// Godot's API loaded, and a file that joins and leaves: Godot's declarations are never checked again.
	directed("godot", true, { { "/p/Mover.as", file("", "import godot.*;\npublic class Mover extends Node2D { function f():void { position.x = 1; } }") } },
			{ { "/p/Mover.as", file("", "import godot.*;\npublic class Mover extends Sprite2D { function f():void { position.x = 1; } }") },
					{ "/p/Mover.as", file("", "import godot.*;\npublic class Mover extends Sprite2D { function f():void { position. } }") } },
			"/p/Mover.as", "position.");
}

// A mistake in a declaration file's tag is said once, in that file, however many files use the class and
// however often their bodies are checked again.
void test_declaration_tags() {
	const fs::path folder = check::fresh_folder("tags");
	const std::string api = (folder / "api.as").string();
	std::ofstream(api, std::ios::binary) << "package api {\n\t[Operators]\n\t[Operator(op=\"+\", right=\"V2\", returns=\"V2\")]\n"
											"\t[Operator(op=\"+\", right=\"V2\", returns=\"V2\")]\n\t[Operator(op=\"-\", right=\"Nope\", returns=\"V2\")]\n"
											"\tpublic class V2 {\n\t\tpublic var x:Number;\n\t\tpublic native function V2(x:Number = 0);\n\t}\n}\n";
	Workspace::Options with = options(false);
	with.declarations.push_back(api);
	const std::string a = "package { import api.V2; public class A { public function f(a:V2, b:V2):V2 { return a + b; } } }";
	const std::string b = "package { import api.V2; public class B { public function g(a:V2, b:V2):V2 { return a + b; } } }";
	Workspace workspace(with);
	workspace.open("/p/A.as", a);
	workspace.open("/p/B.as", b);
	const std::string said = workspace.compilation().format_diagnostics();
	CHECK_EQ(workspace.unit(api)->diagnostics.count(), 2);
	for (int i = 0; i < 8; ++i) {
		workspace.change("/p/A.as", replaced(a, "return a + b;", "var q:int = " + std::to_string(i) + "; return a + b;"));
		CHECK_EQ(workspace.compilation().format_diagnostics(), said);
	}
	// And the same whether two files use the class, or none.
	Workspace alone(with);
	alone.open("/p/C.as", "package { public class C {} }");
	alone.compilation();
	CHECK_EQ(alone.unit(api)->diagnostics.count(), 2);
}

// A long chain of classes: an edit to the root reaches every class, one after another, and is still an
// edit, not a rebuild.
void test_deep_chain() {
	for (int depth : { 70, 1000 }) {
		oracle::Files files;
		const auto name = [](int p_index) { return "C" + std::to_string(p_index); };
		const std::string root = "package { public class C0 { public function m():int { return 1; } } }";
		files["/p/C0.as"] = root;
		for (int i = 1; i < depth; ++i) {
			files["/p/" + name(i) + ".as"] = "package { public class " + name(i) + " extends " + name(i - 1) + " { } }";
		}
		files["/p/Z.as"] = "package { public class Z extends " + name(depth - 1) + " { override public function m():int { return 2; } } }";
		Workspace workspace(options(false));
		for (const auto &[path, text] : files) {
			workspace.open(path, text);
		}
		CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
		for (const std::string &text : { replaced(root, "m():int { return 1; }", "m():String { return ''; }"), root }) {
			workspace.change("/p/C0.as", text);
			files["/p/C0.as"] = text;
			Workspace reference(options(false));
			for (const auto &[path, written] : files) {
				reference.open(path, written);
			}
			CHECK(program_state(workspace) == program_state(reference));
		}
		CHECK_EQ(workspace.full_checks(), 1);
	}
}

// A chain of 40 files, each made from its number by p_link (the root from 0), and a file that names every
// one of them in its signatures and uses each in a body: it reads every class's chain and members. An edit to
// the root reaches it once, with the whole chain, and is still an edit.
void reader_of_a_chain(const char *p_name, const std::function<std::string(int)> &p_link, const std::string &p_use,
		const std::string &p_from, const std::string &p_to) {
	oracle::Files files;
	files["/p/Marked.as"] = "package { public interface Marked { } }";
	std::string fields = "package { public class Fields {\n";
	for (int i = 0; i < 40; ++i) {
		const std::string number = std::to_string(i);
		files["/p/C" + number + ".as"] = p_link(i);
		fields += "\tvar f" + number + ":C" + number + ";\n\tfunction g" + number + "(m:Marked):void { " +
				replaced(p_use, "$", "f" + number) + " }\n";
	}
	files["/p/Fields.as"] = fields + "} }";
	Workspace workspace(options(false));
	for (const auto &[path, text] : files) {
		workspace.open(path, text);
	}
	workspace.compilation();
	const std::string root = files["/p/C0.as"];
	for (const std::string &text : { replaced(root, p_from, p_to), root }) {
		workspace.change("/p/C0.as", text);
		files["/p/C0.as"] = text;
		Workspace reference(options(false));
		for (const auto &[path, written] : files) {
			reference.open(path, written);
		}
		const std::string got = program_state(workspace);
		const std::string wanted = program_state(reference);
		CHECK(got == wanted);
		if (got != wanted) {
			std::printf("reader of a chain, %s: %s\n", p_name, first_difference(got, wanted).c_str());
		}
	}
	CHECK_EQ(workspace.full_checks(), 1);
}

void test_reader_of_a_chain() {
	const auto below = [](int p_i, const char *p_word) {
		return p_i ? std::string(p_word) + " C" + std::to_string(p_i - 1) + " " : std::string();
	};
	// The root's header: every class's chain changes.
	reader_of_a_chain(
			"header", [&](int p_i) { return "package { public class C" + std::to_string(p_i) + " " + below(p_i, "extends") + "{ } }"; },
			"trace($);", "class C0 {", "class C0 implements Marked {");
	// A member every class overrides: each override is checked against the one above, down the chain, and
	// the reader calls every one of them.
	reader_of_a_chain(
			"member",
			[&](int p_i) {
				return "package { public class C" + std::to_string(p_i) + " " + below(p_i, "extends") + "{ " + (p_i ? "override " : "") +
						"public function m(a:int):int { return a; } } }";
			},
			"trace($.m(1));", "m(a:int):int", "m(a:int, b:int = 0):int");
	// A chain of interfaces, and a member of the root that every one of them inherits.
	reader_of_a_chain(
			"interfaces",
			[&](int p_i) {
				return "package { public interface C" + std::to_string(p_i) + " " + below(p_i, "extends") + "{ " +
						(p_i ? "" : "function m(a:int):int;") + " } }";
			},
			"trace($.m(1));", "m(a:int):int", "m(a:int, b:int = 0):int");
}

// A package that nothing declares any more is forgotten: a file moved from package to package for a whole
// session leaves no trace of where it was.
void test_packages_released() {
	Workspace workspace(options(false));
	workspace.open("/p/User.as", "package { import game.*; public class User { var m:Mover; } }");
	const std::string mover = "package game { public class Mover {} }";
	workspace.open("/p/Mover.as", mover);
	const size_t packages = workspace.compilation().counts().packages;
	for (int i = 0; i < 200; ++i) {
		workspace.change("/p/Mover.as", replaced(mover, "package game", "package game.p" + std::to_string(i)));
		workspace.compilation();
		workspace.change("/p/Mover.as", mover);
		CHECK_EQ(workspace.compilation().counts().packages, packages);
	}
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	CHECK_EQ(workspace.full_checks(), 1);
}

// A tree checked a second time keeps nothing of the first check: a for-each loop's element type was worked
// out from a class that the second compilation does not have.
void test_reused_tree() {
	const std::string collection = "package api { [Indexed(element=\"int\")] public class Bag { public native function Bag(); } }";
	std::shared_ptr<Parsed> program = parse_file("/p/Loop.as",
			"package { import api.Bag; public class Loop { function f(b:Bag):void { for each (var i:int in b) {} } } }");
	const auto element = [&] {
		const Type *found = nullptr;
		bool any = false;
		walk(program->tree, [&](Node *p_node) {
			if (p_node->kind == NodeKind::ForIn) {
				any = true;
				found = static_cast<ForIn *>(p_node)->element;
			}
		});
		CHECK(any);
		return found;
	};
	{
		Compilation first;
		first.add("/p/api.as", collection, true);
		first.add(program);
		first.check(true);
		CHECK_EQ(first.format_diagnostics(), std::string());
		CHECK(element() != nullptr);
	}
	Compilation second;
	second.add(program);
	second.check(true);
	CHECK(element() == nullptr);
}

} // namespace

int main() {
	test_scope();
	test_directed();
	test_declaration_tags();
	test_deep_chain();
	test_reader_of_a_chain();
	test_packages_released();
	test_reused_tree();
	return check::finish();
}
