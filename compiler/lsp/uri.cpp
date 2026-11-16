#include "uri.h"

#include <filesystem>
#include <algorithm>

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

std::string path_from_uri(std::string_view p_uri) {
	const std::string_view scheme = "file://";
	if (p_uri.compare(0, scheme.size(), scheme) != 0) {
		return std::string();
	}
	// After the scheme comes a host, usually empty, and then the path.
	size_t slash = p_uri.find('/', scheme.size());
	if (slash == std::string_view::npos) {
		return std::string();
	}
	const auto host = p_uri.substr(scheme.size(), slash - scheme.size());
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
	if (path.size() >= 3 && path[0] == '/' && is_letter(path[1]) && path[2] == ':') {
		path.erase(0, 1);
	}
	return path.find('\0') == std::string::npos ? path : std::string();
}

std::string normal_path(std::string_view p_path) {
	std::string path(p_path);
#ifdef _WIN32
	std::replace(path.begin(), path.end(), '\\', '/');
	if (path.size() > 1 && path[1] == ':' && path[0] >= 'a' && path[0] <= 'z') {
		path[0] = static_cast<char>(path[0] - 'a' + 'A');
	}
#endif
	const bool network = path.compare(0, 2, "//") == 0;
	path = std::filesystem::u8path(path).lexically_normal().generic_u8string();
	if (network && path.compare(0, 2, "//") != 0) {
		path.insert(path.begin(), '/');
	}
	return path;
}

std::string uri_from_path(std::string_view p_path) {
	constexpr char HEX[] = "0123456789ABCDEF";
	std::string portable(p_path);
	const bool drive = portable.size() > 2 && is_letter(portable[0]) && portable[1] == ':';
	const bool backslash_network = portable.compare(0, 2, "\\\\") == 0;
	if (drive || backslash_network) {
		std::replace(portable.begin(), portable.end(), '\\', '/');
	}
	p_path = portable;
	std::string uri = "file://";
	const bool network = p_path.compare(0, 2, "//") == 0;
	if (network) {
		p_path.remove_prefix(2);
	}
	if (!network && (p_path.empty() || p_path[0] != '/')) {
		uri += '/';
	}
	for (size_t i = 0; i < p_path.size(); i++) {
		unsigned char c = (unsigned char)p_path[i];
		bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' ||
				c == '.' || c == '_' || c == '~' || (c == ':' && i == 1);
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
