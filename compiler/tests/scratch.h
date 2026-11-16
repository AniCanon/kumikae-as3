// Folders for the files a test writes. Several builds, and other worktrees, run the tests at once in one
// temporary folder, so each run of a test program has a folder of its own, removed when the program ends.
#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace check {

inline std::filesystem::path run_folder() {
	struct Run {
		std::filesystem::path path;
		Run() {
			std::random_device random;
			path = std::filesystem::temp_directory_path() / ("kas3-run-" + std::to_string(random()) + "-" + std::to_string(random()));
			std::filesystem::create_directories(path);
		}
		~Run() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	};
	static Run run;
	return run.path;
}

// An empty folder of that name in this run's own.
inline std::filesystem::path fresh_folder(const std::string &p_name) {
	const std::filesystem::path folder = run_folder() / p_name;
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);
	return std::filesystem::canonical(folder);
}

} // namespace check
