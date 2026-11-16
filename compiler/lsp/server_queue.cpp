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
			// A later edit of the document does not stop the request: LSP 3.17 says a server should not send
			// ContentModified for a change it sees only in its unprocessed messages. It is answered for its text.
			const std::string path = document_key(message.get("params").get("textDocument").get("uri").string());
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
				if (method == "textDocument/didClose" && !path.empty() &&
						document_key(params.get("textDocument").get("uri").string()) == path) {
					code = -32801; // Its document is gone before it is answered.
				}
			}
		}
		if (code != 0) {
			r_out.push_back(write_json(Json::object().set("jsonrpc", "2.0").set("id", message.get("id"))
					.set("error", Json::object().set("code", code).set("message",
							code == -32800 ? "request cancelled before execution" : "document closed before request execution"))));
		} else if (valid[i]) {
			handle_message(message, r_out);
		} else {
			handle(p_messages[i], r_out);
		}
	}
}

} // namespace kas3
