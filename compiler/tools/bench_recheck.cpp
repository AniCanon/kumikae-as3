// How long the language server takes to check a project again after one file changes.
//
//   bench_recheck <project folder> [--no-godot] [--rounds N]
//
// Opens the folder as a workspace (a project from tests/bench/generate_project.py), checks it whole, then
// edits the file in the middle of the order N times for each kind of edit, undoing each edit the next time:
//
//   body       a statement in a method body: nothing another file reads changes
//   signature  a method's parameters: the file whose method calls it reads that
//   member     a method added to the class: no other file names it
//   hub        Hub.as's method, which every file calls (a project made with --hub)
//
// Each is timed twice: until the edited file is checked (what a request about it waits for), and until the
// whole program is. Uses only what the Workspace has always offered, so the same source can be built against
// an older compiler to measure turn and turn about (tests/bench/recheck.py).
#include "workspace.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace {

double milliseconds(std::chrono::steady_clock::time_point p_since) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_since).count();
}

void report(const char *p_name, const char *p_until, std::vector<double> p_times) {
	if (p_times.empty()) {
		return;
	}
	std::sort(p_times.begin(), p_times.end());
	std::printf("%-10s %-8s %9.3f ms median %9.3f ms p95 %9.3f ms max\n", p_name, p_until, p_times[p_times.size() / 2],
			p_times[std::min(p_times.size() - 1, p_times.size() * 95 / 100)], p_times.back());
}

// The text with its first p_from replaced, or empty if p_from is not in it.
std::string replaced(const std::string &p_text, const char *p_from, const char *p_to) {
	const size_t at = p_text.find(p_from);
	return at == std::string::npos ? std::string() : std::string(p_text).replace(at, std::strlen(p_from), p_to);
}

struct Edit {
	const char *name;
	std::string path;
	std::string before;
	std::string after;
};

long resident_kib() {
#ifndef _WIN32
	struct rusage usage;
	getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
	return usage.ru_maxrss / 1024;
#else
	return usage.ru_maxrss;
#endif
#else
	return 0;
#endif
}

} // namespace

int main(int p_argc, char **p_argv) {
	if (p_argc < 2) {
		std::fputs("usage: bench_recheck <project folder> [--no-godot] [--rounds N]\n", stderr);
		return 2;
	}
	kas3::Workspace::Options options;
	int rounds = 20;
	for (int i = 2; i < p_argc; i++) {
		if (std::strcmp(p_argv[i], "--no-godot") == 0) {
			options.godot = false;
		} else if (std::strcmp(p_argv[i], "--rounds") == 0 && i + 1 < p_argc) {
			rounds = std::max(1, std::atoi(p_argv[++i]));
		}
	}

	auto start = std::chrono::steady_clock::now();
	kas3::Workspace workspace(options);
	workspace.set_root(p_argv[1]);
	std::vector<kas3::Unit *> units = workspace.program_units();
	workspace.compilation();
	const double first = milliseconds(start);
	if (units.empty()) {
		std::fputs("bench_recheck: no .as files in that folder\n", stderr);
		return 2;
	}
	size_t lines = 0;
	std::string hub;
	for (const kas3::Unit *unit : units) {
		lines += (size_t)unit->source.line_count();
		if (unit->source.path().size() >= 7 && unit->source.path().compare(unit->source.path().size() - 7, 7, "/Hub.as") == 0) {
			hub = unit->source.path();
		}
	}
	// The file in the middle: neither first nor last in the order they are checked in.
	const std::string path = units[units.size() / 2]->source.path();
	const std::string text = units[units.size() / 2]->source.text();
	std::vector<Edit> edits = {
		{ "body", path, text, replaced(text, "var total:Number = speed * delta + amount;", "var total:Number = speed * delta + amount + 1;") },
		{ "signature", path, text, replaced(text, "step0(delta:Number, amount:int)", "step0(delta:Number, amount:int, extra:int = 0)") },
		{ "member", path, text, replaced(text, "\tprivate var count:int = 0;", "\tprivate var count:int = 0;\n\tpublic function extra():void {}") },
	};
	if (!hub.empty()) {
		const std::string hub_text = *workspace.text(hub);
		edits.push_back({ "hub", hub, hub_text, replaced(hub_text, "tick(n:int):int", "tick(n:Number):int") });
	}
	std::printf("%zu files, %zu lines, Godot's API %s\n", units.size(), lines, options.godot ? "loaded" : "not loaded");
	std::printf("first check:  %.1f ms (reads, parses and checks everything)\n", first);
	for (const Edit &edit : edits) {
		if (edit.after.empty()) {
			std::fprintf(stderr, "bench_recheck: the %s edit does not apply to this project\n", edit.name);
			continue;
		}
		std::vector<double> file_times;
		std::vector<double> program_times;
		for (int round = 0; round < rounds; round++) {
			for (const std::string *version : { &edit.after, &edit.before }) {
				start = std::chrono::steady_clock::now();
				workspace.change(edit.path, *version);
				workspace.unit(edit.path);
				file_times.push_back(milliseconds(start));
				workspace.compilation();
				program_times.push_back(milliseconds(start));
			}
		}
		report(edit.name, "file", file_times);
		report(edit.name, "program", program_times);
	}
	std::printf("peak resident memory: %ld KiB\n", resident_kib());
	return workspace.compilation().has_errors() ? 1 : 0;
}
