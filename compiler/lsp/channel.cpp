#include "channel.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

namespace kas3 {

namespace {

// The most a single message may hold.
const size_t MAX_MESSAGE = 256u * 1024 * 1024;

} // namespace

Channel::Channel(int p_input, int p_output) :
		input_(p_input), output_(p_output) {
#ifdef _WIN32
	_setmode(p_input, _O_BINARY);
	_setmode(p_output, _O_BINARY);
#endif
}

bool Channel::fill(bool p_wait) {
	if (ended_ || (!p_wait && !readable())) {
		return false;
	}
	char chunk[65536];
#ifdef _WIN32
	int got = _read(input_, chunk, sizeof(chunk));
#else
	ssize_t got = 0;
	do {
		got = read(input_, chunk, sizeof(chunk));
	} while (got < 0 && errno == EINTR);
#endif
	if (got <= 0) {
		ended_ = true;
		return false;
	}
	buffer_.append(chunk, static_cast<size_t>(got));
	return true;
}

bool Channel::next(std::string *r_message, bool p_wait) {
	while (true) {
		size_t header_end = buffer_.find("\r\n\r\n");
		if (header_end == std::string::npos) {
			if (buffer_.size() > 65536 || !fill(p_wait)) {
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
				while (digits < line.size() && (line[digits] == ' ' || line[digits] == '\t')) {
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
			if (!fill(p_wait)) {
				return false;
			}
		}
		r_message->assign(buffer_, body, length);
		buffer_.erase(0, body + length);
		return true;
	}
}

bool Channel::waiting() const {
	return !buffer_.empty() || readable();
}

bool Channel::readable() const {
#ifdef _WIN32
	const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(input_));
	DWORD available = 0;
	return PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) && available != 0;
#else
	struct pollfd descriptor;
	descriptor.fd = input_;
	descriptor.events = POLLIN;
	descriptor.revents = 0;
	return poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN) != 0;
#endif
}

bool Channel::send(const std::vector<std::string> &p_messages) {
	std::string out;
	for (const std::string &message : p_messages) {
		out += "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + message;
	}
	size_t sent = 0;
	while (sent < out.size()) {
#ifdef _WIN32
		int wrote = _write(output_, out.data() + sent, static_cast<unsigned>(out.size() - sent));
#else
		ssize_t wrote = 0;
		do {
			wrote = write(output_, out.data() + sent, out.size() - sent);
		} while (wrote < 0 && errno == EINTR);
#endif
		if (wrote <= 0) {
			return false;
		}
		sent += static_cast<size_t>(wrote);
	}
	return true;
}

} // namespace kas3
