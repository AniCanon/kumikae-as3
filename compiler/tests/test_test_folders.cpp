// Which of a project's folders hold its tests, and which runs are the tests' own (godot_ext/test_folders.h):
// a game that is run does not have its tests' scripts in its program, so a mistake in one can't stop it.
#include "check.h"

#include "../godot_ext/test_folders.h"

using namespace kas3;

// A place in the project as the rule compares it: no "res://", no slash at either end.
static void test_place() {
	CHECK_EQ(project_place("res://tests"), std::string("tests"));
	CHECK_EQ(project_place("res://tests/"), std::string("tests"));
	CHECK_EQ(project_place("tests/framework/"), std::string("tests/framework"));
	CHECK_EQ(project_place("./tests"), std::string("tests"));
	CHECK_EQ(project_place("res://"), std::string());
}

// A folder is a test folder when it is one of those named or lies inside one: by whole names, so
// `tests_old` and `mytests` are not inside `tests`.
static void test_within() {
	const std::vector<std::string> tests = test_places({ "res://tests", "game/checks/" });
	CHECK(in_test_folder("tests", tests));
	CHECK(in_test_folder("tests/framework/kumikae/testing", tests));
	CHECK(in_test_folder("res://tests/Suite.as", tests));
	CHECK(in_test_folder("game/checks/Deep.as", tests));
	CHECK(!in_test_folder("tests_old/Suite.as", tests));
	CHECK(!in_test_folder("mytests", tests));
	CHECK(!in_test_folder("src/tests/Suite.as", tests));
	CHECK(!in_test_folder("game", tests));
	CHECK(!in_test_folder("", tests));
	// None named: nothing is a test folder, and the project is read whole as it always was.
	CHECK(!in_test_folder("tests/Suite.as", test_places({})));
	// The project itself can't be named: that would leave no program.
	CHECK(!in_test_folder("src/Game.as", test_places({ "res://", "" })));
}

// A run is the tests' own when what it starts with lies in a test folder: the main scene, or a scene or
// script named on the command line. Options and their values are not places in the project.
static void test_run_of_tests() {
	const std::vector<std::string> tests = test_places({ "res://tests" });
	CHECK(!tests_run({ "res://main.tscn" }, tests));
	CHECK(tests_run({ "res://tests/simulator.tscn" }, tests));
	CHECK(tests_run({ "res://main.tscn", "--headless", "tests/model.tscn" }, tests));
	CHECK(tests_run({ "res://main.tscn", "-s", "res://tests/run.gd" }, tests));
	CHECK(!tests_run({ "res://main.tscn", "--path", "/somewhere/tests/project", "--quit-after", "120" }, tests));
	CHECK(!tests_run({ "res://main.tscn", "--tests" }, tests));
	CHECK(!tests_run({}, tests));
}

int main() {
	test_place();
	test_within();
	test_run_of_tests();
	return check::finish();
}
