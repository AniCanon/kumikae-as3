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
	// On Windows the descriptors are set to binary: in text mode "\r\n" would be read as "\n" and every
	// "\n" written as "\r\n", and the lengths in the headers would be wrong.
	Channel(int p_input, int p_output);

	// The next message's body. False at the end of input, or when the framing is broken past repair.
	bool next(std::string *r_message);
	// True if another message, or part of one, is already waiting.
	bool waiting() const;
	bool send(const std::vector<std::string> &p_messages);
};

} // namespace kas3
