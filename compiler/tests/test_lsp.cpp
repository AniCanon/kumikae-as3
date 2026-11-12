// The language server, driven in process: one JSON message in, the messages it answers with out.
#include "check.h"

#include "builtins.h"
#include "json.h"
#include "server.h"
#include "uri.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace kas3;
namespace fs = std::filesystem;

static fs::path fresh_folder(const char *p_name) {
	fs::path folder = fs::temp_directory_path() / (std::string("kas3-lsp-") + p_name);
	fs::remove_all(folder);
	fs::create_directories(folder);
	return fs::canonical(folder);
}

static void write(const fs::path &p_path, const std::string &p_text) {
	fs::create_directories(p_path.parent_path());
	std::ofstream out(p_path, std::ios::binary);
	out << p_text;
}

static Workspace::Options options(bool p_godot) {
	Workspace::Options result;
	result.godot = p_godot;
	result.support_folder = (fs::temp_directory_path() / "kas3-lsp-support").string();
	return result;
}

// An editor talking to a server.
struct Session {
	Server server;
	int next_id = 1;
	std::vector<Json> received; // Since the last send.

	explicit Session(bool p_godot = false) :
			server(options(p_godot)) {}
	explicit Session(Workspace::Options p_options) :
			server(std::move(p_options)) {}

	void take(const std::vector<std::string> &p_out) {
		for (const std::string &text : p_out) {
			Json message;
			CHECK(parse_json(text, &message));
			CHECK_EQ(message.get("jsonrpc").string(), std::string("2.0"));
			received.push_back(message);
		}
	}
	void raw(const std::string &p_text) {
		received.clear();
		std::vector<std::string> out;
		server.handle(p_text, out);
		take(out);
	}
	void notify(const char *p_method, Json p_params) {
		raw(write_json(Json::object().set("jsonrpc", "2.0").set("method", p_method).set("params", std::move(p_params))));
	}
	// Sends a request and returns its answer: the whole message, with `result` or `error`.
	Json request(const char *p_method, Json p_params) {
		int id = next_id++;
		raw(write_json(Json::object().set("jsonrpc", "2.0").set("id", id).set("method", p_method).set("params", std::move(p_params))));
		Json answer;
		int answers = 0;
		for (const Json &message : received) {
			if (!message.has("method") && message.get("id").integer(-1) == id) {
				answer = message;
				answers++;
			}
		}
		CHECK_EQ(answers, 1);
		return answer;
	}
	void settle() {
		received.clear();
		std::vector<std::string> out;
		server.settle(out);
		take(out);
	}
	void initialize(const std::string &p_root = std::string(), bool p_utf8 = false) {
		Json params = Json::object();
		params.set("processId", Json());
		params.set("rootUri", p_root.empty() ? Json() : Json(uri_from_path(p_root)));
		Json capabilities = Json::object();
		if (p_utf8) {
			capabilities.set("general", Json::object().set("positionEncodings", Json::array().push("utf-8").push("utf-16")));
		}
		params.set("capabilities", capabilities);
		Json answer = request("initialize", params);
		CHECK(answer.get("result").is_object());
		notify("initialized", Json::object());
	}
	void open(const std::string &p_uri, const std::string &p_text) {
		notify("textDocument/didOpen",
				Json::object().set("textDocument",
						Json::object().set("uri", p_uri).set("languageId", "actionscript").set("version", 1).set("text", p_text)));
	}
	void change(const std::string &p_uri, const std::string &p_text) {
		notify("textDocument/didChange",
				Json::object()
						.set("textDocument", Json::object().set("uri", p_uri).set("version", 2))
						.set("contentChanges", Json::array().push(Json::object().set("text", p_text))));
	}
	void close(const std::string &p_uri) {
		notify("textDocument/didClose", Json::object().set("textDocument", Json::object().set("uri", p_uri)));
	}
	static Json at(const std::string &p_uri, int p_line, int p_character) {
		return Json::object()
				.set("textDocument", Json::object().set("uri", p_uri))
				.set("position", Json::object().set("line", p_line).set("character", p_character));
	}
	// What was published since the last send, one line per diagnostic:
	// "<uri> <line>:<character>-<line>:<character> <message>", or "<uri> clear".
	std::string published() const {
		std::string out;
		for (const Json &message : received) {
			if (message.get("method").string() != "textDocument/publishDiagnostics") {
				continue;
			}
			const Json &params = message.get("params");
			if (params.get("diagnostics").size() == 0) {
				out += params.get("uri").string() + " clear\n";
			}
			for (const Json &item : params.get("diagnostics").items()) {
				const Json &range = item.get("range");
				CHECK_EQ(item.get("severity").integer(), (int64_t)1);
				CHECK_EQ(item.get("source").string(), std::string("kas3c"));
				out += params.get("uri").string() + " " + std::to_string(range.get("start").get("line").integer()) + ":" +
						std::to_string(range.get("start").get("character").integer()) + "-" +
						std::to_string(range.get("end").get("line").integer()) + ":" +
						std::to_string(range.get("end").get("character").integer()) + " " + item.get("message").string() + "\n";
			}
		}
		return out;
	}
};

static std::string spot(const Json &p_location) {
	if (!p_location.is_object()) {
		return "null";
	}
	const Json &range = p_location.get("range");
	std::string uri = p_location.get("uri").string();
	return uri.substr(uri.rfind('/') + 1) + " " + std::to_string(range.get("start").get("line").integer()) + ":" +
			std::to_string(range.get("start").get("character").integer()) + "-" +
			std::to_string(range.get("end").get("line").integer()) + ":" +
			std::to_string(range.get("end").get("character").integer());
}

static const std::string MAIN = "file:///work/Main.as";

static void test_lifecycle() {
	Session session;
	// Nothing is answered before `initialize`, except with an error.
	Json early = session.request("textDocument/hover", Session::at(MAIN, 0, 0));
	CHECK_EQ(early.get("error").get("code").integer(), (int64_t)-32002);
	session.open(MAIN, "package { public class Main { var x:Nope; } }");
	session.settle();
	CHECK_EQ(session.published(), std::string());

	Json answer = session.request("initialize", Json::object().set("capabilities", Json::object()));
	const Json &capabilities = answer.get("result").get("capabilities");
	CHECK_EQ(capabilities.get("positionEncoding").string(), std::string("utf-16"));
	CHECK_EQ(capabilities.get("textDocumentSync").get("change").integer(), (int64_t)2); // Changed ranges.
	CHECK(capabilities.get("textDocumentSync").get("openClose").boolean());
	CHECK(capabilities.get("definitionProvider").boolean());
	CHECK(capabilities.get("referencesProvider").boolean());
	CHECK(capabilities.get("hoverProvider").boolean());
	CHECK(capabilities.get("documentSymbolProvider").boolean());
	CHECK(capabilities.get("workspaceSymbolProvider").boolean());
	CHECK(!capabilities.has("completionProvider"));
	CHECK_EQ(answer.get("result").get("serverInfo").get("name").string(), std::string("kas3c"));
	session.notify("initialized", Json::object());
	CHECK_EQ(session.received.size(), (size_t)0); // The editor did not offer file watching: nothing to ask.

	// Unknown requests are refused; unknown notifications are ignored.
	Json unknown = session.request("textDocument/formatting", Json::object());
	CHECK_EQ(unknown.get("error").get("code").integer(), (int64_t)-32601);
	session.notify("$/setTrace", Json::object().set("value", "off"));
	CHECK_EQ(session.received.size(), (size_t)0);
	session.notify("workspace/didChangeConfiguration", Json::object());
	CHECK_EQ(session.received.size(), (size_t)0);
	// A response to something the server never asked is ignored.
	session.raw("{\"jsonrpc\":\"2.0\",\"id\":99,\"result\":null}");
	CHECK_EQ(session.received.size(), (size_t)0);

	// Malformed messages get an error and do no harm.
	session.raw("{not json");
	CHECK_EQ(session.received.size(), (size_t)1);
	CHECK_EQ(session.received[0].get("error").get("code").integer(), (int64_t)-32700);
	CHECK(session.received[0].get("id").is_null());
	session.raw("[1,2]");
	CHECK_EQ(session.received[0].get("error").get("code").integer(), (int64_t)-32600);
	session.raw("{\"jsonrpc\":\"2.0\",\"id\":5}");
	CHECK_EQ(session.received[0].get("error").get("code").integer(), (int64_t)-32600);
	// Parameters of the wrong shape give an empty answer, not a crash.
	CHECK(session.request("textDocument/definition", Json()).get("result").is_null());
	CHECK(session.request("textDocument/definition", Json::array().push(1)).get("result").is_null());
	CHECK(session.request("textDocument/hover", Session::at("file:///work/Unknown.as", 0, 0)).get("result").is_null());
	CHECK(session.request("textDocument/hover", Session::at(MAIN, -5, 99999)).get("result").is_null());
	CHECK(session.request("textDocument/hover", Session::at("not a uri", 0, 0)).get("result").is_null());
	CHECK_EQ(session.request("textDocument/references", Json::object()).get("result").size(), (size_t)0);
	CHECK_EQ(session.request("textDocument/documentSymbol", Json::object()).get("result").size(), (size_t)0);
	session.notify("textDocument/didOpen", Json::object());
	session.notify("textDocument/didChange", Json::object().set("textDocument", Json::object().set("uri", 7)));
	session.notify("textDocument/didClose", Json());

	CHECK(!session.server.exited());
	Json down = session.request("shutdown", Json());
	CHECK(down.has("result") && down.get("result").is_null());
	Json late = session.request("textDocument/hover", Session::at(MAIN, 0, 0));
	CHECK_EQ(late.get("error").get("code").integer(), (int64_t)-32600);
	session.notify("exit", Json());
	CHECK(session.server.exited());
	CHECK_EQ(session.server.exit_code(), 0);

	// `exit` without `shutdown` is an abnormal end.
	Session abrupt;
	abrupt.initialize();
	abrupt.notify("exit", Json());
	CHECK(abrupt.server.exited());
	CHECK_EQ(abrupt.server.exit_code(), 1);
	Session unborn;
	unborn.notify("exit", Json());
	CHECK(unborn.server.exited());
}

static void test_diagnostics() {
	Session session;
	session.initialize();
	// Opening a file with a mistake publishes it once the server settles.
	session.open(MAIN, "package {\npublic class Main {\n\tvar count:int = \"three\";\n}\n}\n");
	CHECK_EQ(session.published(), std::string());
	session.settle();
	CHECK_EQ(session.published(), MAIN + " 2:17-2:24 can't use a value of type 'String' where 'int' is expected\n");
	// Nothing new: nothing sent.
	session.settle();
	CHECK_EQ(session.received.size(), (size_t)0);
	session.change(MAIN, "package {\npublic class Main {\n\tvar count:int = \"three\";\n}\n}\n");
	session.settle();
	CHECK_EQ(session.received.size(), (size_t)0);
	// A second mistake: the file's whole list is sent again.
	session.change(MAIN, "package {\npublic class Main {\n\tvar count:int = \"three\";\n\tvar other:Missing;\n}\n}\n");
	session.settle();
	CHECK_EQ(session.published(),
			MAIN + " 2:17-2:24 can't use a value of type 'String' where 'int' is expected\n" + MAIN +
					" 3:11-3:18 unknown type 'Missing'\n");
	// Fixed: an empty list clears the editor's markers.
	session.change(MAIN, "package {\npublic class Main {\n\tvar count:int = 3;\n}\n}\n");
	session.settle();
	CHECK_EQ(session.published(), MAIN + " clear\n");
	// A syntax error: only syntax errors are shown, as `kas3c check` does. The range of an error that is
	// not on a name is one character, and at the end of a line it is empty.
	session.change(MAIN, "package {\npublic class Main {\n\tvar count:int = ;\n\tvar s:int = \"x\"\n}\n}\n");
	session.settle();
	CHECK_EQ(session.published(), MAIN + " 2:17-2:18 expected an expression but found ';'\n");
	session.change(MAIN, "package {\npublic class Main {\n\tfunction f():void { var a:int = 1 +");
	session.settle();
	CHECK_EQ(session.published(), MAIN + " 2:36-2:36 expected an expression but found end of file\n");
	// Closing a file that is not on disk removes it from the program and clears its errors.
	session.close(MAIN);
	session.settle();
	CHECK_EQ(session.published(), MAIN + " clear\n");
	session.settle();
	CHECK_EQ(session.received.size(), (size_t)0);

	// Several changes, one check.
	session.open(MAIN, "package { public class Main {} }");
	session.settle();
	int before = session.server.workspace().checks();
	session.change(MAIN, "package { public class Main { var a:int; } }");
	session.change(MAIN, "package { public class Main { var a:int; var b:int; } }");
	session.change(MAIN, "package { public class Main { var a:int; var b:Nope; } }");
	CHECK_EQ(session.server.workspace().checks(), before);
	session.settle();
	CHECK_EQ(session.server.workspace().checks(), before + 1);
	CHECK_EQ(session.published(), MAIN + " 0:47-0:51 unknown type 'Nope'\n");
	// A request needs the latest text: it checks, and the new errors go out with its answer.
	session.change(MAIN, "package { public class Main { var a:int; var b:Nah; } }");
	Json hover = session.request("textDocument/hover", Session::at(MAIN, 0, 35));
	CHECK(hover.get("result").is_object());
	CHECK_EQ(session.published(), MAIN + " 0:47-0:50 unknown type 'Nah'\n");

	// An error in one file caused by another: both are reported under their own names.
	const std::string other = "file:///work/Other.as";
	session.change(MAIN, "package { public class Main { var o:Other; function f():void { o.go(1); } } }");
	session.open(other, "package { public class Other { public function go():void {} } }");
	session.settle();
	CHECK_EQ(session.published(), MAIN + " 0:65-0:67 'go' takes no arguments but 1 was given\n");
	session.change(other, "package { public class Other { public function go(n:int):void {} } }");
	session.settle();
	CHECK_EQ(session.published(), MAIN + " clear\n");

	// More errors than the compiler keeps: the count of the rest is reported too.
	std::string many = "package { public class Main {\n";
	for (int i = 0; i < 150; i++) {
		many += "var v" + std::to_string(i) + ":Missing;\n";
	}
	many += "} }";
	session.change(MAIN, many);
	session.settle();
	std::string all = session.published();
	CHECK(all.find(MAIN + " 100:" ) != std::string::npos);
	CHECK(all.find(MAIN + " 101:") == std::string::npos);
	CHECK(all.find("too many errors (50 more)") != std::string::npos);
}

static void test_positions() {
	// "é" is two bytes in UTF-8 and one unit in UTF-16; "😀" is four bytes and two units.
	const std::string text = "package { public class Main {\n\tvar s:String = \"\xC3\xA9\xF0\x9F\x98\x80\"; var n:int = \"x\"; var \xC3\xA9t\xC3\xA9:Nope;\n} }";
	Session wide;
	wide.initialize();
	wide.open(MAIN, text);
	wide.settle();
	CHECK_EQ(wide.published(),
			MAIN + " 1:35-1:38 can't use a value of type 'String' where 'int' is expected\n" + MAIN +
					" 1:48-1:52 unknown type 'Nope'\n");
	// The field `été`, by a position inside it.
	Json hover = wide.request("textDocument/hover", Session::at(MAIN, 1, 46));
	CHECK(hover.get("result").get("contents").get("value").string().find("var \xC3\xA9t\xC3\xA9:Nope") != std::string::npos);
	CHECK_EQ(spot(Json::object().set("uri", MAIN).set("range", hover.get("result").get("range"))), std::string("Main.as 1:44-1:47"));

	// One unit before `n`, counting the emoji as two: the space, where there is no name.
	CHECK(wide.request("textDocument/hover", Session::at(MAIN, 1, 26)).get("result").is_null());
	CHECK(wide.request("textDocument/hover", Session::at(MAIN, 1, 27)).get("result").is_object());

	Session narrow;
	narrow.initialize(std::string(), true);
	narrow.open(MAIN, text);
	narrow.settle();
	CHECK_EQ(narrow.published(),
			MAIN + " 1:38-1:41 can't use a value of type 'String' where 'int' is expected\n" + MAIN +
					" 1:53-1:57 unknown type 'Nope'\n");
	hover = narrow.request("textDocument/hover", Session::at(MAIN, 1, 50));
	CHECK_EQ(spot(Json::object().set("uri", MAIN).set("range", hover.get("result").get("range"))), std::string("Main.as 1:47-1:52"));
	Json answer = narrow.request("initialize", Json::object()); // A second `initialize` is refused.
	CHECK_EQ(answer.get("error").get("code").integer(), (int64_t)-32600);

	// A position past the end of a line, or of the file, lands at its end.
	CHECK(wide.request("textDocument/hover", Session::at(MAIN, 1, 5000)).get("result").is_null());
	CHECK(wide.request("textDocument/hover", Session::at(MAIN, 5000, 0)).get("result").is_null());
	// Windows line endings and a lone carriage return each end a line.
	wide.change(MAIN, "package {\r\npublic class Main {\rvar a:Nope;\r\n} }");
	wide.settle();
	CHECK_EQ(wide.published(), MAIN + " 2:6-2:10 unknown type 'Nope'\n");
	// A string left open at a lone carriage return: the range stays on its line.
	wide.change(MAIN, "package { public class Main {\rvar s:String = \"abc\rvar t:String = \"x\";\r} }");
	wide.settle();
	std::string open_string = wide.published();
	CHECK(open_string.find(MAIN + " 1:15-1:16 ") == 0);
}

static void test_navigation() {
	fs::path root = fresh_folder("my project");
	write(root / "project.godot", "");
	std::string player_path = (root / "game" / "Player.as").string();
	std::string enemy_path = (root / "game" / "Enemy.as").string();
	const std::string player_text =
			"package game {\n" // 0
			"import godot.CharacterBody2D;\n" // 1
			"public class Player extends CharacterBody2D {\n" // 2
			"\tpublic static const MAX:int = 3;\n" // 3
			"\tprivate var speed:Number = 200;\n" // 4
			"\tpublic var target:Enemy;\n" // 5
			"\toverride public function _physicsProcess(delta:Number):void {\n" // 6
			"\t\ttarget.hurt(speed * delta);\n" // 7
			"\t\tmoveAndSlide();\n" // 8
			"\t}\n"
			"}\n"
			"}\n";
	write(player_path, player_text);
	write(enemy_path, "package game {\npublic class Enemy {\n\tpublic function hurt(amount:Number):void {}\n}\n}\n");
	Session session(true);
	session.initialize(root.string());
	session.settle();
	CHECK_EQ(session.published(), std::string()); // A clean project: nothing to say.
	// The editor's URI for a path with a space.
	std::string player = uri_from_path(player_path);
	std::string enemy = uri_from_path(enemy_path);
	CHECK(player.find("my%20project") != std::string::npos);
	session.open(player, player_text);
	session.settle();
	CHECK_EQ(session.published(), std::string());

	// Go to definition: a field, a method in another file, a class.
	CHECK_EQ(spot(session.request("textDocument/definition", Session::at(player, 7, 16)).get("result")),
			std::string("Player.as 4:13-4:18"));
	Json hurt = session.request("textDocument/definition", Session::at(player, 7, 10)).get("result");
	CHECK_EQ(spot(hurt), std::string("Enemy.as 2:17-2:21"));
	CHECK_EQ(hurt.get("uri").string(), enemy);
	CHECK_EQ(spot(session.request("textDocument/definition", Session::at(player, 5, 20)).get("result")),
			std::string("Enemy.as 1:13-1:18"));
	CHECK(session.request("textDocument/definition", Session::at(player, 0, 3)).get("result").is_null());
	// Into Godot's API: a file that exists, and that the server answers questions about.
	Json move = session.request("textDocument/definition", Session::at(player, 8, 5)).get("result");
	std::string godot_uri = move.get("uri").string();
	std::string godot_path = path_from_uri(godot_uri);
	CHECK(fs::exists(godot_path));
	std::string godot_text;
	CHECK(read_file(godot_path, &godot_text));
	int line = (int)move.get("range").get("start").get("line").integer();
	int character = (int)move.get("range").get("start").get("character").integer();
	size_t start = 0;
	for (int i = 0; i < line; i++) {
		start = godot_text.find('\n', start) + 1;
	}
	CHECK_EQ(godot_text.substr(start + (size_t)character, 12), std::string("moveAndSlide"));
	session.open(godot_uri, godot_text);
	Json inside = session.request("textDocument/hover", Session::at(godot_uri, line, character + 2));
	CHECK(inside.get("result").get("contents").get("value").string().find("function moveAndSlide():Boolean") != std::string::npos);
	session.settle();
	CHECK_EQ(session.published(), std::string()); // Never an error about Godot's own file.
	CHECK_EQ(session.server.workspace().program_units().size(), (size_t)2);

	// Find references, with and without the declaration.
	Json params = Session::at(player, 4, 15);
	params.set("context", Json::object().set("includeDeclaration", true));
	Json found = session.request("textDocument/references", params).get("result");
	CHECK_EQ(found.size(), (size_t)2);
	CHECK_EQ(spot(found.at(0)), std::string("Player.as 4:13-4:18"));
	CHECK_EQ(spot(found.at(1)), std::string("Player.as 7:14-7:19"));
	params.set("context", Json::object().set("includeDeclaration", false));
	found = session.request("textDocument/references", params).get("result");
	CHECK_EQ(found.size(), (size_t)1);
	// From the other file, which is not open.
	params = Session::at(enemy, 2, 18);
	params.set("context", Json::object().set("includeDeclaration", false));
	found = session.request("textDocument/references", params).get("result");
	CHECK_EQ(found.size(), (size_t)1);
	CHECK_EQ(spot(found.at(0)), std::string("Player.as 7:9-7:13"));
	CHECK_EQ(found.at(0).get("uri").string(), player);

	// Hover: the declaration and where it lives.
	Json hover = session.request("textDocument/hover", Session::at(player, 7, 10)).get("result");
	CHECK_EQ(hover.get("contents").get("kind").string(), std::string("markdown"));
	CHECK_EQ(hover.get("contents").get("value").string(),
			std::string("```actionscript\nfunction hurt(amount:Number):void\n```\nmethod of game.Enemy"));
	CHECK(session.request("textDocument/hover", Session::at(player, 9, 1)).get("result").is_null());

	// The outline.
	Json outline = session.request("textDocument/documentSymbol",
										  Json::object().set("textDocument", Json::object().set("uri", player)))
						   .get("result");
	CHECK_EQ(outline.size(), (size_t)1);
	CHECK_EQ(outline.at(0).get("name").string(), std::string("Player"));
	CHECK_EQ(outline.at(0).get("kind").integer(), (int64_t)5);
	CHECK_EQ(spot(Json::object().set("uri", player).set("range", outline.at(0).get("selectionRange"))), std::string("Player.as 2:13-2:19"));
	const Json &children = outline.at(0).get("children");
	CHECK_EQ(children.size(), (size_t)4);
	CHECK_EQ(children.at(0).get("name").string(), std::string("MAX"));
	CHECK_EQ(children.at(0).get("kind").integer(), (int64_t)14); // A constant.
	CHECK_EQ(children.at(1).get("kind").integer(), (int64_t)8); // A field.
	CHECK_EQ(children.at(3).get("name").string(), std::string("_physicsProcess"));
	CHECK_EQ(children.at(3).get("kind").integer(), (int64_t)6); // A method.

	// Workspace symbols.
	Json symbols = session.request("workspace/symbol", Json::object().set("query", "hur")).get("result");
	CHECK_EQ(symbols.size(), (size_t)1);
	CHECK_EQ(symbols.at(0).get("name").string(), std::string("hurt"));
	CHECK_EQ(symbols.at(0).get("containerName").string(), std::string("game.Enemy"));
	CHECK_EQ(spot(symbols.at(0).get("location")), std::string("Enemy.as 2:17-2:21"));
	CHECK(session.request("workspace/symbol", Json::object().set("query", "")).get("result").size() >= 6);

	// A file changes on disk: the program follows, and the errors appear in the open file.
	write(enemy_path, "package game {\npublic class Enemy {\n}\n}\n");
	session.notify("workspace/didChangeWatchedFiles",
			Json::object().set("changes", Json::array().push(Json::object().set("uri", enemy).set("type", 2))));
	session.settle();
	CHECK_EQ(session.published(), player + " 7:9-7:13 'Enemy' has no member 'hurt'\n");
	fs::remove(enemy_path);
	session.notify("workspace/didChangeWatchedFiles",
			Json::object().set("changes", Json::array().push(Json::object().set("uri", enemy).set("type", 3))));
	session.settle();
	CHECK_EQ(session.published(), player + " 5:19-5:24 unknown type 'Enemy'\n");
	// Closing the open file returns to the disk copy, which has the same error: nothing to send.
	session.close(player);
	session.settle();
	CHECK_EQ(session.received.size(), (size_t)0);
	fs::remove_all(root);
}

// A declaration file given on the command line: its errors are reported, as `kas3c check` reports them.
static void test_declaration_files() {
	fs::path root = fresh_folder("declared");
	fs::path declared = root / "lib" / "engine.as";
	write(declared, "package engine { public native class Motor { public native function run(:void; } }");
	write(root / "Main.as", "package { public class Main { var n:int = \"x\"; } }");
	Workspace::Options with = options(false);
	with.declarations.push_back(declared.string());
	Session session(with);
	session.initialize(root.string());
	session.settle();
	CHECK_EQ(session.published(), uri_from_path(declared.string()) + " 0:72-0:73 expected a parameter name but found ':'\n");
	write(declared, "package engine { public native class Motor { public native function run():void; } }");
	session.notify("workspace/didChangeWatchedFiles",
			Json::object().set("changes", Json::array().push(Json::object().set("uri", uri_from_path(declared.string())).set("type", 2))));
	session.settle();
	CHECK_EQ(session.published(),
			uri_from_path(declared.string()) + " clear\n" + uri_from_path((root / "Main.as").string()) +
					" 0:42-0:45 can't use a value of type 'String' where 'int' is expected\n");
	fs::remove_all(root);
}

// A project with more files than the server reads says so in the editor, not only on standard error.
static void test_file_limit() {
	fs::path root = fresh_folder("many");
	for (int i = 0; i < 5; i++) {
		write(root / ("C" + std::to_string(i) + ".as"), "package { public class C" + std::to_string(i) + " {} }");
	}
	Workspace::Options few = options(false);
	few.max_files = 3;
	Session session(few);
	session.initialize(root.string());
	int warnings = 0;
	for (int round = 0; round < 2; round++) {
		session.settle();
		for (const Json &message : session.received) {
			if (message.get("method").string() == "window/showMessage") {
				warnings++;
				CHECK_EQ(message.get("params").get("type").integer(), (int64_t)2);
				CHECK(message.get("params").get("message").string().find("more than 3") != std::string::npos);
			}
		}
	}
	CHECK_EQ(warnings, 1); // Once.
	CHECK_EQ(session.server.workspace().program_units().size(), (size_t)3);
	fs::remove_all(root);
}

// An editor sends the part of a document that changed. Several changes in one message apply in order, each
// to the text the one before left, with positions counted as the editor counts them.
static void test_ranged_changes() {
	for (int encoding = 0; encoding < 2; encoding++) {
		bool utf8 = encoding == 1;
		Session session;
		session.initialize(std::string(), utf8);
		// "é" is one UTF-16 unit and two bytes; "😀" is two units and four bytes.
		session.open(MAIN, "package {\npublic class Main {\n\tvar s:String = \"\xC3\xA9\xF0\x9F\x98\x80\"; var n:int = 1;\n}\n}\n");
		session.settle();
		CHECK_EQ(session.published(), std::string());
		auto range = [](int p_line, int p_from, int p_to_line, int p_to) {
			return Json::object()
					.set("start", Json::object().set("line", p_line).set("character", p_from))
					.set("end", Json::object().set("line", p_to_line).set("character", p_to));
		};
		auto change = [&](Json p_changes) {
			session.notify("textDocument/didChange",
					Json::object().set("textDocument", Json::object().set("uri", MAIN).set("version", 2)).set("contentChanges", std::move(p_changes)));
			session.settle();
		};
		// Replace the `1` after the non-ASCII text with a string: the position differs by encoding.
		int one = utf8 ? 38 : 35;
		change(Json::array().push(Json::object().set("range", range(2, one, 2, one + 1)).set("text", "\"x\"")));
		CHECK_EQ(session.published(), MAIN + " 2:" + std::to_string(one) + "-2:" + std::to_string(one + 3) +
											   " can't use a value of type 'String' where 'int' is expected\n");
		// Two changes at once: an insertion of a new line, then a fix on the line it pushed down.
		change(Json::array()
						.push(Json::object().set("range", range(1, 19, 1, 19)).set("text", "\n\tvar extra:Missing;"))
						.push(Json::object().set("range", range(3, one, 3, one + 3)).set("text", "2")));
		CHECK_EQ(session.published(), MAIN + " 2:11-2:18 unknown type 'Missing'\n");
		// A deletion across lines, and a whole-document change mixed in.
		change(Json::array().push(Json::object().set("range", range(1, 19, 2, 19)).set("text", "")));
		CHECK_EQ(session.published(), MAIN + " clear\n");
		change(Json::array()
						.push(Json::object().set("text", "package { public class Main { var a:int; } }"))
						.push(Json::object().set("range", range(0, 36, 0, 39)).set("text", "Nope")));
		CHECK_EQ(session.published(), MAIN + " 0:36-0:40 unknown type 'Nope'\n");
		// A range past the end of the text is clamped to it, not a crash.
		change(Json::array().push(Json::object().set("range", range(50, 0, 99, 7)).set("text", " ")));
		change(Json::array().push(Json::object().set("range", range(0, 36, 0, 40)).set("text", "int")));
		CHECK_EQ(session.published(), MAIN + " clear\n");
	}
}

static void test_file_watching() {
	Session session;
	Json params = Json::object();
	params.set("capabilities",
			Json::object().set("workspace",
					Json::object().set("didChangeWatchedFiles", Json::object().set("dynamicRegistration", true))));
	session.request("initialize", params);
	session.notify("initialized", Json::object());
	CHECK_EQ(session.received.size(), (size_t)1);
	const Json &ask = session.received[0];
	CHECK_EQ(ask.get("method").string(), std::string("client/registerCapability"));
	CHECK(ask.has("id"));
	const Json &registration = ask.get("params").get("registrations").at(0);
	CHECK_EQ(registration.get("method").string(), std::string("workspace/didChangeWatchedFiles"));
	CHECK_EQ(registration.get("registerOptions").get("watchers").at(0).get("globPattern").string(), std::string("**/*"));
}

// What the server publishes for a file is what `kas3c check` prints for it.
static void test_agreement() {
	int files = 0;
	for (const char *folder : { "golden/errors", "golden/names/errors", "golden/types/errors" }) {
		for (const auto &entry : fs::directory_iterator(std::string(KAS3_TESTS_DIR) + "/" + folder)) {
			if (entry.path().extension() != ".as") {
				continue;
			}
			fs::path expected_path = entry.path();
			expected_path.replace_extension(".diag");
			std::string text, expected;
			CHECK(read_file(entry.path().string(), &text));
			CHECK(read_file(expected_path.string(), &expected));
			files++;
			Session session(true);
			session.initialize(std::string(), true);
			std::string uri = uri_from_path(entry.path().string());
			session.open(uri, text);
			session.settle();
			std::string actual;
			for (const Json &message : session.received) {
				for (const Json &item : message.get("params").get("diagnostics").items()) {
					const Json &at = item.get("range").get("start");
					bool placed = item.get("message").string().rfind("too many errors", 0) != 0;
					actual += std::string(folder) + "/" + entry.path().filename().string() +
							(placed ? ":" + std::to_string(at.get("line").integer() + 1) + ":" +
											  std::to_string(at.get("character").integer() + 1)
									: std::string()) +
							": error: " + item.get("message").string() + "\n";
				}
			}
			check::checks()++;
			if (actual != expected) {
				check::failures()++;
				std::printf("FAIL %s differs from kas3c check:\n--- server\n%s--- kas3c\n%s", entry.path().c_str(), actual.c_str(),
						expected.c_str());
			}
		}
	}
	CHECK(files >= 10);
}

// Prefixes of every corpus file, and the file with one byte missing, as an editor would send them while
// someone types: about 200 edits a file. Nothing may crash, and every request must be answered.
// (test_query runs every prefix and every deletion through the checker and the queries.)
static void test_typing() {
	int files = 0;
	for (const auto &entry : fs::directory_iterator(std::string(KAS3_TESTS_DIR) + "/corpus")) {
		std::string text;
		if (entry.path().extension() != ".as" || !read_file(entry.path().string(), &text)) {
			continue;
		}
		files++;
		// Godot's API makes each check slower, so it is loaded for some files only.
		Session session(files % 4 == 0);
		session.initialize(std::string(), files % 2 == 0);
		std::string uri = "file:///corpus/" + entry.path().filename().string();
		session.open(uri, text);
		session.settle();
		size_t step = text.size() / 200 + 1;
		for (size_t i = 0; i < text.size(); i += step) {
			bool prefix = (i / step) % 2 == 0;
			std::string edited = prefix ? text.substr(0, i) : text.substr(0, i) + text.substr(i + 1);
			session.change(uri, edited);
			// Ask where the edit is, and a little before it.
			int line = 0, character = 0;
			for (size_t at = 0; at < i && at < edited.size(); at++) {
				if (edited[at] == '\n') {
					line++;
					character = 0;
				} else {
					character++;
				}
			}
			for (int back = 0; back < 3; back++) {
				Json where = Session::at(uri, line, character > back ? character - back : 0);
				Json definition = session.request("textDocument/definition", where);
				CHECK(definition.has("result"));
				CHECK(session.request("textDocument/hover", where).has("result"));
				where.set("context", Json::object().set("includeDeclaration", true));
				CHECK(session.request("textDocument/references", where).has("result"));
			}
			CHECK(session.request("textDocument/documentSymbol", Json::object().set("textDocument", Json::object().set("uri", uri)))
							.get("result")
							.is_array());
			if ((i / step) % 50 == 0) {
				CHECK(session.request("workspace/symbol", Json::object().set("query", "a")).get("result").is_array());
			}
			session.settle();
		}
		// The whole file again: whatever was reported along the way is cleared.
		session.change(uri, text);
		session.settle();
		session.change(uri, text + " ");
		session.settle();
		CHECK_EQ(session.published(), std::string());
	}
	CHECK_EQ(files, 14);
}

int main() {
	test_lifecycle();
	test_diagnostics();
	test_positions();
	test_navigation();
	test_declaration_files();
	test_file_limit();
	test_ranged_changes();
	test_file_watching();
	test_agreement();
	test_typing();
	return check::finish();
}
