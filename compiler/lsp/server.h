// The language server: Language Server Protocol messages in, messages out.
#pragma once

#include "json.h"
#include "workspace.h"

#include <functional>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kas3 {

// A publishDiagnostics notification for the file, with that list.
Json diagnostics_message(const std::string &p_uri, Json p_diagnostics);

class Server {
	Workspace workspace_;
	bool initialized_ = false;
	// What code blocks in a hover are marked as: the editor may ask for its own name for the language.
	std::string code_language_ = "actionscript";
	bool shutdown_ = false;
	bool exited_ = false;
	int exit_code_ = 1;
	bool default_edit_range_ = false;
	bool snippets_ = false;
	bool diagnostic_versions_ = false;
	bool insert_replace_ = false;
	bool utf8_ = false; // Positions count bytes. Otherwise UTF-16 code units.
	bool warned_cut_off_ = false; // The editor has been told the project is too big to read whole.
	bool watch_files_ = false; // The editor lets the server ask to be told about files changing on disk.
	int published_check_ = -1; // The check whose errors the editor has.
	uint64_t document_generation_ = 0;
	uint64_t published_document_generation_ = 0;
	int next_request_ = 1;
	std::unordered_map<std::string, std::string> uris_; // The URI the editor uses for a path.
	std::unordered_map<std::string, int64_t> versions_; // Current open-document version, by document_key().
	struct Published {
		std::string diagnostics;
		std::optional<int64_t> version;
	};
	std::map<std::string, Published> published_; // By URI: the diagnostics last sent.

	std::string uri_of(const Unit &p_unit);
	Unit *unit_of(const Json &p_params);
	Json position(const Unit &p_unit, uint32_t p_offset) const;
	Json range(const Unit &p_unit, uint32_t p_offset, uint32_t p_length) const;
	Json location(const Place &p_place);
	uint32_t offset_at(const Unit &p_unit, const Json &p_position) const;
	uint32_t offset_in(const Source &p_source, const Json &p_position) const;
	int mark_width(const Source &p_source) const;
	const Occurrence *occurrence_at(const Json &p_params, Unit **r_unit);
	std::string diagnostics_json(const Unit &p_unit) const;
	void publish(std::vector<std::string> &r_out);
	void handle_message(const Json &p_message, std::vector<std::string> &r_out);
	// The name a document is known by, whatever name the editor gives it: the workspace's for its file.
	std::string document_key(const std::string &p_uri) const;
	static std::optional<int64_t> valid_version(const Json &p_document);
	bool accept_version(const Json &p_document, bool p_open, std::vector<std::string> &r_out);
	std::optional<int64_t> document_version(const std::string &p_uri) const;

	Json initialize(const Json &p_params);
	Json definition(const Json &p_params);
	Json references(const Json &p_params);
	Json hover(const Json &p_params);
	Json completion(const Json &p_params);
	Json completion_item(const Unit &p_unit, const Completion &p_found, const CompletionRange &p_span,
			const Json &p_edit_range, uint32_t p_priority) const;
	Json signature_help(const Json &p_params);
	Json document_symbols(const Json &p_params);
	Json workspace_symbols(const Json &p_params);
	void notification(const std::string &p_method, const Json &p_params, std::vector<std::string> &r_out);
	void register_file_watcher(std::vector<std::string> &r_out);
	// An opened file the program does not read (workspace.h, dot_folder()) says so in the editor.
	void tell_outside(const std::string &p_path, const std::string &p_uri, std::vector<std::string> &r_out);
	std::set<std::string> outside_; // The URIs told so, cleared when they are closed.
	void did_change(const std::string &p_path, const Json &p_document, const Json &p_params,
			std::vector<std::string> &r_out);
	void did_close(const std::string &p_path);
	std::optional<Json> answer(const std::string &p_method, const Json &p_params);
	using Handler = Json (Server::*)(const Json &p_params);
	// The handler of a request the server answers after `initialize` (each may be cancelled while queued),
	// or null.
	static Handler handler_of(std::string_view p_method);

public:
	explicit Server(Workspace::Options p_options);

	// Handles one message and appends what to send back, each a complete JSON text.
	void handle(std::string_view p_message, std::vector<std::string> &r_out);
	// The bounded input batch preserves notification order and skips queued requests that are cancelled or whose document is closed.
	void handle_batch(const std::vector<std::string> &p_messages, std::vector<std::string> &r_out);
	// Checks what is left of the program, and appends the diagnostics that differ from what the editor has.
	// Call it when no further message is waiting. p_yield is asked after each file: true stops the check,
	// to answer newer messages first; what was checked is kept and published. True if all is checked.
	bool settle(std::vector<std::string> &r_out, const std::function<bool()> &p_yield = [] { return false; });
	bool exited() const { return exited_; }
	int exit_code() const { return exit_code_; } // 0 if `shutdown` came before `exit`.
	Workspace &workspace() { return workspace_; }
};

// Serves one editor over two file descriptors until `exit` or the end of input. Messages are framed with
// `Content-Length` headers. Returns the process's exit status.
int run_server(int p_input, int p_output, Workspace::Options p_options);

} // namespace kas3
