// How long the language server takes to check a project again after one file changes.
//
//   bench_recheck <project folder> [--no-godot]
//
// Opens the folder as a workspace, then changes one file 20 times, as typing would.
#include "workspace.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static double milliseconds(std::chrono::steady_clock::time_point p_since) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_since).count();
}

int main(int p_argc, char **p_argv) {
	if (p_argc < 2) {
		std::fputs("usage: bench_recheck <project folder> [--no-godot]\n", stderr);
		return 2;
	}
	kas3::Workspace::Options options;
	options.godot = !(p_argc > 2 && std::strcmp(p_argv[2], "--no-godot") == 0);

	auto start = std::chrono::steady_clock::now();
	kas3::Workspace workspace(options);
	workspace.set_root(p_argv[1]);
	std::vector<kas3::Unit *> units = workspace.program_units();
	double first = milliseconds(start);
	if (units.empty()) {
		std::fputs("bench_recheck: no .as files in that folder\n", stderr);
		return 2;
	}
	size_t lines = 0;
	for (const kas3::Unit *unit : units) {
		lines += (size_t)unit->source.line_count();
	}
	// The file in the middle: neither first nor last in the order they are checked in.
	const std::string path = units[units.size() / 2]->source.path();
	const std::string text = units[units.size() / 2]->source.text();

	std::vector<double> times;
	for (int round = 0; round < 20; round++) {
		std::string edited = text + std::string((size_t)round % 2 + 1, '\n');
		start = std::chrono::steady_clock::now();
		workspace.change(path, std::move(edited));
		workspace.compilation();
		times.push_back(milliseconds(start));
	}
	std::sort(times.begin(), times.end());
	std::printf("%zu files, %zu lines, Godot's API %s\n", units.size(), lines, options.godot ? "loaded" : "not loaded");
	std::printf("first check:  %.1f ms (reads and parses everything)\n", first);
	std::printf("re-check:     %.1f ms median, %.1f ms fastest, %.1f ms slowest\n", times[times.size() / 2], times.front(),
			times.back());
	return workspace.compilation().has_errors() ? 1 : 0;
}
