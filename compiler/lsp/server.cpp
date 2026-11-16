#include "server.h"

#include "channel.h"
#include "positions.h"
#include "uri.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace kas3 {

namespace {

// JSON-RPC and LSP error codes.
const int PARSE_ERROR = -32700;
const int INVALID_REQUEST = -32600;
const int METHOD_NOT_FOUND = -32601;
const int NOT_INITIALIZED = -32002;

std::string response(const Json &p_id, Json p_result) {
	return write_json(Json::object().set("jsonrpc", "2.0").set("id", p_id).set("result", std::move(p_result)));
}

std::string failure(const Json &p_id, int p_code, const char *p_message) {
	return write_json(Json::object().set("jsonrpc", "2.0").set("id", p_id).set("error",
			Json::object().set("code", p_code).set("message", p_message)));
}

} // namespace

Server::Server(Workspace::Options p_options) :
		workspace_(std::move(p_options)) {}

Json Server::initialize(const Json &p_params) {
	const Json &capabilities = p_params.get("capabilities");
	const Json &completion_item = capabilities.get("textDocument").get("completion").get("completionItem");
	for (const Json &item : capabilities.get("textDocument").get("completion").get("completionList").get("itemDefaults").items()) {
		default_edit_range_ = default_edit_range_ || item.string() == "editRange";
	}
	snippets_ = completion_item.get("snippetSupport").boolean();
	insert_replace_ = completion_item.get("insertReplaceSupport").boolean();
	for (const Json &encoding : capabilities.get("general").get("positionEncodings").items()) {
		utf8_ = utf8_ || encoding.string() == "utf-8";
	}
	diagnostic_versions_ = capabilities.get("textDocument").get("publishDiagnostics").get("versionSupport").boolean();
	watch_files_ = capabilities.get("workspace").get("didChangeWatchedFiles").get("dynamicRegistration").boolean();
	const std::string language = p_params.get("initializationOptions").get("codeLanguage").string();
	// A name of letters and digits only: it goes into Markdown as it is.
	if (!language.empty() && std::all_of(language.begin(), language.end(), [](char p_c) { return std::isalnum(static_cast<unsigned char>(p_c)) != 0; })) {
		code_language_ = language;
	}
	std::string root = path_from_uri(p_params.get("workspaceFolders").at(0).get("uri").string());
	if (root.empty()) {
		root = path_from_uri(p_params.get("rootUri").string());
	}
	if (root.empty()) {
		root = p_params.get("rootPath").string();
	}
	if (!root.empty()) {
		workspace_.set_root(root);
	}
	initialized_ = true;
	Json provided = Json::object();
	provided.set("positionEncoding", utf8_ ? "utf-8" : "utf-16");
	provided.set("textDocumentSync", Json::object().set("openClose", true).set("change", 2)); // The ranges that changed.
	provided.set("definitionProvider", true);
	provided.set("referencesProvider", true);
	provided.set("hoverProvider", true);
	provided.set("completionProvider", Json::object().set("triggerCharacters", Json::array().push(".")));
	provided.set("signatureHelpProvider", Json::object().set("triggerCharacters", Json::array().push("(").push(",")));
	provided.set("documentSymbolProvider", true);
	provided.set("workspaceSymbolProvider", true);
	return Json::object().set("capabilities", provided).set("serverInfo",
			Json::object().set("name", "kas3c").set("version", "0.1.0"));
}

// Asks the editor to say when files change on disk.
void Server::register_file_watcher(std::vector<std::string> &r_out) {
	Json watchers = Json::array().push(Json::object().set("globPattern", "**/*"));
	Json registration = Json::object()
								.set("id", "kas3c-watch")
								.set("method", "workspace/didChangeWatchedFiles")
								.set("registerOptions", Json::object().set("watchers", std::move(watchers)));
	r_out.push_back(write_json(Json::object()
									   .set("jsonrpc", "2.0")
									   .set("id", "kas3c-" + std::to_string(next_request_++))
									   .set("method", "client/registerCapability")
									   .set("params", Json::object().set("registrations", Json::array().push(std::move(registration))))));
}

void Server::did_change(const std::string &p_path, const Json &p_document, const Json &p_params,
		std::vector<std::string> &r_out) {
	if (p_path.empty() || !accept_version(p_document, false, r_out)) {
		return;
	}
	// Each change applies to the text the one before it left: a range of it replaced, or, with no
	// range, the whole of it.
	const std::string *held = p_path.empty() ? nullptr : workspace_.text(p_path);
	const Json &changes = p_params.get("contentChanges");
	bool any = false;
	std::string text = held ? *held : std::string();
	if (held && workspace_.sent_with_mark(p_path)) {
		text.insert(0, BYTE_ORDER_MARK); // Changed as the editor has it: the workspace keeps the mark out of the text.
	}
	for (const Json &change : changes.items()) {
		if (!change.get("text").is_string()) {
			continue;
		}
		const Json &range = change.get("range");
		if (!range.is_object()) {
			text = change.get("text").string();
			held = &text; // From here on there is a whole text to change.
		} else if (!held) {
			continue; // A piece of a document the server was never given.
		} else {
			auto offset = [&](const Json &p_position) {
				return offset_in_text(text, p_position.get("line").integer(-1), p_position.get("character").integer(0), utf8_);
			};
			uint32_t from = offset(range.get("start"));
			uint32_t to = offset(range.get("end"));
			if (to < from) {
				std::swap(from, to);
			}
			text.replace(from, to - from, change.get("text").string());
		}
		any = true;
	}
	if (!p_path.empty() && any) {
		workspace_.change(p_path, std::move(text));
	}
}

void Server::did_close(const std::string &p_path) {
	const std::string key = workspace_.resolve(p_path);
	workspace_.close(p_path);
	versions_.erase(key);
	++document_generation_;
	uris_.erase(key);
}

void Server::notification(const std::string &p_method, const Json &p_params, std::vector<std::string> &r_out) {
	const Json &document = p_params.get("textDocument");
	std::string path = path_from_uri(document.get("uri").string());
	if (p_method == "initialized") {
		if (watch_files_) {
			register_file_watcher(r_out);
		}
	} else if (p_method == "textDocument/didOpen") {
		if (!path.empty() && document.get("text").is_string() && accept_version(document, true, r_out)) {
			uris_[workspace_.resolve(path)] = document.get("uri").string();
			workspace_.open(path, document.get("text").string());
		}
	} else if (p_method == "textDocument/didChange") {
		did_change(path, document, p_params, r_out);
	} else if (p_method == "textDocument/didClose") {
		if (!path.empty()) {
			did_close(path);
		}
	} else if (p_method == "workspace/didChangeWatchedFiles") {
		for (const Json &change : p_params.get("changes").items()) {
			std::string changed = path_from_uri(change.get("uri").string());
			if (!changed.empty()) {
				workspace_.file_changed_on_disk(changed);
			}
		}
	}
	// Everything else is something this server has no use for.
}

void Server::handle(std::string_view p_message, std::vector<std::string> &r_out) {
	if (exited_) {
		return;
	}
	Json message;
	if (!parse_json(p_message, &message)) {
		r_out.push_back(failure(Json(), PARSE_ERROR, "the message is not JSON"));
		return;
	}
	handle_message(message, r_out);
}

// The answer to a request after `initialize`, or nothing for a method the server does not handle.
std::optional<Json> Server::answer(const std::string &p_method, const Json &p_params) {
	const Handler handler = handler_of(p_method);
	if (!handler) {
		return std::nullopt;
	}
	return (this->*handler)(p_params);
}

Server::Handler Server::handler_of(std::string_view p_method) {
	static constexpr std::pair<std::string_view, Handler> HANDLERS[] = {
		{ "textDocument/definition", &Server::definition },
		{ "textDocument/references", &Server::references },
		{ "textDocument/hover", &Server::hover },
		{ "textDocument/signatureHelp", &Server::signature_help },
		{ "textDocument/completion", &Server::completion },
		{ "textDocument/documentSymbol", &Server::document_symbols },
		{ "workspace/symbol", &Server::workspace_symbols },
	};
	for (const auto &[method, handler] : HANDLERS) {
		if (method == p_method) {
			return handler;
		}
	}
	return nullptr;
}

void Server::handle_message(const Json &p_message, std::vector<std::string> &r_out) {
	if (!p_message.is_object()) {
		r_out.push_back(failure(Json(), INVALID_REQUEST, "the message is not a request"));
		return;
	}
	const Json &id = p_message.get("id");
	const Json &params = p_message.get("params");
	if (!p_message.get("method").is_string()) {
		if (!p_message.has("result") && !p_message.has("error")) {
			r_out.push_back(failure(id, INVALID_REQUEST, "the message has no method"));
		}
		return; // An answer to something the server asked: nothing depends on it.
	}
	const std::string &method = p_message.get("method").string();
	if (!p_message.has("id")) {
		if (method == "exit") {
			exited_ = true;
			exit_code_ = shutdown_ ? 0 : 1;
		} else if (initialized_ && !shutdown_) {
			notification(method, params, r_out);
		}
		return;
	}
	if (method == "initialize") {
		if (initialized_) {
			r_out.push_back(failure(id, INVALID_REQUEST, "initialize was already sent"));
		} else {
			r_out.push_back(response(id, initialize(params)));
		}
		return;
	}
	if (!initialized_) {
		r_out.push_back(failure(id, NOT_INITIALIZED, "initialize has not been sent"));
		return;
	}
	if (shutdown_) {
		r_out.push_back(failure(id, INVALID_REQUEST, "the server is shutting down"));
		return;
	}
	if (method == "shutdown") {
		shutdown_ = true;
		r_out.push_back(response(id, Json()));
		return;
	}
	std::optional<Json> result = answer(method, params);
	if (!result) {
		r_out.push_back(failure(id, METHOD_NOT_FOUND, "the server does not handle this request"));
		return;
	}
	r_out.push_back(response(id, std::move(*result)));
	// Answering made the server check the latest text: its errors are due as well.
	publish(r_out);
}

int run_server(int p_input, int p_output, Workspace::Options p_options) {
	Server server(std::move(p_options));
	Channel channel(p_input, p_output);
	std::string message;
	std::vector<std::string> out;
	while (!server.exited() && channel.next(&message)) {
		out.clear();
		std::vector<std::string> batch;
		size_t bytes = message.size();
		batch.push_back(std::move(message));
		// Bound lookahead memory and work; do not wait for a quiet editor to answer.
		while (batch.size() < 32 && bytes < 1024 * 1024 && channel.waiting() && channel.next(&message, false)) {
			bytes += message.size();
			batch.push_back(std::move(message));
		}
		server.handle_batch(batch, out);
		// While someone is typing, changes arrive faster than they can be checked. Check once the editor
		// pauses.
		if (!server.exited() && !channel.waiting()) {
			server.settle(out);
		}
		if (!channel.send(out)) {
			return 1;
		}
	}
	return server.exited() ? server.exit_code() : 1;
}

} // namespace kas3
