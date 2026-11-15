#include "server.h"
#include "uri.h"

#include <limits>

namespace kas3 {

std::optional<int64_t> Server::valid_version(const Json &p_document) {
	const Json &value = p_document.get("version");
	const int64_t version = value.integer(std::numeric_limits<int64_t>::min());
	return value.is_number() && version >= std::numeric_limits<int32_t>::min() &&
			version <= std::numeric_limits<int32_t>::max() ? std::optional<int64_t>(version) : std::nullopt;
}

bool Server::accept_version(const Json &p_document, bool p_open, std::vector<std::string> &r_out) {
	const auto version = valid_version(p_document);
	const std::string path = normal_path(path_from_uri(p_document.get("uri").string()));
	const auto previous = versions_.find(path);
	if (!version || (!p_open && previous != versions_.end() && *version <= previous->second)) {
		r_out.push_back(write_json(Json::object().set("jsonrpc", "2.0").set("method", "window/logMessage")
				.set("params", Json::object().set("type", 2).set("message",
						version ? "ignored stale document change" : "ignored document with an invalid version"))));
		return false;
	}
	versions_[path] = *version;
	++document_generation_;
	return true;
}

std::optional<int64_t> Server::document_version(const std::string &p_uri) const {
	if (!diagnostic_versions_) {
		return std::nullopt;
	}
	const auto found = versions_.find(normal_path(path_from_uri(p_uri)));
	return found == versions_.end() ? std::nullopt : std::optional<int64_t>(found->second);
}

} // namespace kas3
