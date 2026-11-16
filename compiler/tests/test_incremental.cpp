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

Workspace::Options options(bool p_godot) {
	Workspace::Options result;
	result.godot = p_godot;
	result.support_folder = (check::run_folder() / "support").string();
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
		Workspace reference(options(godot_));
		this->reference(reference);
		for (const std::string &path : sampled_paths(p_edited)) {
			const std::vector<uint32_t> offsets = probe_offsets(random_, files_[path], 4);
			const std::string got = answers(workspace_, where(path), offsets);
			const std::string wanted = answers(reference, where(path), offsets);
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
		const std::string got = answers(workspace_, where(p_path), offsets);
		Workspace reference(options(godot_));
		this->reference(reference);
		const std::string wanted = answers(reference, where(p_path), offsets);
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
	Sequence(uint64_t p_seed, bool p_godot, oracle::Files p_files, Totals &r_totals, bool p_rooted = false) :
			seed_(p_seed), godot_(p_godot), random_(p_seed), files_(std::move(p_files)), workspace_(options(p_godot)), totals_(r_totals) {
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

} // namespace

// A named sequence for what random edits seldom reach. After each step the workspace, asked about p_asked
// first (only that file checked), then about everything, says what a fresh workspace says.
void directed(const char *p_name, bool p_godot, oracle::Files p_files,
		const std::vector<std::pair<std::string, std::string>> &p_steps, const std::string &p_asked, const char *p_marker) {
	Workspace workspace(options(p_godot));
	for (const auto &[path, text] : p_files) {
		workspace.open(path, text);
	}
	workspace.compilation();
	int step = 0;
	int rebuilds = 1; // A file that joins or leaves the program rebuilds it.
	for (const auto &[path, text] : p_steps) {
		++step;
		rebuilds += text.empty() || !p_files.count(path) ? 1 : 0;
		if (text.empty()) {
			workspace.close(path);
			p_files.erase(path);
		} else {
			workspace.open(path, text);
			p_files[path] = text;
		}
		Workspace reference(options(p_godot));
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
	// Godot's API loaded, and a file that joins and leaves: Godot's declarations are never checked again.
	directed("godot", true, { { "/p/Mover.as", file("", "import godot.*;\npublic class Mover extends Node2D { function f():void { position.x = 1; } }") } },
			{ { "/p/Mover.as", file("", "import godot.*;\npublic class Mover extends Sprite2D { function f():void { position.x = 1; } }") },
					{ "/p/Mover.as", file("", "import godot.*;\npublic class Mover extends Sprite2D { function f():void { position. } }") } },
			"/p/Mover.as", "position.");
}

int main() {
	test_scope();
	test_directed();
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
		Sequence(seed, false, oracle::generate(random, (int)classes), totals, seed % 3 == 2).run((int)edits);
		// Godot's API is checked once and kept: a sequence with it shows that it is never checked again.
		if (seed % 4 == 1) {
			Sequence(seed, true, corpus(fs::path(KAS3_TESTS_DIR) / "corpus"), totals).run((int)edits / 3);
		}
	}
	std::printf("oracle: %ld edits, %ld comparisons, %ld yielding checks, %ld one-file questions\n", totals.edits,
			totals.comparisons, totals.partial_settles, totals.lazy_questions);
	const Compilation::Counts &work = totals.work;
	std::printf("oracle: %zu declarations and %zu bodies checked; %zu cascades, %zu cycle rounds, %zu symbols reused, "
				"%zu new first definitions, %zu doubt judgements, %zu bodies checked alone\n",
			work.declarations, work.bodies, work.cascades, work.cycles, work.reused, work.firsts, work.judged, work.bodies_only);
	return check::finish();
}
