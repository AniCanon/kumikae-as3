// Which of a project's folders hold its tests, apart from Godot: a game that is run does not have its
// tests' scripts in its program (godot_ext/sources.cpp asks), so a mistake in a test can't stop the game.
#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace kas3 {

// A place in the project as it is compared here: with no "res://" or "./" before it and no slash at either
// end. The project itself is the empty place.
inline std::string project_place(std::string_view p_path) {
	for (const std::string_view prefix : { std::string_view("res://"), std::string_view("./") }) {
		if (p_path.substr(0, prefix.size()) == prefix) {
			p_path.remove_prefix(prefix.size());
		}
	}
	while (!p_path.empty() && p_path.front() == '/') {
		p_path.remove_prefix(1);
	}
	while (!p_path.empty() && p_path.back() == '/') {
		p_path.remove_suffix(1);
	}
	return std::string(p_path);
}

// The test folders a project names (its setting), each as a place. The project itself is not one: with
// it, there would be no program.
inline std::vector<std::string> test_places(const std::vector<std::string> &p_named) {
	std::vector<std::string> places;
	for (const std::string &name : p_named) {
		std::string place = project_place(name);
		if (!place.empty()) {
			places.push_back(std::move(place));
		}
	}
	return places;
}

// Whether the path is a test folder or lies in one, by whole names: `tests_old` is not in `tests`.
inline bool in_test_folder(std::string_view p_path, const std::vector<std::string> &p_tests) {
	const std::string place = project_place(p_path);
	return std::any_of(p_tests.begin(), p_tests.end(), [&place](const std::string &p_folder) {
		return place.size() >= p_folder.size() && place.compare(0, p_folder.size(), p_folder) == 0 &&
				(place.size() == p_folder.size() || place[p_folder.size()] == '/');
	});
}

// Whether a run is the tests' own: something it starts with (its main scene, or a scene or script named on
// its command line) lies in a test folder. An option, or a path from the computer's root (the value of
// `--path`), is not a place in the project.
inline bool tests_run(const std::vector<std::string> &p_started, const std::vector<std::string> &p_tests) {
	return std::any_of(p_started.begin(), p_started.end(), [&p_tests](const std::string &p_word) {
		return !p_word.empty() && p_word.front() != '-' && p_word.front() != '/' && in_test_folder(p_word, p_tests);
	});
}

} // namespace kas3
