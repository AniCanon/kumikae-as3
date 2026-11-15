#include "server.h"

#include "positions.h"
#include "uri.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

#ifdef _WIN32
#include <io.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace kas3 {

namespace {

// JSON-RPC and LSP error codes.
const int PARSE_ERROR = -32700;
const int INVALID_REQUEST = -32600;
const int METHOD_NOT_FOUND = -32601;
const int NOT_INITIALIZED = -32002;

// The most a single message may hold.
const size_t MAX_MESSAGE = 256u * 1024 * 1024;

std::string response(const Json &p_id, Json p_result) {
	return write_json(Json::object().set("jsonrpc", "2.0").set("id", p_id).set("result", std::move(p_result)));
}

std::string failure(const Json &p_id, int p_code, const char *p_message) {
	return write_json(Json::object().set("jsonrpc", "2.0").set("id", p_id).set("error",
			Json::object().set("code", p_code).set("message", p_message)));
}

bool is_name_byte(unsigned char p_c) {
	return (p_c >= 'a' && p_c <= 'z') || (p_c >= 'A' && p_c <= 'Z') || (p_c >= '0' && p_c <= '9') || p_c == '_' ||
			p_c == '$' || p_c >= 0x80;
}

// How much of the text an error at this offset is about: the name, number or string there, or else one
// character. Nothing at the end of a line.
uint32_t extent(const std::string &p_text, uint32_t p_offset) {
	if (p_offset >= p_text.size() || p_text[p_offset] == '\n' || p_text[p_offset] == '\r') {
		return 0;
	}
	uint32_t end = p_offset;
	char quote = p_text[p_offset];
	if (quote == '"' || quote == '\'') {
		// To the closing quote, if the line has one.
		for (end = p_offset + 1; end < p_text.size() && p_text[end] != '\n' && p_text[end] != '\r'; end++) {
			if (p_text[end] == '\\') {
				end++;
			} else if (p_text[end] == quote) {
				return end + 1 - p_offset;
			}
		}
		return 1;
	}
	while (end < p_text.size() && is_name_byte((unsigned char)p_text[end])) {
		end++;
	}
	if (end > p_offset) {
		return end - p_offset;
	}
	// One character, which may be several bytes.
	end = p_offset + 1;
	while (end < p_text.size() && ((unsigned char)p_text[end] & 0xC0) == 0x80) {
		end++;
	}
	return end - p_offset;
}

int symbol_kind(SymbolKind p_kind, bool p_const, bool p_constructor) {
	switch (p_kind) {
		case SymbolKind::Class:
			return 5;
		case SymbolKind::Interface:
			return 11;
		case SymbolKind::Method:
			return p_constructor ? 9 : 6;
		case SymbolKind::Property:
			return 7;
		case SymbolKind::Field:
			return p_const ? 14 : 8;
		case SymbolKind::Function:
		case SymbolKind::LocalFunction:
			return 12;
		default:
			return p_const ? 14 : 13;
	}
}

} // namespace

Server::Server(Workspace::Options p_options) :
		workspace_(std::move(p_options)) {}

std::string Server::uri_of(const Unit &p_unit) {
	std::string path = workspace_.real_path(p_unit);
	if (path.empty()) {
		return std::string();
	}
	auto known = uris_.find(path);
	return known != uris_.end() ? known->second : uri_from_path(workspace_.shown_path(path));
}

Unit *Server::unit_of(const Json &p_params) {
	std::string path = path_from_uri(p_params.get("textDocument").get("uri").string());
	return path.empty() ? nullptr : workspace_.unit(path);
}

Json Server::position(const Unit &p_unit, uint32_t p_offset) const {
	Position at = p_unit.source.position(p_offset);
	std::string_view line = p_unit.source.line_text(at.line);
	int character = character_at(line, (size_t)(at.column - 1), utf8_);
	if (at.line == 1) {
		character += mark_width(p_unit.source);
	}
	return Json::object().set("line", at.line - 1).set("character", character);
}

Json Server::range(const Unit &p_unit, uint32_t p_offset, uint32_t p_length) const {
	return Json::object().set("start", position(p_unit, p_offset)).set("end", position(p_unit, p_offset + p_length));
}

Json Server::location(const Place &p_place) {
	if (!p_place.unit) {
		return Json();
	}
	std::string uri = uri_of(*p_place.unit);
	if (uri.empty()) {
		return Json();
	}
	return Json::object().set("uri", uri).set("range", range(*p_place.unit, p_place.offset, p_place.length));
}

uint32_t Server::offset_at(const Unit &p_unit, const Json &p_position) const {
	return offset_in(p_unit.source, p_position);
}

uint32_t Server::offset_in(const Source &p_source, const Json &p_position) const {
	int64_t line = p_position.get("line").integer(-1);
	int64_t character = p_position.get("character").integer(0);
	const Source &source = p_source;
	if (line < 0) {
		return 0;
	}
	if (line >= source.line_count()) {
		return (uint32_t)source.text().size();
	}
	if (line == 0) {
		character -= mark_width(source);
	}
	return source.line_start((int)line + 1) + bytes_before(source.line_text((int)line + 1), character, utf8_);
}

// What the editor counts before the first line's text: the byte-order mark it sent, if it sent one. A
// file it has not opened is counted without: an editor shows a file from disk without its mark.
int Server::mark_width(const Source &p_source) const {
	if (!p_source.has_mark() || !workspace_.sent_with_mark(p_source.path())) {
		return 0;
	}
	return utf8_ ? (int)BYTE_ORDER_MARK.size() : 1;
}

const Occurrence *Server::occurrence_at(const Json &p_params, Unit **r_unit) {
	Unit *unit = unit_of(p_params);
	if (!unit || !p_params.get("position").is_object()) {
		return nullptr;
	}
	*r_unit = unit;
	return workspace_.queries().at(*unit, offset_at(*unit, p_params.get("position")));
}

void Server::publish(std::vector<std::string> &r_out) {
	if (workspace_.stale() || workspace_.checks() == published_check_) {
		return;
	}
	published_check_ = workspace_.checks();
	std::set<std::string> present;
	auto send = [&](const std::string &p_uri, const std::string &p_diagnostics) {
		// The list is JSON already.
		r_out.push_back("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":" +
				write_json(Json(p_uri)) + ",\"diagnostics\":" + p_diagnostics + "}}");
	};
	for (Unit *unit : workspace_.reported_units()) {
		std::string uri = uri_of(*unit);
		if (uri.empty()) {
			continue;
		}
		present.insert(uri);
		auto before = published_.find(uri);
		if (!unit->diagnostics.has_errors()) {
			if (before != published_.end()) {
				send(uri, "[]");
				published_.erase(before);
			}
			continue;
		}
		Json list = Json::array();
		for (const Diagnostic &item : unit->diagnostics.items()) {
			list.push(Json::object()
							  .set("range", range(*unit, item.offset, extent(unit->source.text(), item.offset)))
							  .set("severity", item.warning ? 2 : 1)
							  .set("source", "kas3c")
							  .set("message", item.message));
		}
		int dropped = unit->diagnostics.dropped();
		if (dropped > 0) {
			list.push(Json::object()
							  .set("range", range(*unit, 0, 0))
							  .set("severity", 1)
							  .set("source", "kas3c")
							  .set("message", "too many errors (" + std::to_string(dropped) + " more)"));
		}
		std::string text = write_json(list);
		if (before == published_.end() || before->second != text) {
			send(uri, text);
			published_[uri] = std::move(text);
		}
	}
	// Files that left the program take their errors with them.
	for (auto item = published_.begin(); item != published_.end();) {
		if (present.count(item->first)) {
			++item;
		} else {
			send(item->first, "[]");
			item = published_.erase(item);
		}
	}
}

void Server::settle(std::vector<std::string> &r_out) {
	if (!initialized_ || exited_) {
		return;
	}
	workspace_.compilation();
	if (workspace_.cut_off() && !warned_cut_off_) {
		warned_cut_off_ = true;
		std::string text = "Kumikae AS3: this project has more than " + std::to_string(workspace_.max_files()) +
				" .as files. The rest are not read, so names they define are reported as unknown.";
		r_out.push_back(write_json(Json::object().set("jsonrpc", "2.0").set("method", "window/showMessage").set("params",
				Json::object().set("type", 2).set("message", text))));
	}
	publish(r_out);
}

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

Json Server::definition(const Json &p_params) {
	Unit *unit = unit_of(p_params);
	if (!unit || !p_params.get("position").is_object()) {
		return Json();
	}
	Json where = location(workspace_.queries().definition(*unit, offset_at(*unit, p_params.get("position"))));
	// A list of one. The protocol allows a place on its own, and VS Code reads it, but the client
	// library JetBrains' IDEs use reads only a list: with a bare place, go to definition does nothing.
	return where.is_null() ? Json() : Json::array().push(std::move(where));
}

Json Server::references(const Json &p_params) {
	Json list = Json::array();
	Unit *unit = nullptr;
	const Occurrence *found = occurrence_at(p_params, &unit);
	if (!found) {
		return list;
	}
	bool with_declaration = p_params.get("context").get("includeDeclaration").boolean();
	for (const Place &place : workspace_.queries().references(found->symbol, with_declaration)) {
		Json where = location(place);
		if (!where.is_null()) {
			list.push(std::move(where));
		}
	}
	return list;
}

Json Server::hover(const Json &p_params) {
	Unit *unit = nullptr;
	const Occurrence *found = occurrence_at(p_params, &unit);
	if (!found) {
		return Json();
	}
	Queries &queries = workspace_.queries();
	std::string text = "```" + code_language_ + "\n" + queries.describe(found->symbol) + "\n```\n" + queries.context(found->symbol);
	return Json::object()
			.set("contents", Json::object().set("kind", "markdown").set("value", text))
			.set("range", range(*unit, found->offset, found->length));
}

Json Server::document_symbols(const Json &p_params) {
	Json list = Json::array();
	Unit *unit = unit_of(p_params);
	if (!unit) {
		return list;
	}
	struct Convert {
		const Server &server;
		const Unit &unit;
		Json operator()(const OutlineItem &p_item) const {
			Json symbol = Json::object();
			symbol.set("name", p_item.name);
			symbol.set("kind", symbol_kind(p_item.kind, p_item.is_const, p_item.is_constructor));
			symbol.set("range", server.range(unit, p_item.offset, p_item.end - p_item.offset));
			symbol.set("selectionRange", server.range(unit, p_item.name_offset, p_item.name_length));
			if (!p_item.children.empty()) {
				Json children = Json::array();
				for (const OutlineItem &child : p_item.children) {
					children.push((*this)(child));
				}
				symbol.set("children", std::move(children));
			}
			return symbol;
		}
	};
	Convert convert{ *this, *unit };
	for (const OutlineItem &item : workspace_.queries().outline(*unit)) {
		list.push(convert(item));
	}
	return list;
}

Json Server::workspace_symbols(const Json &p_params) {
	Json list = Json::array();
	for (const FoundSymbol &found : workspace_.queries().find(p_params.get("query").string(), 200)) {
		Place place;
		place.unit = found.unit;
		place.offset = found.offset;
		place.length = found.length;
		Json where = location(place);
		if (where.is_null()) {
			continue;
		}
		Json symbol = Json::object();
		symbol.set("name", found.name);
		symbol.set("kind", symbol_kind(found.kind, found.is_const, found.is_constructor));
		symbol.set("location", std::move(where));
		if (!found.container.empty()) {
			symbol.set("containerName", found.container);
		}
		list.push(std::move(symbol));
	}
	return list;
}

void Server::notification(const std::string &p_method, const Json &p_params, std::vector<std::string> &r_out) {
	const Json &document = p_params.get("textDocument");
	std::string path = path_from_uri(document.get("uri").string());
	if (p_method == "initialized") {
		if (watch_files_) {
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
	} else if (p_method == "textDocument/didOpen") {
		if (!path.empty() && document.get("text").is_string()) {
			uris_[workspace_.resolve(path)] = document.get("uri").string();
			workspace_.open(path, document.get("text").string());
		}
	} else if (p_method == "textDocument/didChange") {
		// Each change applies to the text the one before it left: a range of it replaced, or, with no
		// range, the whole of it.
		const std::string *held = path.empty() ? nullptr : workspace_.text(path);
		const Json &changes = p_params.get("contentChanges");
		bool any = false;
		std::string text = held ? *held : std::string();
		if (held && workspace_.sent_with_mark(path)) {
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
		if (!path.empty() && any) {
			workspace_.change(path, std::move(text));
		}
	} else if (p_method == "textDocument/didClose") {
		if (!path.empty()) {
			workspace_.close(path);
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
	if (!message.is_object()) {
		r_out.push_back(failure(Json(), INVALID_REQUEST, "the message is not a request"));
		return;
	}
	const Json &id = message.get("id");
	const Json &params = message.get("params");
	if (!message.get("method").is_string()) {
		if (!message.has("result") && !message.has("error")) {
			r_out.push_back(failure(id, INVALID_REQUEST, "the message has no method"));
		}
		return; // An answer to something the server asked: nothing depends on it.
	}
	const std::string &method = message.get("method").string();
	if (!message.has("id")) {
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
	Json result;
	if (method == "textDocument/definition") {
		result = definition(params);
	} else if (method == "textDocument/references") {
		result = references(params);
	} else if (method == "textDocument/hover") {
		result = hover(params);
	} else if (method == "textDocument/signatureHelp") {
		result = signature_help(params);
	} else if (method == "textDocument/completion") {
		result = completion(params);
	} else if (method == "textDocument/documentSymbol") {
		result = document_symbols(params);
	} else if (method == "workspace/symbol") {
		result = workspace_symbols(params);
	} else {
		r_out.push_back(failure(id, METHOD_NOT_FOUND, "the server does not handle this request"));
		return;
	}
	r_out.push_back(response(id, std::move(result)));
	// Answering made the server check the latest text: its errors are due as well.
	publish(r_out);
}

namespace {

class Channel {
	int input_;
	int output_;
	std::string buffer_;
	bool ended_ = false;

	bool fill() {
		char chunk[65536];
#ifdef _WIN32
		int got = _read(input_, chunk, sizeof(chunk));
#else
		ssize_t got = read(input_, chunk, sizeof(chunk));
#endif
		if (got <= 0) {
			ended_ = true;
			return false;
		}
		buffer_.append(chunk, (size_t)got);
		return true;
	}

public:
	Channel(int p_input, int p_output) :
			input_(p_input), output_(p_output) {}

	// The next message's body. False at the end of input, or when the framing is broken past repair.
	bool next(std::string *r_message) {
		while (true) {
			size_t header_end = buffer_.find("\r\n\r\n");
			if (header_end == std::string::npos) {
				if (buffer_.size() > 65536 || !fill()) {
					return false; // No header in sight.
				}
				continue;
			}
			size_t length = MAX_MESSAGE + 1;
			size_t at = 0;
			while (at < header_end) {
				size_t line_end = buffer_.find("\r\n", at);
				std::string line = buffer_.substr(at, line_end - at);
				const std::string name = "content-length:";
				bool matches = line.size() > name.size();
				for (size_t i = 0; matches && i < name.size(); i++) {
					char c = line[i];
					matches = (c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c) == name[i];
				}
				if (matches) {
					size_t digits = name.size();
					while (digits < line.size() && line[digits] == ' ') {
						digits++;
					}
					size_t value = 0;
					bool any = false;
					while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9' && value <= MAX_MESSAGE) {
						value = value * 10 + (size_t)(line[digits++] - '0');
						any = true;
					}
					while (digits < line.size() && (line[digits] == ' ' || line[digits] == '\t')) {
						digits++;
					}
					if (any && digits == line.size()) {
						length = value;
					}
				}
				at = line_end + 2;
			}
			if (length > MAX_MESSAGE) {
				return false; // Without a length there is no telling where the message ends.
			}
			size_t body = header_end + 4;
			while (buffer_.size() < body + length) {
				if (!fill()) {
					return false;
				}
			}
			r_message->assign(buffer_, body, length);
			buffer_.erase(0, body + length);
			return true;
		}
	}

	// True if another message, or part of one, is already waiting.
	bool waiting() const {
		if (!buffer_.empty()) {
			return true;
		}
#ifdef _WIN32
		return false;
#else
		struct pollfd descriptor;
		descriptor.fd = input_;
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		return poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN) != 0;
#endif
	}

	bool send(const std::vector<std::string> &p_messages) {
		std::string out;
		for (const std::string &message : p_messages) {
			out += "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + message;
		}
		size_t sent = 0;
		while (sent < out.size()) {
#ifdef _WIN32
			int wrote = _write(output_, out.data() + sent, (unsigned)(out.size() - sent));
#else
			ssize_t wrote = write(output_, out.data() + sent, out.size() - sent);
#endif
			if (wrote <= 0) {
				return false;
			}
			sent += (size_t)wrote;
		}
		return true;
	}
};

} // namespace

int run_server(int p_input, int p_output, Workspace::Options p_options) {
	Server server(std::move(p_options));
	Channel channel(p_input, p_output);
	std::string message;
	std::vector<std::string> out;
	while (!server.exited() && channel.next(&message)) {
		out.clear();
		server.handle(message, out);
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
