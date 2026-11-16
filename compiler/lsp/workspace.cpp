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

std::string folder_prefix(const std::string &p_folder) {
	return !p_folder.empty() && p_folder.back() == '/' ? p_folder : p_folder + "/";
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
	fs::create_directories(fs::u8path(p_path).parent_path(), error);
	fs::permissions(fs::u8path(p_path), fs::perms::owner_write, fs::perm_options::add, error);
#ifdef _WIN32
	FILE *file = _wfopen(fs::u8path(p_path).c_str(), L"wb");
#else
	FILE *file = std::fopen(p_path.c_str(), "wb");
#endif
	if (!file) {
		return false;
	}
	bool written = std::fwrite(p_text.data(), 1, p_text.size(), file) == p_text.size();
	written = std::fclose(file) == 0 && written;
	fs::permissions(fs::u8path(p_path), fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
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
	std::error_code error;
	for (const std::string &path : options_.declarations) {
		// By absolute path, so that an editor can open it and the project scan can recognise it.
		fs::path absolute = fs::absolute(fs::u8path(path), error);
		std::string full = normal(error ? path : absolute.generic_u8string());
		std::string text;
		if (read_file(full, &text)) {
			declarations_.push_back(parse_file(full, std::move(text)));
		} else {
			std::fprintf(stderr, "kas3c: cannot read %s\n", path.c_str());
		}
	}
	if (options_.support_folder.empty()) {
		fs::path temporary = fs::temp_directory_path(error);
		if (!error) {
			options_.support_folder = (temporary / "kas3c-declarations").generic_u8string();
		}
	}
	options_.support_folder = normal(options_.support_folder);
}

Workspace::~Workspace() {}

void Workspace::set_root(const std::string &p_folder) {
	std::error_code error;
	fs::path folder = fs::absolute(fs::u8path(p_folder), error).lexically_normal();
	if (error) {
		folder = fs::u8path(p_folder).lexically_normal();
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
	root_ = normal(project.generic_u8string());
	for (auto file = files_.begin(); file != files_.end();) {
		file = file->second.open ? std::next(file) : files_.erase(file);
	}
	scan(root_);
	stale_ = true;
}

int Workspace::declaration(const std::string &p_path) const {
	for (size_t i = 0; i < declarations_.size(); i++) {
		if (declarations_[i]->source.path() == p_path) {
			return (int)i;
		}
	}
	return -1;
}

bool Workspace::is_support(const std::string &p_path) const {
	return !options_.support_folder.empty() && options_.support_folder != "/" &&
			starts_with(p_path, options_.support_folder + "/");
}

// Whether a file the workspace holds is part of the program.
bool Workspace::included(const std::string &p_path) const {
	return root_.empty() || in_project(p_path);
}

void Workspace::scan(const std::string &p_folder) {
	std::error_code error;
	fs::recursive_directory_iterator at(fs::u8path(p_folder), fs::directory_options::skip_permission_denied, error);
	const fs::recursive_directory_iterator end;
	size_t count = 0;
	for (; !error && at != end; at.increment(error)) {
		std::string name = at->path().filename().generic_u8string();
		if (at->is_directory(error)) {
			if (!name.empty() && name[0] == '.') {
				at.disable_recursion_pending();
			}
			continue;
		}
		if (at->path().extension() != ".as" || (!name.empty() && name[0] == '.')) {
			continue;
		}
		std::string path = at->path().generic_u8string();
		auto found = files_.find(path);
		if ((found != files_.end() && found->second.open) || declaration(path) >= 0) {
			continue;
		}
		if (count >= options_.max_files) {
			cut_off_ = true; // One more than is read: the editor is told.
			std::fprintf(stderr, "kas3c: %s has more than %zu .as files; the rest are not read\n", p_folder.c_str(), options_.max_files);
			break;
		}
		if (load(path)) {
			count++;
		}
	}
}

bool Workspace::in_project(const std::string &p_path) const {
	if (root_.empty() || !starts_with(p_path, folder_prefix(root_))) {
		return false;
	}
	if (fs::u8path(p_path).extension() != ".as") {
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
	if (!fs::is_regular_file(fs::u8path(p_path), error) || !read_file(p_path, &text)) {
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
	if (is_support(path)) {
		return; // Our own copy of the embedded declarations: it is in the program already.
	}
	int declared = declaration(path);
	if (declared >= 0) {
		if (declarations_[(size_t)declared]->source.text() != p_text) {
			declarations_[(size_t)declared] = parse_file(path, std::move(p_text));
			stale_ = true;
		}
		return;
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
	if (declaration(path) >= 0) {
		file_changed_on_disk(path); // Back to what is on disk.
		return;
	}
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
	int declared = declaration(path);
	if (declared >= 0) {
		std::string text;
		if (read_file(path, &text) && text != declarations_[(size_t)declared]->source.text()) {
			declarations_[(size_t)declared] = parse_file(path, std::move(text));
			stale_ = true;
		}
		return;
	}
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
		return;
	}
	if (root_.empty() || (path != root_ && !starts_with(path, folder_prefix(root_)))) {
		return;
	}
	// Not a file of the program: perhaps a folder. What was under it and is gone leaves the program, and
	// what is under it now joins.
	std::error_code error;
	const std::string prefix = path + "/";
	for (auto file = files_.lower_bound(prefix); file != files_.end() && starts_with(file->first, prefix);) {
		if (!file->second.open && !fs::is_regular_file(fs::u8path(file->first), error)) {
			file = files_.erase(file);
			stale_ = true;
		} else {
			++file;
		}
	}
	// A folder inside a dot folder holds nothing of the program: ask as if for a file in it.
	if (fs::is_directory(fs::u8path(path), error) && (path == root_ || in_project(prefix + "x.as"))) {
		scan(path);
	}
}

const std::string *Workspace::text(const std::string &p_path) const {
	std::string path = normal(p_path);
	int declared = declaration(path);
	if (declared >= 0) {
		return &declarations_[(size_t)declared]->source.text();
	}
	auto found = files_.find(path);
	return found == files_.end() || !found->second.parsed ? nullptr : &found->second.parsed->source.text();
}

bool Workspace::update_bodies() {
	if (!compilation_) {
		return false;
	}
	std::vector<std::pair<Unit *, std::shared_ptr<Parsed>>> changes;
	size_t present = 0;
	for (const auto &[path, file] : files_) {
		if (!included(path)) {
			continue;
		}
		auto found = units_.find(path);
		if (found == units_.end()) {
			return false;
		}
		++present;
		if (found->second->parsed != file.parsed) {
			changes.emplace_back(found->second, file.parsed);
		}
	}
	size_t expected = 0;
	for (const Unit &unit : compilation_->units()) {
		if (!unit.is_declaration) {
			++expected;
		}
	}
	for (const auto &parsed : declarations_) {
		auto found = units_.find(parsed->source.path());
		if (found == units_.end() || found->second->parsed != parsed) {
			return false;
		}
	}
	return present == expected && compilation_->update_bodies(changes);
}

void Workspace::check() {
	// The old compilation goes first: the new one writes onto the same trees.
	queries_.reset();
	if (update_bodies()) {
		queries_ = std::make_unique<Queries>(*compilation_);
		stale_ = false;
		++checks_;
		return;
	}
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
		if (included(file.first)) {
			compilation_->add(file.second.parsed);
		}
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
	++full_checks_;
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

std::vector<Unit *> Workspace::reported_units() {
	std::vector<Unit *> units;
	for (Unit &unit : compilation().units()) {
		if (!unit.is_builtin && unit.source.path() != GODOT_DECLARATIONS_PATH) {
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
