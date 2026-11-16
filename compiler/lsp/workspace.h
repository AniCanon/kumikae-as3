// The program as the editor sees it: the project's files on disk, with the text of open files in their
// place, checked as one compilation whenever something changed.
#pragma once

#include "names.h"
#include "query.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kas3 {

class Workspace {
public:
	// The most files read from a project, unless the options say otherwise.
	static const size_t MAX_FILES = 20000;

	struct Options {
		bool godot = true; // Load Godot's API.
		std::vector<std::string> declarations; // More declaration files.
		std::string support_folder; // Where the embedded declarations are written. Empty: the temporary folder.
		size_t max_files = MAX_FILES; // No more files than this are read from a project.
	};
private:
	struct File {
		std::shared_ptr<Parsed> parsed;
		bool open = false; // The text is the editor's.
	};

	Options options_;
	std::string root_; // The project folder; empty if there is none.
	std::string opened_; // The folder the editor opened, by the editor's name for it.
	std::string opened_real_; // The same folder with links followed.
	std::map<std::string, File> files_; // By path: the order they are checked in.
	std::vector<std::shared_ptr<Parsed>> builtins_;
	std::shared_ptr<Parsed> godot_;
	std::vector<std::shared_ptr<Parsed>> declarations_; // The files named in the options, by absolute path.
	std::unique_ptr<Compilation> compilation_;
	std::unique_ptr<Queries> queries_;
	std::unordered_map<std::string, Unit *> units_; // By the path an editor knows the file by.
	std::string support_folder_; // Empty until the embedded declarations have been written.
	bool stale_ = true;
	std::vector<std::string> touched_; // The files whose text or presence changed since the edits were last taken in.
	bool every_file_ = false; // The project folder changed: every file is looked at.
	bool cut_off_ = false; // The project has more files than are read.
	int checks_ = 0;
	int progress_ = 0;
	int full_checks_ = 0;

	void scan(const std::string &p_folder);
	int declaration(const std::string &p_path) const; // Its place in declarations_, or -1.
	bool is_support(const std::string &p_path) const;
	bool included(const std::string &p_path) const;
	bool in_project(const std::string &p_path) const;
	bool load(const std::string &p_path);
	void check();
	bool update();
	void touch(const std::string &p_path);
	const Unit *unit_after(const std::string &p_path) const;
	Compilation &current(); // The edits applied: declarations checked, bodies perhaps not.
	void check_bodies(Unit &p_unit);
	const std::string &support_folder();

public:
	explicit Workspace(Options p_options);
	~Workspace();

	// The folder the editor opened. The project is the nearest folder at or above it that has a
	// `project.godot`, or else the folder itself. Every .as file under the project is read.
	void set_root(const std::string &p_folder);
	const std::string &root() const { return root_; }
	// For a file under the root that the program does not read because a folder on the way to it has a name
	// that starts with a dot: that folder's name. Empty for any other file.
	std::string dot_folder(const std::string &p_path) const;

	// The one name the workspace has a file by, whatever name it was reached by: links are followed, and
	// where the file system takes two spellings for one file (letter case, the form of an accented
	// letter) the spelling on disk is the name. A link in the project to a file outside it keeps its own
	// name. A file that does not exist keeps the name given, with the folders above it resolved.
	std::string resolve(const std::string &p_path) const;
	// The name an editor has for a file the workspace names: under the folder as the editor opened it.
	std::string shown_path(const std::string &p_path) const;

	// The editor's text for a file. A file outside the project (or in a dot folder of it) is not part of
	// the program, unless there is no project folder at all: then the open files are the program.
	void open(const std::string &p_path, std::string p_text);
	void change(const std::string &p_path, std::string p_text);
	void close(const std::string &p_path);
	// A file or a folder was created, changed or deleted outside the editor.
	void file_changed_on_disk(const std::string &p_path);

	// The text the workspace holds for a file right now (the editor's, if it is open), without checking
	// anything. Null if it holds none.
	const std::string *text(const std::string &p_path) const;
	// True if the file is open and the editor's text for it begins with a byte-order mark, which text()
	// leaves out.
	bool sent_with_mark(const std::string &p_path) const;
	bool stale() const { return stale_; }
	// True if the project had more .as files than the limit, so some were left out.
	bool cut_off() const { return cut_off_; }
	size_t max_files() const { return options_.max_files; }
	int full_checks() const { return full_checks_; } // Rebuilds of the whole semantic environment.
	int checks() const { return checks_; } // How many times edits have been taken in.
	int progress() const { return progress_; } // Changes each time more of the program has been checked.
	// Each applies the edits first. What they return is good until the next edit.
	// The whole program, every file's bodies checked.
	Compilation &compilation();
	// Questions about the files whose bodies are checked; declarations are always checked.
	Queries &queries();
	// Null if the file is not part of the program. Its bodies are checked; other files' may not be.
	Unit *unit(const std::string &p_path);
	// Checks what is left to check, asking p_yield after each file whether to stop for newer input. True
	// if everything is checked. What is checked is kept, and the rest is checked on the next call.
	bool settle(const std::function<bool()> &p_yield);
	bool checked(const Unit &p_unit) const { return !p_unit.unchecked; }
	// The program's own files, and the files errors are reported for (with the declaration files named in
	// the options), checked or not.
	std::vector<Unit *> program_units();
	std::vector<Unit *> reported_units();
	// A file an editor can open: the unit's own path, or for Godot's API and the built-in library a
	// read-only copy written to the support folder. Empty if that copy can't be written.
	std::string real_path(const Unit &p_unit);
};

} // namespace kas3
