#include "check.h"

#include "builtins.h"
#include "paths.h"
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
	CHECK_EQ(path_from_uri("file:///c%3A/dir/b.as", PathStyle::Posix), std::string("/c:/dir/b.as"));
	CHECK_EQ(path_from_uri("file://server/share/a.as", PathStyle::Posix), std::string("//server/share/a.as"));
	CHECK_EQ(path_from_uri("untitled:Untitled-1"), std::string());
	CHECK_EQ(path_from_uri("http://example.com/a.as"), std::string());
	CHECK_EQ(path_from_uri("file:///a%2"), std::string("/a%2")); // A broken escape is kept as written.
	CHECK_EQ(path_from_uri("file:///a%zzb"), std::string("/a%zzb"));
	CHECK_EQ(path_from_uri(""), std::string());
	CHECK_EQ(uri_from_path("/a/b.as"), std::string("file:///a/b.as"));
	CHECK_EQ(uri_from_path("/a b/c\xC3\xA9%.as"), std::string("file:///a%20b/c%C3%A9%25.as"));
	// On a Mac or Linux a '\' is part of a name, not a separator.
	CHECK_EQ(uri_from_path("/a\\b.as", PathStyle::Posix), std::string("file:///a%5Cb.as"));
	for (const char *path : { "/x/y z/\xE6\x97\xA5\xE6\x9C\xAC.as", "/a#b?c/d&e=f.as", "/plain.as", "/%41.as", "/a\\b.as" }) {
		CHECK_EQ(path_from_uri(uri_from_path(path, PathStyle::Posix), PathStyle::Posix), std::string(path));
	}

	// On Windows: a drive's letter however the editor writes it, '\' as a separator, and shares.
	const PathStyle windows = PathStyle::Windows;
	CHECK_EQ(path_from_uri("file:///c%3A/dir/b.as", windows), std::string("C:/dir/b.as"));
	CHECK_EQ(path_from_uri("file:///C:/dir/b.as", windows), std::string("C:/dir/b.as"));
	CHECK_EQ(path_from_uri("file:///c:/My%20Game/a.as", windows), std::string("C:/My Game/a.as"));
	CHECK_EQ(path_from_uri("file://server/share/a.as", windows), std::string("//server/share/a.as"));
	CHECK_EQ(path_from_uri("file://localhost/C:/a.as", windows), std::string("C:/a.as"));
	CHECK_EQ(uri_from_path("C:\\proj\\a.as", windows), std::string("file:///C:/proj/a.as"));
	CHECK_EQ(uri_from_path("C:/My Game/a.as", windows), std::string("file:///C:/My%20Game/a.as"));
	CHECK_EQ(uri_from_path("\\\\server\\share\\a.as", windows), std::string("file://server/share/a.as"));
	for (const char *path : { "C:/proj/a.as", "//server/share/dir/a.as", "D:/x y/\xE6\x97\xA5.as" }) {
		CHECK_EQ(path_from_uri(uri_from_path(path, windows), windows), std::string(path));
	}
}

// Comparing, joining and spelling paths, with the rules of both kinds of system.
static void test_paths() {
	const PathStyle posix = PathStyle::Posix;
	const PathStyle windows = PathStyle::Windows;
	CHECK_EQ(generic_path("/a//b/./c/../d/", posix), std::string("/a/b/d"));
	CHECK_EQ(generic_path("/", posix), std::string("/"));
	CHECK_EQ(generic_path("/..", posix), std::string("/"));
	CHECK_EQ(generic_path("a/../../b", posix), std::string("../b"));
	CHECK_EQ(generic_path("/a\\b", posix), std::string("/a\\b"));
	CHECK_EQ(generic_path("", posix), std::string());
	CHECK_EQ(generic_path("C:\\proj\\src\\..\\a.as", windows), std::string("C:/proj/a.as"));
	CHECK_EQ(generic_path("c:\\", windows), std::string("C:/"));
	CHECK_EQ(generic_path("c:/proj/", windows), std::string("C:/proj"));
	CHECK_EQ(generic_path("C:/..", windows), std::string("C:/"));
	CHECK_EQ(generic_path("\\\\server\\share\\a\\..\\..\\b", windows), std::string("//server/share/b"));

	CHECK(path_within("/proj", "/proj/a.as", posix));
	CHECK(path_within("/proj", "/proj", posix));
	CHECK(!path_within("/proj", "/project/a.as", posix));
	CHECK(!path_within("/proj", "/Proj/a.as", posix));
	CHECK(!path_within("", "/proj/a.as", posix));
	CHECK(path_within("/", "/a.as", posix));
	// A project at a drive's root, and letters in either case.
	CHECK(path_within("C:/", "C:/a.as", windows));
	CHECK(path_within("C:/Proj", "C:/proj/Main.as", windows));
	CHECK(!path_within("C:/proj", "D:/proj/Main.as", windows));
	CHECK(!path_within("C:/proj", "C:/projects/Main.as", windows));
	CHECK(path_within("//server/share", "//server/share/a.as", windows));

	CHECK_EQ(path_join("/proj", "a.as"), std::string("/proj/a.as"));
	CHECK_EQ(path_join("/", "a.as"), std::string("/a.as"));
	CHECK_EQ(path_join("C:/", "a.as"), std::string("C:/a.as"));
	CHECK_EQ(path_join("C:/proj", "builtins/a.as"), std::string("C:/proj/builtins/a.as"));
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
	// A file the editor opens from outside the project, or from a dot folder in it, is not part of the
	// program: a second copy of a class must not put an error on the project's own file.
	workspace.open((outside / "Player.as").string(), "package { public class Player {} }");
	workspace.open((root / ".bak" / "Player.as").string(), "package { public class Player {} }");
	CHECK_EQ(workspace.program_units().size(), (size_t)2);
	CHECK(workspace.unit((outside / "Player.as").string()) == nullptr);
	CHECK_EQ(workspace.compilation().format_diagnostics(), extra + ":1:38: error: unknown type 'Enemy'\n");
	workspace.close((outside / "Player.as").string());
	workspace.close((root / ".bak" / "Player.as").string());
	CHECK_EQ(workspace.program_units().size(), (size_t)2);
	// A folder deleted or created as a whole: its files leave and join the program.
	fs::remove_all(root / "sub");
	workspace.file_changed_on_disk((root / "sub").string());
	CHECK_EQ(paths(workspace, root), std::string("Player.as "));
	write(root / "pack" / "deep" / "Two.as", "package { public class Two {} }");
	write(root / "pack" / "One.as", "package { public class One {} }");
	workspace.file_changed_on_disk((root / "pack").string());
	CHECK_EQ(paths(workspace, root), std::string("Player.as pack/One.as pack/deep/Two.as "));
	fs::rename(root / "pack", root / "moved");
	workspace.file_changed_on_disk((root / "pack").string());
	workspace.file_changed_on_disk((root / "moved").string());
	CHECK_EQ(paths(workspace, root), std::string("Player.as moved/One.as moved/deep/Two.as "));
	fs::remove_all(root / "moved");
	workspace.file_changed_on_disk((root / "moved").string());
	CHECK_EQ(paths(workspace, root), std::string("Player.as "));
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
	// The declaration file is inside the project, but it is read once, as a declaration file: no
	// "already defined", and errors in it are reported as `kas3c check` reports them.
	write(declared, "package engine { public native class Motor { public native function run(:void; } }");
	third.file_changed_on_disk(declared.string());
	CHECK_EQ(third.program_units().size(), (size_t)1);
	CHECK_EQ(third.reported_units().size(), (size_t)2);
	CHECK_EQ(third.compilation().format_diagnostics(),
			declared.string() + ":1:73: error: expected a parameter name but found ':'\n");
	// The editor's text for it stands in, as for any file.
	third.open(declared.string(), "package engine { public native class Motor { public native function run():void; } }");
	CHECK_EQ(third.program_units().size(), (size_t)1);
	CHECK_EQ(third.compilation().format_diagnostics(), player + ":1:92: error: 'Motor' has no member 'stop'\n");
	third.close(declared.string());
	CHECK(third.compilation().format_diagnostics().find("expected a parameter name") != std::string::npos);

	// With no folder for the embedded declarations, files still open.
	Workspace::Options nowhere;
	nowhere.godot = false;
	nowhere.support_folder = "/";
	Workspace fourth(nowhere);
	fourth.open("/work/Main.as", "package { public class Main {} }");
	CHECK_EQ(fourth.program_units().size(), (size_t)1);
	fs::remove_all(root);
	fs::remove_all(support);
}

static void test_completion_cache_lifetime() {
	Workspace::Options options;
	options.godot = false;
	Workspace workspace(options);
	const std::string path = "/work/Main.as";
	const std::string head = "package { class Main { var current:";
	for (int generation = 0; generation < 40; ++generation) {
		const std::string type = generation % 2 ? "String" : "int";
		const std::string source = head + type + "; function run():void { cur } } }";
		workspace.open(path, source);
		Unit *unit = workspace.unit(path);
		Queries &queries = workspace.queries();
		CHECK_EQ(queries.cached_completion_items(), size_t{0});
		CHECK_EQ(queries.cached_context_units(), size_t{0});
		const auto caret = static_cast<uint32_t>(source.find("cur }") + 3);
		for (int request = 0; request < 100; ++request) {
			const auto items = queries.complete(*unit, caret);
			CHECK_EQ(items.size(), size_t{1});
			CHECK_EQ(items.front().detail, "var current:" + type);
			CHECK_EQ(queries.cached_completion_items(), size_t{1});
			CHECK_EQ(queries.cached_context_units(), size_t{1});
		}
	}
	workspace.close(path);
	CHECK_EQ(workspace.queries().cached_completion_items(), size_t{0});
	CHECK_EQ(workspace.queries().cached_context_units(), size_t{0});
}

// A file has one place in the program, whatever name it is reached by: through a link to its folder or
// to itself, and, where the file system does not tell them apart, in another letter case or with its
// accented letters written another way.
static void test_one_file_many_names() {
	fs::path root = fresh_folder("names");
	fs::path links = fresh_folder("names-links");
	const std::string composed = "Caf\xC3\xA9.as"; // "é" as one character.
	const std::string decomposed = "Cafe\xCC\x81.as"; // "e" and a combining accent.
	write(root / "project.godot", "");
	write(root / "game" / "Player.as", "package game { public class Player {} }");
	write(root / "game" / composed, "package game { public class Caf\xC3\xA9 {} }");
	write(links / "Outside.as", "package { public class Outside {} }");
	std::error_code error;
	fs::create_directory_symlink(root, links / "project", error);
	CHECK(!error);
	fs::create_symlink(root / "game" / "Player.as", root / "game" / "Alias.as", error);
	CHECK(!error);
	fs::create_symlink(links / "Outside.as", root / "Outside.as", error);
	CHECK(!error);
	const std::string real = (root / "game" / "Player.as").string();
	const std::string changed = "package game { public class Player { var a:Nope; } }";

	Workspace workspace(plain());
	workspace.set_root((links / "project").string());
	CHECK_EQ(workspace.root(), root.string());
	// The link to a file of the project is that file, once. A link to a file outside it is a file here.
	CHECK_EQ(paths(workspace, root), std::string("Outside.as game/" + composed + " game/Player.as "));
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	Unit *player = workspace.unit(real);
	CHECK(player != nullptr);
	auto same = [&](const fs::path &p_name) {
		const std::string name = p_name.string();
		CHECK(workspace.unit(name) == workspace.unit(real));
		workspace.open(name, changed);
		CHECK_EQ(workspace.program_units().size(), (size_t)3);
		CHECK_EQ(workspace.compilation().format_diagnostics(), real + ":1:44: error: unknown type 'Nope'\n");
		CHECK(workspace.text(real) && *workspace.text(real) == changed);
		CHECK(workspace.text(name) && *workspace.text(name) == changed);
		CHECK(workspace.unit(name) == workspace.unit(real));
		workspace.close(name);
		CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
		CHECK_EQ(workspace.program_units().size(), (size_t)3);
	};
	same(links / "project" / "game" / "Player.as"); // Through a link to the folder.
	same(root / "game" / "Alias.as"); // Through a link to the file.
	same(root / "game" / ".." / "game" / "Player.as");
	// Opened under one name, changed on disk under another: the editor's text stays.
	workspace.open(real, changed);
	workspace.file_changed_on_disk((links / "project" / "game" / "Player.as").string());
	CHECK_EQ(workspace.compilation().format_diagnostics(), real + ":1:44: error: unknown type 'Nope'\n");
	workspace.close((root / "game" / "Alias.as").string());
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	// The link to a file outside the project keeps its own name: that is where the project has it.
	CHECK(workspace.unit((root / "Outside.as").string()) != nullptr);
	workspace.open((root / "Outside.as").string(), "package { public class Outside { var a:Nope; } }");
	CHECK_EQ(workspace.program_units().size(), (size_t)3);
	workspace.close((root / "Outside.as").string());
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	// A file that is not there yet has the name it was given.
	workspace.open((links / "project" / "game" / "New.as").string(), "package game { public class New {} }");
	CHECK(workspace.unit((root / "game" / "New.as").string()) != nullptr);
	workspace.close((root / "game" / "New.as").string());
	CHECK_EQ(workspace.program_units().size(), (size_t)3);

	// Only where the file system itself takes the two names for one file.
	if (fs::exists(root / "GAME" / "PLAYER.AS")) {
		same(root / "GAME" / "PLAYER.AS");
		same(root / "game" / "player.as");
	} else {
		std::printf("note: this file system tells letter case apart; that part was not run\n");
	}
	if (fs::exists(root / "game" / decomposed)) {
		const std::string other = (root / "game" / composed).string();
		const std::string name = (root / "game" / decomposed).string();
		CHECK(workspace.unit(name) != nullptr);
		CHECK(workspace.unit(name) == workspace.unit(other));
		workspace.open(name, "package game { public class Caf\xC3\xA9 { var a:Nope; } }");
		CHECK_EQ(workspace.program_units().size(), (size_t)3);
		CHECK(workspace.compilation().format_diagnostics().find("unknown type 'Nope'") != std::string::npos);
		workspace.close(other);
		CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
	} else {
		std::printf("note: this file system tells Unicode forms apart; that part was not run\n");
	}
	fs::remove_all(root);
	fs::remove_all(links);
}

int main() {
	test_paths();
	test_one_file_many_names();
	test_completion_cache_lifetime();
	test_uri();
	test_scan();
	test_buffers();
	test_names_with_spaces();
	test_godot();
	return check::finish();
}
