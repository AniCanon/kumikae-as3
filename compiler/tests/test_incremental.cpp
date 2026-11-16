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
#include "scratch.h"
#include "oracle_answers.h"
#include "oracle_edits.h"

#include "workspace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace kas3;
namespace fs = std::filesystem;

namespace {

long setting(const char *p_name, long p_default) {
	const char *value = std::getenv(p_name);
	return value && *value ? std::strtol(value, nullptr, 10) : p_default;
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

struct Totals {
	long edits = 0;
	long comparisons = 0;
	long partial_settles = 0;
	long lazy_questions = 0;
	long joined_or_left = 0; // Times the program's set of files changed.
	Compilation::Counts work; // Summed over every workspace: shows which hard paths the edits reached.
};

void add(Compilation::Counts &r_total, const Compilation::Counts &p_now, const Compilation::Counts &p_before) {
	r_total.declarations += p_now.declarations - p_before.declarations;
	r_total.bodies += p_now.bodies - p_before.bodies;
	r_total.bodies_only += p_now.bodies_only - p_before.bodies_only;
	r_total.cascades += p_now.cascades - p_before.cascades;
	r_total.cycles += p_now.cycles - p_before.cycles;
	r_total.reused += p_now.reused - p_before.reused;
	r_total.firsts += p_now.firsts - p_before.firsts;
	r_total.judged += p_now.judged - p_before.judged;
}

class Sequence {
	uint64_t seed_;
	bool godot_;
	bool tagged_; // A declaration file with tags is part of the program (tagged_api).
	oracle::Random random_;
	oracle::Files files_;
	std::map<std::string, std::vector<std::string>> history_;
	std::vector<std::string> log_;
	Workspace workspace_;
	Totals &totals_;
	int allowed_rebuilds_ = 0;
	std::vector<std::string> applied_; // The program's files when the workspace last applied the edits.
	int added_ = 0;
	Compilation::Counts counted_;
	int counted_rebuilds_ = 0;
	// With a root, the project is files on disk under it, as an editor opens a folder: an edit is sometimes
	// made in the editor (the file is then open, its text the editor's) and sometimes on disk, and an open
	// file is sometimes closed, which takes the text on disk again.
	std::string root_;
	std::set<std::string> open_;
	std::map<std::string, std::string> disk_;

	std::string where(const std::string &p_path) const { return root_ + p_path; }

	void write_disk(const std::string &p_path, const std::string &p_text) {
		fs::create_directories(fs::path(where(p_path)).parent_path());
		std::ofstream(where(p_path), std::ios::binary) << p_text;
		disk_[p_path] = p_text;
	}

	void put(const std::string &p_path, const std::string &p_text) {
		files_[p_path] = p_text;
		if (!root_.empty() && !open_.count(p_path) && random_.chance(40)) {
			write_disk(p_path, p_text);
			workspace_.file_changed_on_disk(where(p_path));
			return;
		}
		open_.insert(p_path);
		workspace_.change(where(p_path), p_text);
	}

	void drop(const std::string &p_path) {
		files_.erase(p_path);
		if (root_.empty()) {
			workspace_.close(where(p_path));
			return;
		}
		std::error_code error;
		fs::remove(where(p_path), error);
		disk_.erase(p_path);
		if (open_.erase(p_path)) {
			workspace_.close(where(p_path));
		} else {
			workspace_.file_changed_on_disk(where(p_path));
		}
	}

	// The workspace applies the edits that wait: a file that left the program rebuilds it once.
	void applying() {
		std::vector<std::string> paths;
		for (const auto &[path, text] : files_) {
			paths.push_back(path);
		}
		totals_.joined_or_left += paths != applied_ ? 1 : 0; // No rebuild: a file that joins or leaves is an edit.
		applied_ = std::move(paths);
	}

	bool fail(const std::string &p_what, const std::string &p_got, const std::string &p_wanted) {
		CHECK(false);
		std::printf("oracle: seed %llu (godot %d) differs after %zu edits in %s: %s\n",
				(unsigned long long)seed_, (int)godot_, log_.size(), p_what.c_str(), oracle::first_difference(p_got, p_wanted).c_str());
		for (size_t i = log_.size() > 8 ? log_.size() - 8 : 0; i < log_.size(); ++i) {
			std::printf("  edit %zu: %s\n", i, log_[i].c_str());
		}
		// A file whose text the workspace holds otherwise than the sequence does: a mistake in the sequence.
		for (const auto &[path, text] : files_) {
			const std::string *held = workspace_.text(where(path));
			if (!held || *held != text) {
				std::printf("  the workspace holds other text for %s\n", path.c_str());
			}
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
			r_workspace.open(where(path), text);
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
		Workspace reference(oracle::options(godot_, tagged_));
		this->reference(reference);
		for (const std::string &path : sampled_paths(p_edited)) {
			const std::vector<uint32_t> offsets = probe_offsets(random_, files_[path], 4);
			const std::string got = oracle::answers(workspace_, where(path), offsets);
			const std::string wanted = oracle::answers(reference, where(path), offsets);
			if (got != wanted) {
				return fail("answers for " + path, got, wanted);
			}
		}
		const std::string got = oracle::program_state(workspace_);
		const std::string wanted = oracle::program_state(reference);
		if (got != wanted) {
			return fail("the program's errors, names and types", got, wanted);
		}
		const int rebuilds = workspace_.full_checks() - 1; // The first is not a rebuild.
		if (rebuilds != allowed_rebuilds_) {
			return fail("whole-program rebuilds", std::to_string(rebuilds), std::to_string(allowed_rebuilds_));
		}
		const Compilation::Counts now = workspace_.compilation().counts();
		if (rebuilds != counted_rebuilds_) {
			counted_ = Compilation::Counts(); // A new compilation counts from zero.
			counted_rebuilds_ = rebuilds;
		}
		add(totals_.work, now, counted_);
		counted_ = now;
		return true;
	}

	// Asks about one file only, as an editor's request does, and checks the answer before anything else is
	// checked.
	bool lazy_question(const std::string &p_path) {
		applying();
		totals_.lazy_questions++;
		check::checks()++;
		const std::vector<uint32_t> offsets = probe_offsets(random_, files_[p_path], 3);
		const std::string got = oracle::answers(workspace_, where(p_path), offsets);
		Workspace reference(oracle::options(godot_, tagged_));
		this->reference(reference);
		const std::string wanted = oracle::answers(reference, where(p_path), offsets);
		return got == wanted || fail("a question about " + p_path + " only", got, wanted);
	}

	std::string change_files(std::string &r_path) {
		auto at = files_.begin();
		std::advance(at, (long)random_.below(files_.size()));
		r_path = at->first;
		std::vector<std::string> &earlier = history_[r_path];
		if (!earlier.empty() && random_.chance(8)) {
			put(r_path, earlier[random_.below(earlier.size())]);
			return "undo " + r_path;
		}
		if (!root_.empty() && open_.count(r_path) && random_.chance(10)) {
			open_.erase(r_path);
			workspace_.close(where(r_path));
			files_[r_path] = disk_[r_path];
			return "close " + r_path;
		}
		if (random_.chance(3) && files_.size() > 2) {
			drop(r_path);
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
			if (!root_.empty()) {
				write_disk(r_path, text); // On disk first, as a new file is.
				workspace_.file_changed_on_disk(where(r_path));
				files_[r_path] = text;
			} else {
				put(r_path, text);
			}
			return "add " + r_path;
		}
		earlier.push_back(at->second);
		auto other = files_.begin();
		std::advance(other, (long)random_.below(files_.size()));
		const std::vector<std::string> words = oracle::vocabulary(files_);
		oracle::Editor editor(random_, words);
		std::string text = at->second;
		const std::string what = editor.edit(text, other->second);
		put(r_path, text);
		return what + " in " + r_path;
	}

public:
	Sequence(uint64_t p_seed, bool p_godot, oracle::Files p_files, Totals &r_totals, bool p_rooted = false, bool p_tagged = false) :
			seed_(p_seed), godot_(p_godot), tagged_(p_tagged), random_(p_seed), files_(std::move(p_files)),
			workspace_(oracle::options(p_godot, p_tagged)), totals_(r_totals) {
		if (p_rooted) {
			root_ = check::fresh_folder("root" + std::to_string(p_seed)).string();
			for (const auto &[path, text] : files_) {
				write_disk(path, text);
			}
			workspace_.set_root(root_);
		} else {
			fresh(workspace_);
		}
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

} // namespace

int main() {
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
		// One sequence in three is a project folder on disk, edited in the editor and on disk.
		// One in four has a declaration file with tags, and two files that use its class.
		oracle::Files generated = oracle::generate(random, (int)classes);
		const bool tagged = seed % 4 == 3;
		if (tagged) {
			for (const char *name : { "Adds", "Loops" }) {
				generated[std::string("/project/") + name + ".as"] = std::string("package {\n\timport api.V2;\n\tpublic class ") + name +
						" {\n\t\tpublic function sum(a:V2, b:V2):V2 {\n\t\t\tfor each (var i:int in a) { trace(i); }\n"
						"\t\t\treturn a + b;\n\t\t}\n\t}\n}\n";
			}
		}
		Sequence(seed, false, std::move(generated), totals, seed % 3 == 2, tagged).run((int)edits);
		// Godot's API is checked once and kept: a sequence with it shows that it is never checked again.
		if (seed % 4 == 1) {
			Sequence(seed, true, corpus(fs::path(KAS3_TESTS_DIR) / "corpus"), totals).run((int)edits / 3);
		}
	}
	std::printf("oracle: %ld edits, %ld comparisons, %ld yielding checks, %ld one-file questions, %ld changes of the files\n",
			totals.edits, totals.comparisons, totals.partial_settles, totals.lazy_questions, totals.joined_or_left);
	const Compilation::Counts &work = totals.work;
	std::printf("oracle: %zu declarations and %zu bodies checked; %zu cascades, %zu cycle rounds, %zu symbols reused, "
				"%zu new first definitions, %zu doubt judgements, %zu bodies checked alone\n",
			work.declarations, work.bodies, work.cascades, work.cycles, work.reused, work.firsts, work.judged, work.bodies_only);
	return check::finish();
}
