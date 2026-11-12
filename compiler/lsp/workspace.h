// The program as the editor sees it: the project's files on disk, with the text of open files in their
// place, checked as one compilation whenever something changed.
#pragma once

#include "names.h"
#include "query.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kas3 {

class Workspace {
public:
	struct Options {
		bool godot = true; // Load Godot's API.
		std::vector<std::string> declarations; // More declaration files.
		std::string support_folder; // Where the embedded declarations are written. Empty: the temporary folder.
	};
	// No more files than this are read from a project.
	static const size_t MAX_FILES = 20000;

private:
	struct File {
		std::shared_ptr<Parsed> parsed;
		bool open = false; // The text is the editor's.
	};

	Options options_;
	std::string root_; // The project folder; empty if there is none.
	std::map<std::string, File> files_; // By path: the order they are checked in.
	std::vector<std::shared_ptr<Parsed>> builtins_;
	std::shared_ptr<Parsed> godot_;
	std::vector<std::shared_ptr<Parsed>> declarations_;
	std::unique_ptr<Compilation> compilation_;
	std::unique_ptr<Queries> queries_;
	std::unordered_map<std::string, Unit *> units_; // By the path an editor knows the file by.
	std::string support_folder_; // Empty until the embedded declarations have been written.
	bool stale_ = true;
	int checks_ = 0;

	void scan();
	bool in_project(const std::string &p_path) const;
	bool load(const std::string &p_path);
	void check();
	const std::string &support_folder();

public:
	explicit Workspace(Options p_options);
	~Workspace();

	// The folder the editor opened. The project is the nearest folder at or above it that has a
	// `project.godot`, or else the folder itself. Every .as file under the project is read.
	void set_root(const std::string &p_folder);
	const std::string &root() const { return root_; }

	void open(const std::string &p_path, std::string p_text);
	void change(const std::string &p_path, std::string p_text);
	void close(const std::string &p_path);
	// A file was created, changed or deleted outside the editor.
	void file_changed_on_disk(const std::string &p_path);

	bool stale() const { return stale_; }
	int checks() const { return checks_; } // How many times the program has been checked.
	// All three check the program first if anything changed. What they return is good until the next
	// change.
	Compilation &compilation();
	Queries &queries();
	Unit *unit(const std::string &p_path); // Null if the file is not part of the program.
	std::vector<Unit *> program_units(); // The files diagnostics are reported for.
	// A file an editor can open: the unit's own path, or for Godot's API and the built-in library a
	// read-only copy written to the support folder. Empty if that copy can't be written.
	std::string real_path(const Unit &p_unit);
};

} // namespace kas3
