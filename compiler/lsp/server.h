// The language server: Language Server Protocol messages in, messages out.
#pragma once

#include "json.h"
#include "workspace.h"

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kas3 {

class Server {
	Workspace workspace_;
	bool initialized_ = false;
	bool shutdown_ = false;
	bool exited_ = false;
	int exit_code_ = 1;
	bool utf8_ = false; // Positions count bytes. Otherwise UTF-16 code units.
	bool watch_files_ = false; // The editor lets the server ask to be told about files changing on disk.
	int published_check_ = -1; // The check whose errors the editor has.
	int next_request_ = 1;
	std::unordered_map<std::string, std::string> uris_; // The URI the editor uses for a path.
	std::map<std::string, std::string> published_; // By URI: the diagnostics last sent, as JSON.

	std::string uri_of(const Unit &p_unit);
	Unit *unit_of(const Json &p_params);
	Json position(const Unit &p_unit, uint32_t p_offset) const;
	Json range(const Unit &p_unit, uint32_t p_offset, uint32_t p_length) const;
	Json location(const Place &p_place);
	uint32_t offset_at(const Unit &p_unit, const Json &p_position) const;
	const Occurrence *occurrence_at(const Json &p_params, Unit **r_unit);
	void publish(std::vector<std::string> &r_out);

	Json initialize(const Json &p_params);
	Json definition(const Json &p_params);
	Json references(const Json &p_params);
	Json hover(const Json &p_params);
	Json document_symbols(const Json &p_params);
	Json workspace_symbols(const Json &p_params);
	void notification(const std::string &p_method, const Json &p_params, std::vector<std::string> &r_out);

public:
	explicit Server(Workspace::Options p_options);

	// Handles one message and appends what to send back, each a complete JSON text.
	void handle(std::string_view p_message, std::vector<std::string> &r_out);
	// Checks the program if it changed, and appends the diagnostics that differ from what the editor has.
	// Call it when no further message is waiting.
	void settle(std::vector<std::string> &r_out);
	bool exited() const { return exited_; }
	int exit_code() const { return exit_code_; } // 0 if `shutdown` came before `exit`.
	Workspace &workspace() { return workspace_; }
};

// Serves one editor over two file descriptors until `exit` or the end of input. Messages are framed with
// `Content-Length` headers. Returns the process's exit status.
int run_server(int p_input, int p_output, Workspace::Options p_options);

} // namespace kas3
