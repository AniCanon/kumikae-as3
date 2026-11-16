// The incremental-checking oracle. Random, seeded edit sequences are applied to one long-lived Workspace;
// after them, everything it says (every file's errors, what each name refers to, the type of each
// expression, and completion, hover, definition and argument help at sampled places) must equal what a
// fresh Workspace says after a whole check of the same text. Between edits the long-lived one is left
// part-checked at random: a check that yields, a question about one file only, or nothing at all.
//
// The quick run is part of the suite. A longer one:
//   KAS3_ORACLE_SEEDS=200 KAS3_ORACLE_EDITS=400 test_incremental
//   KAS3_ORACLE_CORPUS=<folder of .as files> KAS3_ORACLE_SEEDS=20 test_incremental
// KAS3_ORACLE_FIRST_SEED repeats a sequence that failed.
#include "check.h"
#include "oracle_edits.h"

#include "workspace.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace kas3;
namespace fs = std::filesystem;

namespace {

long setting(const char *p_name, long p_default) {
	const char *value = std::getenv(p_name);
	return value && *value ? std::strtol(value, nullptr, 10) : p_default;
}

Workspace::Options options(bool p_godot) {
	Workspace::Options result;
	result.godot = p_godot;
	result.support_folder = (fs::temp_directory_path() / "kas3-oracle-support").string();
	return result;
}

std::string program_state(Workspace &p_workspace) {
	Compilation &compilation = p_workspace.compilation();
	return compilation.format_diagnostics() + "-- names\n" + compilation.dump_names() + "-- types\n" + compilation.dump_types();
}

std::string place_text(const Place &p_place) {
	return p_place.unit ? p_place.unit->source.path() + ":" + std::to_string(p_place.offset) + "+" + std::to_string(p_place.length)
						: std::string("none");
}

// What the editor would be told at each offset of the file.
std::string answers(Workspace &p_workspace, const std::string &p_path, const std::vector<uint32_t> &p_offsets) {
	Unit *unit = p_workspace.unit(p_path);
	if (!unit) {
		return "no unit\n";
	}
	Queries &queries = p_workspace.queries();
	std::string out;
	for (uint32_t offset : p_offsets) {
		out += "@" + std::to_string(offset) + "\n";
		const CompletionResult completion = queries.complete(*unit, offset);
		out += completion.incomplete ? "incomplete\n" : "";
		for (const Completion &item : completion.items) {
			out += " " + item.name + "|" + item.detail + "|" + std::to_string((int)item.kind) + (item.is_keyword ? "k" : "");
			if (item.edit) {
				out += "|" + item.edit->text;
			}
			if (item.additional_edit) {
				out += "|+" + item.additional_edit->text;
			}
			out += "\n";
		}
		if (const Occurrence *found = queries.at(*unit, offset)) {
			out += "hover " + queries.describe(found->symbol) + " / " + queries.context(found->symbol) + "\n";
		}
		out += "definition " + place_text(queries.definition(*unit, offset)) + "\n";
		if (auto help = queries.signature_help(*unit, offset)) {
			out += "help " + help->label + " " + std::to_string(help->active_parameter) + "\n";
		}
	}
	return out;
}

std::vector<uint32_t> probe_offsets(oracle::Random &p_random, const std::string &p_text, int p_count) {
	std::vector<uint32_t> offsets;
	std::vector<oracle::Span> found = oracle::words(p_text);
	for (int i = 0; i < p_count && !found.empty(); ++i) {
		const oracle::Span word = p_random.pick(found);
		// Inside a name, at its end, or just after the dot before it.
		const size_t at = word.begin + p_random.below(word.end - word.begin + 1);
		offsets.push_back((uint32_t)(word.begin > 0 && p_text[word.begin - 1] == '.' && p_random.chance(30) ? word.begin : at));
	}
	return offsets;
}

std::string first_difference(const std::string &p_got, const std::string &p_wanted) {
	std::istringstream got(p_got);
	std::istringstream wanted(p_wanted);
	std::string a;
	std::string b;
	for (int line = 1;; ++line) {
		const bool more_a = (bool)std::getline(got, a);
		const bool more_b = (bool)std::getline(wanted, b);
		if (!more_a && !more_b) {
			return "no difference";
		}
		if (a != b || more_a != more_b) {
			return "line " + std::to_string(line) + "\n  incremental: " + (more_a ? a : "(end)") + "\n  full check:  " + (more_b ? b : "(end)");
		}
	}
}

struct Totals {
	long edits = 0;
	long comparisons = 0;
	long partial_settles = 0;
	long lazy_questions = 0;
};

class Sequence {
	uint64_t seed_;
	bool godot_;
	oracle::Random random_;
	oracle::Files files_;
	std::map<std::string, std::vector<std::string>> history_;
	std::vector<std::string> log_;
	Workspace workspace_;
	Totals &totals_;
	int allowed_rebuilds_ = 0;
	std::vector<std::string> applied_; // The program's files when the workspace last applied the edits.
	int added_ = 0;

	// The workspace applies the edits that wait: a file that left the program rebuilds it once.
	void applying() {
		std::vector<std::string> paths;
		for (const auto &[path, text] : files_) {
			paths.push_back(path);
		}
		allowed_rebuilds_ += paths != applied_ ? 1 : 0;
		applied_ = std::move(paths);
	}

	bool fail(const std::string &p_what, const std::string &p_got, const std::string &p_wanted) {
		CHECK(false);
		std::printf("oracle: seed %llu (godot %d) differs after %zu edits in %s: %s\n",
				(unsigned long long)seed_, (int)godot_, log_.size(), p_what.c_str(), first_difference(p_got, p_wanted).c_str());
		for (size_t i = log_.size() > 8 ? log_.size() - 8 : 0; i < log_.size(); ++i) {
			std::printf("  edit %zu: %s\n", i, log_[i].c_str());
		}
		// KAS3_ORACLE_DUMP=<folder> keeps the files, and both answers, to look into.
		if (const char *dump = std::getenv("KAS3_ORACLE_DUMP")) {
			for (const auto &[path, text] : files_) {
				fs::create_directories(fs::path(dump + path).parent_path());
				std::ofstream(dump + path, std::ios::binary) << text;
			}
			std::ofstream(fs::path(dump) / "incremental.txt", std::ios::binary) << p_got;
			std::ofstream(fs::path(dump) / "full.txt", std::ios::binary) << p_wanted;
		}
		return false;
	}

	void fresh(Workspace &r_workspace) const {
		for (const auto &[path, text] : files_) {
			r_workspace.open(path, text);
		}
	}

	// The reference: a fresh workspace with every file checked, whatever is asked of it after.
	void reference(Workspace &r_workspace) const {
		fresh(r_workspace);
		r_workspace.compilation();
	}

	std::vector<std::string> sampled_paths(const std::string &p_edited) {
		std::vector<std::string> paths;
		if (files_.count(p_edited)) {
			paths.push_back(p_edited);
		}
		for (int i = 0; i < 2 && !files_.empty(); ++i) {
			auto at = files_.begin();
			std::advance(at, (long)random_.below(files_.size()));
			paths.push_back(at->first);
		}
		return paths;
	}

	bool compare(const std::string &p_edited) {
		applying();
		totals_.comparisons++;
		check::checks()++;
		Workspace reference(options(godot_));
		this->reference(reference);
		for (const std::string &path : sampled_paths(p_edited)) {
			const std::vector<uint32_t> offsets = probe_offsets(random_, files_[path], 4);
			const std::string got = answers(workspace_, path, offsets);
			const std::string wanted = answers(reference, path, offsets);
			if (got != wanted) {
				return fail("answers for " + path, got, wanted);
			}
		}
		const std::string got = program_state(workspace_);
		const std::string wanted = program_state(reference);
		if (got != wanted) {
			return fail("the program's errors, names and types", got, wanted);
		}
		const int rebuilds = workspace_.full_checks() - 1; // The first is not a rebuild.
		if (rebuilds != allowed_rebuilds_) {
			return fail("whole-program rebuilds", std::to_string(rebuilds), std::to_string(allowed_rebuilds_));
		}
		return true;
	}

	// Asks about one file only, as an editor's request does, and checks the answer before anything else is
	// checked.
	bool lazy_question(const std::string &p_path) {
		applying();
		totals_.lazy_questions++;
		check::checks()++;
		const std::vector<uint32_t> offsets = probe_offsets(random_, files_[p_path], 3);
		const std::string got = answers(workspace_, p_path, offsets);
		Workspace reference(options(godot_));
		this->reference(reference);
		const std::string wanted = answers(reference, p_path, offsets);
		return got == wanted || fail("a question about " + p_path + " only", got, wanted);
	}

	std::string change_files(std::string &r_path) {
		auto at = files_.begin();
		std::advance(at, (long)random_.below(files_.size()));
		r_path = at->first;
		std::vector<std::string> &earlier = history_[r_path];
		if (!earlier.empty() && random_.chance(8)) {
			at->second = earlier[random_.below(earlier.size())];
			workspace_.change(r_path, at->second);
			return "undo " + r_path;
		}
		if (random_.chance(3) && files_.size() > 2) {
			workspace_.close(r_path);
			files_.erase(at);
			return "remove " + r_path;
		}
		if (random_.chance(3)) {
			// A copy of the file under another name, as pasting a file and renaming its class does.
			const std::string stem = r_path.substr(r_path.rfind('/') + 1, r_path.size() - r_path.rfind('/') - 4);
			const std::string name = "Extra" + std::to_string(++added_);
			std::string text = at->second;
			for (size_t found = text.find(stem); found != std::string::npos; found = text.find(stem, found + name.size())) {
				text.replace(found, stem.size(), name);
			}
			r_path = r_path.substr(0, r_path.rfind('/') + 1) + name + ".as";
			files_[r_path] = text;
			workspace_.open(r_path, text);
			return "add " + r_path;
		}
		earlier.push_back(at->second);
		auto other = files_.begin();
		std::advance(other, (long)random_.below(files_.size()));
		const std::vector<std::string> words = oracle::vocabulary(files_);
		oracle::Editor editor(random_, words);
		const std::string what = editor.edit(at->second, other->second);
		workspace_.change(r_path, at->second);
		return what + " in " + r_path;
	}

public:
	Sequence(uint64_t p_seed, bool p_godot, oracle::Files p_files, Totals &r_totals) :
			seed_(p_seed), godot_(p_godot), random_(p_seed), files_(std::move(p_files)), workspace_(options(p_godot)), totals_(r_totals) {
		fresh(workspace_);
		for (const auto &[path, text] : files_) {
			applied_.push_back(path);
		}
	}

	bool run(int p_edits) {
		if (!compare(std::string())) {
			return false;
		}
		for (int step = 0; step < p_edits; ++step) {
			std::string path;
			log_.push_back(change_files(path));
			totals_.edits++;
			switch (random_.below(5)) {
				case 0:
					break; // Edits pile up: the next one arrives before anything is checked.
				case 1:
					applying();
					totals_.partial_settles++;
					workspace_.settle([&] { return random_.chance(40); });
					break;
				case 2:
					if (files_.count(path) && !lazy_question(path)) {
						return false;
					}
					break;
				default:
					if (!compare(path)) {
						return false;
					}
					break;
			}
		}
		return compare(std::string());
	}
};

oracle::Files corpus(const fs::path &p_folder) {
	oracle::Files files;
	std::error_code error;
	for (fs::recursive_directory_iterator at(p_folder, error), end; !error && at != end; at.increment(error)) {
		if (at->path().extension() == ".as") {
			std::ifstream in(at->path(), std::ios::binary);
			std::ostringstream text;
			text << in.rdbuf();
			files["/project/" + at->path().lexically_relative(p_folder).generic_string()] = text.str();
		}
	}
	return files;
}

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
	Compilation &before = p_workspace.compilation();
	const size_t declarations = before.declarations_checked();
	const size_t bodies = before.bodies_checked();
	p_workspace.change(p_path, p_text);
	Compilation &after = p_workspace.compilation();
	return { after.declarations_checked() - declarations, after.bodies_checked() - bodies };
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
	// A method's signature: the file, and Thing4, which calls it.
	both_ways(thing, "step(delta:Number)", "step(delta:Number, extra:int = 0)", { 2, 2 });
	// A new method nobody calls: the file alone.
	both_ways(thing, "\t}\n}", "\t\tpublic function extra():void {}\n\t}\n}", { 1, 1 });
	// A syntax error in a body changes no declaration: the file alone. The others are only judged again.
	both_ways(thing, "var total:Number = delta;", "var total:Number = ;", { 1, 1 });
	// The hub's method, which every file calls: all of them.
	both_ways(hub, "ping(n:int):int", "ping(n:Number):int", { 21, 21 });
	// A new member of the hub: the hub alone.
	both_ways(hub, "\t}\n}", "\t\tpublic var count:int;\n\t}\n}", { 1, 1 });
	CHECK_EQ(workspace.full_checks(), 1);
	CHECK_EQ(workspace.compilation().format_diagnostics(), std::string());
}

} // namespace

int main() {
	test_scope();
	const long seeds = setting("KAS3_ORACLE_SEEDS", 12);
	const long edits = setting("KAS3_ORACLE_EDITS", 60);
	const long classes = setting("KAS3_ORACLE_CLASSES", 10);
	const uint64_t first = (uint64_t)setting("KAS3_ORACLE_FIRST_SEED", 1);
	const char *folder = std::getenv("KAS3_ORACLE_CORPUS");
	Totals totals;
	for (uint64_t seed = first; seed < first + (uint64_t)seeds; ++seed) {
		if (folder && *folder) {
			Sequence(seed, true, corpus(folder), totals).run((int)edits);
			continue;
		}
		oracle::Random random(seed * 7919);
		Sequence(seed, false, oracle::generate(random, (int)classes), totals).run((int)edits);
		// Godot's API is checked once and kept: a sequence with it shows that it is never checked again.
		if (seed % 4 == 1) {
			Sequence(seed, true, corpus(fs::path(KAS3_TESTS_DIR) / "corpus"), totals).run((int)edits / 3);
		}
	}
	std::printf("oracle: %ld edits, %ld comparisons, %ld yielding checks, %ld one-file questions\n", totals.edits,
			totals.comparisons, totals.partial_settles, totals.lazy_questions);
	return check::finish();
}
