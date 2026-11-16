// The errors the editor is sent: what changed since it was last told, after a check.
#include "server.h"

#include <set>

namespace kas3 {

namespace {

bool is_name_byte(unsigned char p_c) {
	return (p_c >= 'a' && p_c <= 'z') || (p_c >= 'A' && p_c <= 'Z') || (p_c >= '0' && p_c <= '9') || p_c == '_' ||
			p_c == '$' || p_c >= 0x80;
}

// How much of the text an error at this offset is about: the name, number or string there, or else one
// character. Nothing at the end of a line.
uint32_t extent(std::string_view p_text, uint32_t p_offset) {
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

} // namespace

// The file's errors as the protocol's list of diagnostics, written out.
std::string Server::diagnostics_json(const Unit &p_unit) const {
	Json list = Json::array();
	for (const Diagnostic &item : p_unit.diagnostics.items()) {
		list.push(Json::object()
						  .set("range", range(p_unit, item.offset, extent(p_unit.source.text(), item.offset)))
						  .set("severity", item.warning ? 2 : 1)
						  .set("source", "kas3c")
						  .set("message", item.message));
	}
	int dropped = p_unit.diagnostics.dropped();
	if (dropped > 0) {
		list.push(Json::object()
						  .set("range", range(p_unit, 0, 0))
						  .set("severity", 1)
						  .set("source", "kas3c")
						  .set("message", "too many errors (" + std::to_string(dropped) + " more)"));
	}
	return write_json(list);
}

Json diagnostics_message(const std::string &p_uri, Json p_diagnostics) {
	return Json::object()
			.set("jsonrpc", "2.0")
			.set("method", "textDocument/publishDiagnostics")
			.set("params", Json::object().set("uri", p_uri).set("diagnostics", std::move(p_diagnostics)));
}

// An empty list of suggestions says nothing: a file that is not the program's says why, on its first word.
void Server::tell_outside(const std::string &p_path, const std::string &p_uri, std::vector<std::string> &r_out) {
	const std::string folder = workspace_.dot_folder(workspace_.resolve(p_path));
	if (folder.empty()) {
		return;
	}
	const auto place = [](int p_character) { return Json::object().set("line", 0).set("character", p_character); };
	outside_.insert(p_uri);
	r_out.push_back(write_json(diagnostics_message(p_uri,
			Json::array().push(Json::object()
									   .set("range", Json::object().set("start", place(0)).set("end", place(7)))
									   .set("severity", 2)
									   .set("source", "kas3c")
									   .set("message", "this file is not part of the program: it is in '" + folder +
															   "', a folder whose name starts with a dot, which the program does not read. "
															   "Nothing here is checked or completed")))));
}

void Server::publish(std::vector<std::string> &r_out) {
	if (workspace_.stale() || (workspace_.progress() == published_check_ && document_generation_ == published_document_generation_)) {
		return;
	}
	published_check_ = workspace_.progress();
	published_document_generation_ = document_generation_;
	std::set<std::string> present;
	auto send = [&](const std::string &p_uri, const std::string &p_diagnostics) {
		// The list is JSON already.
		const auto version = document_version(p_uri);
		r_out.push_back("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":" +
				write_json(Json(p_uri)) + ",\"diagnostics\":" + p_diagnostics +
				(version ? ",\"version\":" + std::to_string(*version) : "") + "}}");
	};
	for (Unit *unit : workspace_.reported_units()) {
		std::string uri = uri_of(*unit);
		if (uri.empty()) {
			continue;
		}
		present.insert(uri);
		if (!workspace_.checked(*unit)) {
			continue; // The editor keeps what it has until the file is checked.
		}
		auto before = published_.find(uri);
		const auto version = document_version(uri);
		// Warnings are shown as errors are: a file with either is published, and cleared when it has none.
		const bool any = unit->diagnostics.has_errors() || unit->diagnostics.has_warnings();
		if (!any && before == published_.end() && !version) {
			continue;
		}
		std::string text = diagnostics_json(*unit);
		if (before == published_.end() || before->second.diagnostics != text || before->second.version != version) {
			send(uri, text);
			if (!any && !version) {
				published_.erase(uri);
			} else {
				// A client typing ahead can discard a clear, so clean open versions need fresh clears too.
				published_[uri] = { std::move(text), version };
			}
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

bool Server::settle(std::vector<std::string> &r_out, const std::function<bool()> &p_yield) {
	if (!initialized_ || exited_) {
		return true;
	}
	const bool done = workspace_.settle(p_yield);
	if (workspace_.cut_off() && !warned_cut_off_) {
		warned_cut_off_ = true;
		std::string text = "Kumikae AS3: this project has more than " + std::to_string(workspace_.max_files()) +
				" .as files. The rest are not read, so names they define are reported as unknown.";
		r_out.push_back(write_json(Json::object().set("jsonrpc", "2.0").set("method", "window/showMessage").set("params",
				Json::object().set("type", 2).set("message", text))));
	}
	publish(r_out);
	return done;
}

} // namespace kas3
