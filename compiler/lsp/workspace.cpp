#include "workspace.h"

#include "builtins.h"
#include "uri.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

namespace kas3 {

namespace fs = std::filesystem;

namespace {

std::string normal(const std::string &p_path) {
	return normal_path(p_path);
}

bool starts_with(const std::string &p_text, const std::string &p_prefix) {
	return p_text.compare(0, p_prefix.size(), p_prefix) == 0;
}

// True if the file holds exactly this text.
bool holds(const std::string &p_path, const std::string &p_text) {
	std::string found;
	return read_file(p_path, &found) && found == p_text;
}

// Writes the text as a read-only file, unless it is there already.
bool write_read_only(const std::string &p_path, const std::string &p_text) {
	if (holds(p_path, p_text)) {
		return true;
	}
	std::error_code error;
	fs::create_directories(fs::path(p_path).parent_path(), error);
	fs::permissions(p_path, fs::perms::owner_write, fs::perm_options::add, error);
	FILE *file = std::fopen(p_path.c_str(), "wb");
	if (!file) {
		return false;
	}
	bool written = std::fwrite(p_text.data(), 1, p_text.size(), file) == p_text.size();
	written = std::fclose(file) == 0 && written;
	fs::permissions(p_path, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
			fs::perm_options::replace, error);
	return written;
}

// "<built-in>/Array.as" -> "builtins/Array.as"; "<godot>/godot.as" -> "godot.as".
std::string support_name(const std::string &p_embedded_path) {
	size_t slash = p_embedded_path.rfind('/');
	std::string name = slash == std::string::npos ? p_embedded_path : p_embedded_path.substr(slash + 1);
	return p_embedded_path == GODOT_DECLARATIONS_PATH ? name : "builtins/" + name;
}

} // namespace

Workspace::Workspace(Options p_options) :
		options_(std::move(p_options)) {
	builtins_ = parse_builtins();
	if (options_.godot) {
		godot_ = parse_file(GODOT_DECLARATIONS_PATH, godot_declarations());
	}
	for (const std::string &path : options_.declarations) {
		std::string text;
		if (read_file(path, &text)) {
			declarations_.push_back(parse_file(normal(path), std::move(text)));
		}
	}
	std::error_code error;
	if (options_.support_folder.empty()) {
		fs::path temporary = fs::temp_directory_path(error);
		if (!error) {
			options_.support_folder = (temporary / "kas3c-declarations").string();
		}
	}
	options_.support_folder = normal(options_.support_folder);
}

Workspace::~Workspace() {}

void Workspace::set_root(const std::string &p_folder) {
	std::error_code error;
	fs::path folder = fs::absolute(fs::path(p_folder), error).lexically_normal();
	if (error) {
		folder = fs::path(p_folder).lexically_normal();
	}
	// A trailing separator would make the path's last part empty.
	if (!folder.has_filename() && folder.has_parent_path()) {
		folder = folder.parent_path();
	}
	fs::path project = folder;
	for (fs::path at = folder; !at.empty(); at = at.parent_path()) {
		if (fs::exists(at / "project.godot", error)) {
			project = at;
			break;
		}
		if (at == at.parent_path()) {
			break;
		}
	}
	root_ = project.string();
	for (auto file = files_.begin(); file != files_.end();) {
		file = file->second.open ? std::next(file) : files_.erase(file);
	}
	scan();
	stale_ = true;
}

void Workspace::scan() {
	std::error_code error;
	fs::recursive_directory_iterator at(root_, fs::directory_options::skip_permission_denied, error);
	const fs::recursive_directory_iterator end;
	size_t count = 0;
	for (; !error && at != end; at.increment(error)) {
		std::string name = at->path().filename().string();
		if (at->is_directory(error)) {
			if (!name.empty() && name[0] == '.') {
				at.disable_recursion_pending();
			}
			continue;
		}
		if (at->path().extension() != ".as" || (!name.empty() && name[0] == '.')) {
			continue;
		}
		std::string path = at->path().string();
		auto found = files_.find(path);
		if (found != files_.end() && found->second.open) {
			continue;
		}
		if (load(path) && ++count >= MAX_FILES) {
			break;
		}
	}
}

bool Workspace::in_project(const std::string &p_path) const {
	if (root_.empty() || !starts_with(p_path, root_ + "/")) {
		return false;
	}
	if (fs::path(p_path).extension() != ".as") {
		return false;
	}
	// Folders and files whose name starts with a dot are not part of a project.
	for (size_t at = root_.size(); at < p_path.size(); at++) {
		if (p_path[at] == '/' && at + 1 < p_path.size() && p_path[at + 1] == '.') {
			return false;
		}
	}
	return true;
}

bool Workspace::load(const std::string &p_path) {
	std::string text;
	std::error_code error;
	if (!fs::is_regular_file(p_path, error) || !read_file(p_path, &text)) {
		return false;
	}
	File &file = files_[p_path];
	if (file.parsed && !file.open && file.parsed->source.text() == text) {
		return true;
	}
	file.parsed = parse_file(p_path, std::move(text));
	file.open = false;
	stale_ = true;
	return true;
}

void Workspace::open(const std::string &p_path, std::string p_text) {
	std::string path = normal(p_path);
	if (starts_with(path, options_.support_folder + "/")) {
		return; // Our own copy of the embedded declarations: it is in the program already.
	}
	File &file = files_[path];
	if (file.parsed && file.open && file.parsed->source.text() == p_text) {
		return;
	}
	file.parsed = parse_file(path, std::move(p_text));
	file.open = true;
	stale_ = true;
}

void Workspace::change(const std::string &p_path, std::string p_text) {
	open(p_path, std::move(p_text));
}

void Workspace::close(const std::string &p_path) {
	std::string path = normal(p_path);
	auto found = files_.find(path);
	if (found == files_.end() || !found->second.open) {
		return;
	}
	found->second.open = false;
	stale_ = true;
	if (!in_project(path) || !load(path)) {
		files_.erase(path);
	}
}

void Workspace::file_changed_on_disk(const std::string &p_path) {
	std::string path = normal(p_path);
	auto found = files_.find(path);
	if (found != files_.end() && found->second.open) {
		return; // The editor's text is the file for now.
	}
	if (in_project(path) && load(path)) {
		return;
	}
	if (found != files_.end()) {
		files_.erase(found);
		stale_ = true;
	}
}

void Workspace::check() {
	// The old compilation goes first: the new one writes onto the same trees.
	queries_.reset();
	compilation_.reset();
	units_.clear();
	compilation_ = std::make_unique<Compilation>(builtins_);
	if (godot_) {
		// Before the program's files: a name the program defines again is then reported in the program.
		compilation_->add(godot_, true);
	}
	for (const std::shared_ptr<Parsed> &declaration : declarations_) {
		compilation_->add(declaration, true);
	}
	for (const auto &file : files_) {
		compilation_->add(file.second.parsed);
	}
	compilation_->check(true);
	queries_ = std::make_unique<Queries>(*compilation_);
	for (Unit &unit : compilation_->units()) {
		const std::string &path = unit.source.path();
		bool embedded = unit.is_builtin || path == GODOT_DECLARATIONS_PATH;
		units_[embedded ? options_.support_folder + "/" + support_name(path) : path] = &unit;
	}
	stale_ = false;
	checks_++;
}

Compilation &Workspace::compilation() {
	if (stale_ || !compilation_) {
		check();
	}
	return *compilation_;
}

Queries &Workspace::queries() {
	compilation();
	return *queries_;
}

Unit *Workspace::unit(const std::string &p_path) {
	compilation();
	auto found = units_.find(normal(p_path));
	return found == units_.end() ? nullptr : found->second;
}

std::vector<Unit *> Workspace::program_units() {
	std::vector<Unit *> units;
	for (Unit &unit : compilation().units()) {
		if (!unit.is_declaration) {
			units.push_back(&unit);
		}
	}
	return units;
}

const std::string &Workspace::support_folder() {
	if (support_folder_.empty() && !options_.support_folder.empty()) {
		bool written = true;
		for (const std::shared_ptr<Parsed> &file : builtins_) {
			written = write_read_only(options_.support_folder + "/" + support_name(file->source.path()), file->source.text()) &&
					written;
		}
		if (godot_) {
			written = write_read_only(options_.support_folder + "/" + support_name(GODOT_DECLARATIONS_PATH),
							  godot_->source.text()) &&
					written;
		}
		if (written) {
			support_folder_ = options_.support_folder;
		}
	}
	return support_folder_;
}

std::string Workspace::real_path(const Unit &p_unit) {
	const std::string &path = p_unit.source.path();
	if (!p_unit.is_builtin && path != GODOT_DECLARATIONS_PATH) {
		return path;
	}
	const std::string &folder = support_folder();
	return folder.empty() ? std::string() : folder + "/" + support_name(path);
}

} // namespace kas3
