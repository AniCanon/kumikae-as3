#include "paths.h"

#include <vector>

namespace kas3 {

namespace {

bool is_letter(char p_c) {
	return (p_c >= 'a' && p_c <= 'z') || (p_c >= 'A' && p_c <= 'Z');
}

char lower(char p_c) {
	return p_c >= 'A' && p_c <= 'Z' ? (char)(p_c - 'A' + 'a') : p_c;
}

bool same_text(std::string_view p_a, std::string_view p_b, PathStyle p_style) {
	if (p_a.size() != p_b.size()) {
		return false;
	}
	for (size_t i = 0; i < p_a.size(); i++) {
		if (p_a[i] != p_b[i] && !(p_style == PathStyle::Windows && lower(p_a[i]) == lower(p_b[i]))) {
			return false;
		}
	}
	return true;
}

} // namespace

std::string generic_path(std::string_view p_path, PathStyle p_style) {
	std::string text(p_path);
	const bool windows = p_style == PathStyle::Windows;
	if (windows) {
		for (char &c : text) {
			c = c == '\\' ? '/' : c;
		}
	}
	// The root, which no ".." takes back: "/", a drive, or a share's server and name (`//server/share`,
	// which a URI with a host names).
	std::string root;
	size_t at = 0;
	size_t kept = 0; // Parts that belong to the root.
	if (windows && text.size() >= 2 && is_letter(text[0]) && text[1] == ':') {
		root = std::string(1, (char)(text[0] >= 'a' ? text[0] - 'a' + 'A' : text[0])) + ":";
		at = 2;
		if (at < text.size() && text[at] == '/') {
			root += '/';
		}
	} else if (text.compare(0, 2, "//") == 0 && text.compare(0, 3, "///") != 0) {
		root = "//";
		kept = 2;
	} else if (!text.empty() && text[0] == '/') {
		root = "/";
	}
	std::vector<std::string_view> parts;
	std::string_view rest = std::string_view(text).substr(at);
	while (!rest.empty()) {
		size_t slash = rest.find('/');
		std::string_view part = rest.substr(0, slash);
		rest = slash == std::string_view::npos ? std::string_view() : rest.substr(slash + 1);
		if (part.empty() || part == ".") {
			continue;
		}
		if (part == ".." && parts.size() > kept && parts.back() != "..") {
			parts.pop_back();
		} else if (part != ".." || root.empty()) {
			parts.push_back(part); // A ".." above a root is the root.
		}
	}
	std::string out = root;
	for (size_t i = 0; i < parts.size(); i++) {
		out += i == 0 ? "" : "/";
		out += parts[i];
	}
	return out.empty() && !text.empty() ? "." : out;
}

bool path_within(std::string_view p_folder, std::string_view p_path, PathStyle p_style) {
	if (p_folder.empty() || p_path.size() < p_folder.size() || !same_text(p_folder, p_path.substr(0, p_folder.size()), p_style)) {
		return false;
	}
	return p_path.size() == p_folder.size() || p_folder.back() == '/' || p_path[p_folder.size()] == '/';
}

std::string path_join(std::string_view p_folder, std::string_view p_name) {
	std::string out(p_folder);
	if (!out.empty() && out.back() != '/') {
		out += '/';
	}
	return out + std::string(p_name);
}

} // namespace kas3
