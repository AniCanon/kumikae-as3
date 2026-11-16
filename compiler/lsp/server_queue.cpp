#include "server.h"
#include "uri.h"

namespace kas3 {
namespace {

bool cancellable(const std::string &p_method) {
	return p_method == "textDocument/completion" || p_method == "textDocument/signatureHelp" ||
			p_method == "textDocument/hover" || p_method == "textDocument/definition" ||
			p_method == "textDocument/references" || p_method == "textDocument/documentSymbol" ||
			p_method == "workspace/symbol";
}

} // namespace

void Server::handle_batch(const std::vector<std::string> &p_messages, std::vector<std::string> &r_out) {
	std::vector<Json> messages(p_messages.size());
	std::vector<bool> valid(p_messages.size());
	for (size_t i = 0; i < messages.size(); ++i) {
		valid[i] = parse_json(p_messages[i], &messages[i]);
		if (!valid[i]) {
			messages[i] = Json();
		}
	}
	for (size_t i = 0; i < messages.size() && !exited_; ++i) {
		const Json &message = messages[i];
		int code = 0;
		if (initialized_ && !shutdown_ && message.has("id") && cancellable(message.get("method").string())) {
			const std::string path = document_key(message.get("params").get("textDocument").get("uri").string());
			const auto version = versions_.find(path);
			for (size_t j = i + 1; j < messages.size(); ++j) {
				const Json &later = messages[j];
				const std::string &method = later.get("method").string();
				if (later.has("id")) {
					continue;
				}
				const Json &params = later.get("params");
				if (method == "$/cancelRequest" && write_json(params.get("id")) == write_json(message.get("id"))) {
					code = -32800;
					break;
				}
				const Json &document = params.get("textDocument");
				const auto later_version = valid_version(document);
				if (!path.empty() && document_key(document.get("uri").string()) == path &&
						(method == "textDocument/didClose" || (method == "textDocument/didChange" &&
						version != versions_.end() && later_version && *later_version > version->second))) {
					code = -32801;
				}
			}
		}
		if (code != 0) {
			r_out.push_back(write_json(Json::object().set("jsonrpc", "2.0").set("id", message.get("id"))
					.set("error", Json::object().set("code", code).set("message",
							code == -32800 ? "request cancelled before execution" : "document changed before request execution"))));
		} else if (valid[i]) {
			handle_message(message, r_out);
		} else {
			handle(p_messages[i], r_out);
		}
	}
}

} // namespace kas3
