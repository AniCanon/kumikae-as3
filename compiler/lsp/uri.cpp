#include "uri.h"


namespace kas3 {

namespace {

int hex_value(char p_c) {
	if (p_c >= '0' && p_c <= '9') {
		return p_c - '0';
	}
	if (p_c >= 'a' && p_c <= 'f') {
		return p_c - 'a' + 10;
	}
	if (p_c >= 'A' && p_c <= 'F') {
		return p_c - 'A' + 10;
	}
	return -1;
}

bool is_letter(char p_c) {
	return (p_c >= 'a' && p_c <= 'z') || (p_c >= 'A' && p_c <= 'Z');
}

} // namespace

std::string path_from_uri(std::string_view p_uri, PathStyle p_style) {
	const std::string_view scheme = "file://";
	if (p_uri.compare(0, scheme.size(), scheme) != 0) {
		return std::string();
	}
	// After the scheme comes a host, usually empty, and then the path.
	size_t slash = p_uri.find('/', scheme.size());
	if (slash == std::string_view::npos) {
		return std::string();
	}
	// A host other than this machine names a share: "file://server/share/a.as" is "//server/share/a.as".
	const std::string_view host = p_uri.substr(scheme.size(), slash - scheme.size());
	std::string path = host.empty() || host == "localhost" ? std::string() : "//" + std::string(host);
	for (size_t i = slash; i < p_uri.size(); i++) {
		if (p_uri[i] == '%' && i + 2 < p_uri.size() + 0 && hex_value(p_uri[i + 1]) >= 0 && hex_value(p_uri[i + 2]) >= 0) {
			path += (char)(hex_value(p_uri[i + 1]) * 16 + hex_value(p_uri[i + 2]));
			i += 2;
		} else {
			path += p_uri[i];
		}
	}
	// "/C:/dir" is the Windows path "C:/dir".
	if (p_style == PathStyle::Windows && path.size() >= 3 && path[0] == '/' && is_letter(path[1]) && path[2] == ':') {
		path.erase(0, 1);
	}
	return generic_path(path, p_style);
}

std::string uri_from_path(std::string_view p_path, PathStyle p_style) {
	static const char *const HEX = "0123456789ABCDEF";
	const std::string generic = generic_path(p_path, p_style);
	std::string_view path = generic;
	std::string uri = "file://";
	if (p_style == PathStyle::Windows && path.compare(0, 2, "//") == 0) {
		path.remove_prefix(2); // A share: its server is the URI's host.
	} else if (path.empty() || path[0] != '/') {
		uri += '/';
	}
	const bool drive = p_style == PathStyle::Windows && path.size() >= 2 && is_letter(path[0]) && path[1] == ':';
	for (size_t i = 0; i < path.size(); i++) {
		unsigned char c = (unsigned char)path[i];
		bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' ||
				c == '.' || c == '_' || c == '~' || (c == ':' && i == 1 && drive);
		if (plain) {
			uri += (char)c;
		} else {
			uri += '%';
			uri += HEX[c >> 4];
			uri += HEX[c & 15];
		}
	}
	return uri;
}

} // namespace kas3
