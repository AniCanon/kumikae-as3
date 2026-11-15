// The framing of Language Server Protocol messages over two file descriptors: `Content-Length` headers.
#pragma once

#include <string>
#include <vector>

namespace kas3 {

class Channel {
	int input_;
	int output_;
	std::string buffer_;
	bool ended_ = false;

	bool fill();

public:
	Channel(int p_input, int p_output) :
			input_(p_input), output_(p_output) {}

	// The next message's body. False at the end of input, or when the framing is broken past repair.
	bool next(std::string *r_message);
	// True if another message, or part of one, is already waiting.
	bool waiting() const;
	bool send(const std::vector<std::string> &p_messages);
};

} // namespace kas3
