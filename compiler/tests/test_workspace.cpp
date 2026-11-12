#include "check.h"

#include "builtins.h"
#include "uri.h"
#include "workspace.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace kas3;
namespace fs = std::filesystem;

static void write(const fs::path &p_path, const std::string &p_text) {
	fs::create_directories(p_path.parent_path());
	std::ofstream out(p_path, std::ios::binary);
	out << p_text;
}

static fs::path fresh_folder(const char *p_name) {
	fs::path folder = fs::temp_directory_path() / (std::string("kas3-test-") + p_name);
	fs::remove_all(folder);
	fs::create_directories(folder);
	return fs::canonical(folder);
}

static Workspace::Options plain() {
	Workspace::Options options;
	options.godot = false;
	return options;
}

static std::string paths(Workspace &p_workspace, const fs::path &p_root) {
	std::string out;
	for (Unit *unit : p_workspace.program_units()) {
		out += fs::path(unit->source.path()).lexically_relative(p_root).generic_string() + " ";
	}
	return out;
}

static void test_uri() {
	CHECK_EQ(path_from_uri("file:///a/b.as"), std::string("/a/b.as"));
	CHECK_EQ(path_from_uri("file:///a%20b/c%C3%A9.as"), std::string("/a b/c\xC3\xA9.as"));
	CHECK_EQ(path_from_uri("file:///a%25b.as"), std::string("/a%b.as"));
	CHECK_EQ(path_from_uri("file://localhost/a/b.as"), std::string("/a/b.as"));
	CHECK_EQ(path_from_uri("file:///c%3A/dir/b.as"), std::string("c:/dir/b.as"));
	CHECK_EQ(path_from_uri("file:///C:/dir/b.as"), std::string("C:/dir/b.as"));
	CHECK_EQ(path_from_uri("untitled:Untitled-1"), std::string());
	CHECK_EQ(path_from_uri("http://example.com/a.as"), std::string());
	CHECK_EQ(path_from_uri("file:///a%2"), std::string("/a%2")); // A broken escape is kept as written.
	CHECK_EQ(path_from_uri("file:///a%zzb"), std::string("/a%zzb"));
	CHECK_EQ(path_from_uri(""), std::string());
	CHECK_EQ(uri_from_path("/a/b.as"), std::string("file:///a/b.as"));
	CHECK_EQ(uri_from_path("/a b/c\xC3\xA9%.as"), std::string("file:///a%20b/c%C3%A9%25.as"));
	CHECK_EQ(uri_from_path("C:/dir/b.as"), std::string("file:///C:/dir/b.as"));
	for (const char *path : { "/x/y z/\xE6\x97\xA5\xE6\x9C\xAC.as", "/a#b?c/d&e=f.as", "/plain.as", "/%41.as" }) {
		CHECK_EQ(path_from_uri(uri_from_path(path)), std::string(path));
	}
}

static void test_scan() {
	fs::path root = fresh_folder("scan");
	write(root / "project.godot", "");
	write(root / "scripts" / "Player.as", "package { public class Player { public var other:Enemy; } }");
	write(root / "scripts" / "deep" / "Enemy.as", "package { public class Enemy {} }");
	write(root / "notes.txt", "not code");
	write(root / ".godot" / "Cache.as", "package { public class Player {} }");
	write(root / "scripts" / ".hidden" / "Hidden.as", "package { public class Enemy {} }");
	Workspace workspace(plain());
	// The editor opened a folder inside the project.
	workspace.set_root((root / "scripts" / "deep").string());
	CHECK_EQ(workspace.root(), root.string());
	CHECK_EQ(paths(workspace, root), std::string("scripts/Player.as scripts/deep/Enemy.as "));
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	CHECK(workspace.unit((root / "scripts" / "Player.as").string()) != nullptr);
	CHECK(workspace.unit((root / "notes.txt").string()) == nullptr);

	// No project.godot: the opened folder is the project.
	fs::path bare = fresh_folder("bare");
	write(bare / "Main.as", "package { public class Main {} }");
	Workspace second(plain());
	second.set_root(bare.string());
	CHECK_EQ(second.root(), bare.string());
	CHECK_EQ(paths(second, bare), std::string("Main.as "));
	// A folder that does not exist is an empty project.
	Workspace third(plain());
	third.set_root((bare / "missing").string());
	CHECK_EQ(third.program_units().size(), (size_t)0);
	fs::remove_all(root);
	fs::remove_all(bare);
}

static void test_buffers() {
	fs::path root = fresh_folder("buffers");
	std::string player = (root / "Player.as").string();
	std::string enemy = (root / "Enemy.as").string();
	write(player, "package { public class Player { public var other:Enemy; } }");
	write(enemy, "package { public class Enemy {} }");
	Workspace workspace(plain());
	workspace.set_root(root.string());
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	CHECK_EQ(workspace.checks(), 1);
	workspace.compilation();
	CHECK_EQ(workspace.checks(), 1); // Nothing changed: no second check.
	std::shared_ptr<Parsed> enemy_parse = workspace.unit(enemy)->parsed;

	// An open buffer stands in for the file: one unit, with the editor's text.
	workspace.open(player, "package { public class Player { public var other:Enemi; } }");
	CHECK(workspace.stale());
	CHECK_EQ(workspace.program_units().size(), (size_t)2);
	CHECK_EQ(workspace.compilation().format_diagnostics(), player + ":1:50: error: unknown type 'Enemi'\n");
	CHECK_EQ(workspace.checks(), 2);
	// The file that did not change kept its parse.
	CHECK(workspace.unit(enemy)->parsed == enemy_parse);

	workspace.change(player, "package { public class Player { public var other:Enemy; } }");
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	// The same text again is not a change.
	workspace.change(player, "package { public class Player { public var other:Enemy; } }");
	CHECK(!workspace.stale());
	// The disk copy changing under an open buffer changes nothing.
	write(player, "package { public class Player { broken");
	workspace.file_changed_on_disk(player);
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	// Closing returns to what is on disk.
	workspace.close(player);
	CHECK(workspace.compilation().has_errors());
	write(player, "package { public class Player {} }");
	workspace.file_changed_on_disk(player);
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());

	// A new file on disk joins the program; a deleted one leaves it.
	std::string extra = (root / "sub" / "Extra.as").string();
	write(extra, "package { public class Extra extends Enemy {} }");
	workspace.file_changed_on_disk(extra);
	CHECK_EQ(paths(workspace, root), std::string("Enemy.as Player.as sub/Extra.as "));
	fs::remove(enemy);
	workspace.file_changed_on_disk(enemy);
	CHECK_EQ(paths(workspace, root), std::string("Player.as sub/Extra.as "));
	CHECK_EQ(workspace.compilation().format_diagnostics(), extra + ":1:38: error: unknown type 'Enemy'\n");
	// Files that are not .as, or are outside the project, are not picked up from disk.
	write(root / "readme.md", "x");
	workspace.file_changed_on_disk((root / "readme.md").string());
	fs::path outside = fresh_folder("outside");
	write(outside / "Out.as", "package { public class Out {} }");
	workspace.file_changed_on_disk((outside / "Out.as").string());
	CHECK_EQ(paths(workspace, root), std::string("Player.as sub/Extra.as "));
	// But a file outside the project that the editor opens is checked while it is open.
	workspace.open((outside / "Out.as").string(), "package { public class Out extends Player {} }");
	CHECK_EQ(workspace.program_units().size(), (size_t)3);
	CHECK(workspace.unit((outside / "Out.as").string()) != nullptr);
	workspace.close((outside / "Out.as").string());
	CHECK_EQ(workspace.program_units().size(), (size_t)2);
	// A buffer for a file that was never saved goes away when it closes.
	std::string unsaved = (root / "New.as").string();
	workspace.open(unsaved, "package { public class New {} }");
	CHECK(workspace.unit(unsaved) != nullptr);
	workspace.close(unsaved);
	CHECK(workspace.unit(unsaved) == nullptr);
	// Closing or changing a file that was never opened does no harm.
	workspace.close((root / "Nothing.as").string());
	workspace.change((root / "Nothing.as").string(), "package {}");
	workspace.compilation();
	fs::remove_all(root);
	fs::remove_all(outside);
}

static void test_names_with_spaces() {
	fs::path root = fresh_folder("my project \xC3\xA9");
	std::string path = (root / "My Class.as").string();
	write(path, "package { public class Other {} }");
	Workspace workspace(plain());
	workspace.set_root(root.string());
	std::string uri = uri_from_path(path);
	CHECK(uri.find(' ') == std::string::npos);
	CHECK(workspace.unit(path_from_uri(uri)) != nullptr);
	workspace.open(path_from_uri(uri), "package { public class Other {} }");
	CHECK_EQ(workspace.program_units().size(), (size_t)1);
	fs::remove_all(root);
}

static void test_godot() {
	fs::path root = fresh_folder("godot");
	fs::path support = fresh_folder("support");
	std::string player = (root / "Player.as").string();
	write(player, "package { import godot.*; public class Player extends Node2D {\n"
				  "public function f():String { position = new Vector2(1, 2); return name.length; } } }");
	Workspace::Options options;
	options.support_folder = support.string();
	Workspace workspace(options);
	workspace.set_root(root.string());
	CHECK_EQ(workspace.compilation().format_diagnostics(),
			player + ":2:67: error: can't use a value of type 'int' where 'String' is expected\n");
	// Godot's API is not one of the program's files, but it has a file an editor can open.
	CHECK_EQ(workspace.program_units().size(), (size_t)1);
	const Occurrence *base = workspace.queries().at(*workspace.unit(player), 54);
	CHECK(base && base->symbol->name == "Node2D");
	std::string real = base ? workspace.real_path(*base->symbol->unit) : std::string();
	CHECK_EQ(real, (support / "godot.as").string());
	std::string text;
	CHECK(read_file(real, &text));
	CHECK_EQ(text, godot_declarations());
	CHECK_EQ(workspace.real_path(*workspace.unit(player)), player);
	// The copy maps back to Godot's unit, and opening it in the editor does not add it to the program.
	CHECK(workspace.unit(real) == base->symbol->unit);
	workspace.open(real, text);
	workspace.change(real, "package {}");
	CHECK_EQ(workspace.program_units().size(), (size_t)1);
	CHECK(workspace.unit(real) == workspace.queries().at(*workspace.unit(player), 54)->symbol->unit);
	workspace.close(real);
	// The built-in library too.
	const Occurrence *string = workspace.queries().at(*workspace.unit(player), 85);
	CHECK(string && string->symbol->name == "String");
	std::string builtin = string ? workspace.real_path(*string->symbol->unit) : std::string();
	CHECK(builtin.rfind(support.string(), 0) == 0 && fs::exists(builtin));
	CHECK(workspace.unit(builtin) == string->symbol->unit);
	// Written again without trouble by a second workspace: the files are read-only.
	Workspace again(options);
	again.set_root(root.string());
	CHECK_EQ(again.real_path(*again.queries().at(*again.unit(player), 54)->symbol->unit), real);

	// Extra declaration files.
	fs::path declared = root / "decl" / "engine.txt";
	write(declared, "package engine { public native class Motor { public native function run():void; } }");
	write(player, "package { import engine.Motor; public class Player { function f(m:Motor):void { m.run(); m.stop(); } } }");
	Workspace::Options with;
	with.godot = false;
	with.declarations.push_back(declared.string());
	Workspace third(with);
	third.set_root(root.string());
	CHECK_EQ(third.compilation().format_diagnostics(), player + ":1:92: error: 'Motor' has no member 'stop'\n");
	CHECK_EQ(third.program_units().size(), (size_t)1);
	fs::remove_all(root);
	fs::remove_all(support);
}

int main() {
	test_uri();
	test_scan();
	test_buffers();
	test_names_with_spaces();
	test_godot();
	return check::finish();
}
