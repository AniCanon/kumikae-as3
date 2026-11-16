// The incremental oracle at the server: an editor's session of random edits, requests and checks that stop
// part-way, against a fresh server given the same text. What the editor ends up holding for each file (the
// diagnostics last published, with their version) must be the same, and every request must be answered as
// the fresh server answers it for the text it was asked about.
//
// Longer: KAS3_ORACLE_SEEDS=100 KAS3_ORACLE_EDITS=300 test_server_incremental
#include "check.h"
#include "scratch.h"
#include "oracle_edits.h"

#include "json.h"
#include "server.h"
#include "uri.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>

using namespace kas3;
namespace fs = std::filesystem;

namespace {

long setting(const char *p_name, long p_default) {
	const char *value = std::getenv(p_name);
	return value && *value ? std::strtol(value, nullptr, 10) : p_default;
}

Workspace::Options options() {
	Workspace::Options result;
	result.godot = false;
	result.support_folder = (check::run_folder() / "support").string();
	return result;
}

// An editor: what it sends, and what it holds from what it is sent.
struct Editor {
	Server server{ options() };
	int next_id = 1;
	std::map<std::string, int> versions;
	std::map<std::string, std::string> held; // Diagnostics by URI, as last published, with their version.
	std::vector<std::string> answers; // To requests, in order.

	void take(const std::vector<std::string> &p_out) {
		for (const std::string &text : p_out) {
			Json message;
			CHECK(parse_json(text, &message));
			if (message.get("method").string() == "textDocument/publishDiagnostics") {
				const Json &params = message.get("params");
				held[params.get("uri").string()] = write_json(params.get("diagnostics")) + " v" + write_json(params.get("version"));
			} else if (message.has("id") && !message.has("method")) {
				answers.push_back(write_json(message.get("result")) + write_json(message.get("error")));
			}
		}
	}
	void send(const std::vector<Json> &p_messages) {
		std::vector<std::string> batch;
		for (const Json &message : p_messages) {
			batch.push_back(write_json(message));
		}
		std::vector<std::string> out;
		server.handle_batch(batch, out);
		take(out);
	}
	Json notification(const char *p_method, Json p_params) {
		return Json::object().set("jsonrpc", "2.0").set("method", p_method).set("params", std::move(p_params));
	}
	Json request(const char *p_method, Json p_params) {
		return Json::object().set("jsonrpc", "2.0").set("id", next_id++).set("method", p_method).set("params", std::move(p_params));
	}
	void initialize() {
		Json capabilities = Json::object().set("textDocument",
				Json::object().set("publishDiagnostics", Json::object().set("versionSupport", true)));
		send({ request("initialize", Json::object().set("processId", Json()).set("rootUri", Json()).set("capabilities", capabilities)),
				notification("initialized", Json::object()) });
		answers.clear();
	}
	Json open(const std::string &p_path, const std::string &p_text) {
		versions[p_path] = 1;
		return notification("textDocument/didOpen", Json::object().set("textDocument", Json::object()
				.set("uri", uri_from_path(p_path)).set("languageId", "actionscript").set("version", 1).set("text", p_text)));
	}
	Json change(const std::string &p_path, const std::string &p_text) {
		return notification("textDocument/didChange", Json::object()
				.set("textDocument", Json::object().set("uri", uri_from_path(p_path)).set("version", ++versions[p_path]))
				.set("contentChanges", Json::array().push(Json::object().set("text", p_text))));
	}
	Json at(const char *p_method, const std::string &p_path, const std::string &p_text, size_t p_offset) {
		int line = 0;
		int column = 0;
		for (size_t i = 0; i < p_offset && i < p_text.size(); ++i) {
			line += p_text[i] == '\n';
			column = p_text[i] == '\n' ? 0 : column + 1;
		}
		return request(p_method, Json::object().set("textDocument", Json::object().set("uri", uri_from_path(p_path)))
				.set("position", Json::object().set("line", line).set("character", column)));
	}
	// Checks what is left, stopping after each file with the given chance, as newer input would make it.
	bool settle(oracle::Random &p_random, int p_stop_percent) {
		std::vector<std::string> out;
		const bool done = server.settle(out, [&] { return p_random.chance(p_stop_percent); });
		take(out);
		return done;
	}
};

const char *const QUESTIONS[] = { "textDocument/hover", "textDocument/completion", "textDocument/definition",
	"textDocument/signatureHelp", "textDocument/documentSymbol" };

bool run(uint64_t p_seed, int p_edits, long &r_edits, long &r_yields) {
	oracle::Random random(p_seed);
	oracle::Files files = oracle::generate(random, 8);
	Editor editor;
	editor.initialize();
	std::vector<Json> opening;
	for (const auto &[path, text] : files) {
		opening.push_back(editor.open(path, text));
	}
	editor.send(opening);
	for (int step = 0; step < p_edits; ++step) {
		// One batch, as the server reads them: edits, and questions about the text as it is at that point.
		std::vector<Json> batch;
		std::vector<std::pair<Json, oracle::Files>> questions;
		for (int i = 0, count = 1 + (int)random.below(3); i < count; ++i) {
			auto at = files.begin();
			std::advance(at, (long)random.below(files.size()));
			if (random.chance(60)) {
				const std::vector<std::string> words = oracle::vocabulary(files);
				oracle::Editor typing(random, words);
				typing.edit(at->second, files.begin()->second);
				batch.push_back(editor.change(at->first, at->second));
				r_edits++;
			} else {
				Json question = editor.at(QUESTIONS[random.below(5)], at->first, at->second, random.below(at->second.size() + 1));
				batch.push_back(question);
				questions.emplace_back(question, files);
			}
		}
		const size_t asked = editor.answers.size();
		editor.send(batch);
		// Each answer is what a fresh server says about the text the question was asked about.
		for (size_t i = 0; i < questions.size(); ++i) {
			Editor fresh;
			fresh.initialize();
			std::vector<Json> all;
			for (const auto &[path, text] : questions[i].second) {
				all.push_back(fresh.open(path, text));
			}
			fresh.send(all);
			std::vector<std::string> out;
			fresh.server.settle(out); // Every file checked: the reference does not depend on what was asked.
			Json question = questions[i].first;
			question.set("id", 1);
			fresh.send({ question });
			if (editor.answers.size() != asked + questions.size() || fresh.answers.size() != 1 ||
					editor.answers[asked + i] != fresh.answers[0]) {
				CHECK(false);
				std::printf("server oracle: seed %llu step %d: an answer differs\n  incremental: %s\n  full check:  %s\n",
						(unsigned long long)p_seed, step, editor.answers.size() > asked + i ? editor.answers[asked + i].c_str() : "(none)",
						fresh.answers.empty() ? "(none)" : fresh.answers[0].c_str());
				return false;
			}
		}
		if (random.chance(70)) {
			r_yields++;
			editor.settle(random, 30);
		}
	}
	while (!editor.settle(random, 30)) {
		r_yields++;
	}
	Editor fresh;
	fresh.initialize();
	std::vector<Json> all;
	for (const auto &[path, text] : files) {
		all.push_back(fresh.open(path, text));
		fresh.versions[path] = editor.versions[path];
	}
	fresh.send(all);
	// The fresh editor's versions are all 1: compare what is held, with the version left out.
	std::map<std::string, std::string> held;
	for (const auto &[uri, diagnostics] : editor.held) {
		held[uri] = diagnostics.substr(0, diagnostics.rfind(" v"));
	}
	std::vector<std::string> out;
	fresh.server.settle(out);
	fresh.take(out);
	std::map<std::string, std::string> wanted;
	for (const auto &[uri, diagnostics] : fresh.held) {
		wanted[uri] = diagnostics.substr(0, diagnostics.rfind(" v"));
	}
	if (held != wanted) {
		CHECK(false);
		std::printf("server oracle: seed %llu: what the editor holds differs\n", (unsigned long long)p_seed);
		for (const auto &[uri, diagnostics] : wanted) {
			if (held[uri] != diagnostics) {
				std::printf("  %s\n    incremental: %s\n    full check:  %s\n", uri.c_str(), held[uri].c_str(), diagnostics.c_str());
			}
		}
		return false;
	}
	// The version held is the latest the editor sent.
	for (const auto &[path, version] : editor.versions) {
		const std::string &diagnostics = editor.held[uri_from_path(path)];
		CHECK_EQ(diagnostics.substr(diagnostics.rfind(" v") + 2), std::to_string(version));
	}
	return true;
}

// A check that stops part-way publishes what it checked, and the next one carries on from there.
void test_resumes() {
	Editor editor;
	editor.initialize();
	std::vector<Json> opening;
	for (int i = 0; i < 6; ++i) {
		const std::string path = "/project/F" + std::to_string(i) + ".as";
		opening.push_back(editor.open(path, "package { public class F" + std::to_string(i) + " { var a:Nope" + std::to_string(i) + "; } }"));
	}
	editor.send(opening);
	std::vector<std::string> out;
	int asked = 0;
	CHECK(!editor.server.settle(out, [&] { return ++asked == 2; }));
	editor.take(out);
	CHECK_EQ(editor.held.size(), (size_t)2);
	const int checks = editor.server.workspace().checks();
	out.clear();
	CHECK(editor.server.settle(out));
	editor.take(out);
	CHECK_EQ(editor.held.size(), (size_t)6);
	CHECK_EQ(editor.server.workspace().checks(), checks); // Carried on: the edits were not taken in again.
	CHECK_EQ(editor.server.workspace().full_checks(), 1);
}

} // namespace

int main() {
	test_resumes();
	const long seeds = setting("KAS3_ORACLE_SEEDS", 6);
	const long edits = setting("KAS3_ORACLE_EDITS", 40);
	const uint64_t first = (uint64_t)setting("KAS3_ORACLE_FIRST_SEED", 1);
	long total_edits = 0;
	long yields = 0;
	for (uint64_t seed = first; seed < first + (uint64_t)seeds; ++seed) {
		run(seed, (int)edits, total_edits, yields);
	}
	std::printf("server oracle: %ld edits, %ld yielding checks\n", total_edits, yields);
	return check::finish();
}
