// Source text with the places where errors are expected marked as `/*!*/`.
#pragma once

#include <initializer_list>
#include <string>

struct MarkedSource {
	std::string text; // The source with the markers removed.
	std::string expected; // One "<path>:<line>:<column>: error: <message>" line per marker.
};

inline MarkedSource mark(const char *p_path, const std::string &p_text, std::initializer_list<const char *> p_messages) {
	MarkedSource result;
	const std::string marker = "/*!*/";
	int line = 1;
	int column = 1;
	auto message = p_messages.begin();
	for (size_t i = 0; i < p_text.size();) {
		if (p_text.compare(i, marker.size(), marker) == 0) {
			result.expected += std::string(p_path) + ":" + std::to_string(line) + ":" + std::to_string(column) +
					": error: " + (message != p_messages.end() ? *message++ : "MISSING MESSAGE") + "\n";
			i += marker.size();
			continue;
		}
		if (p_text[i] == '\n') {
			line++;
			column = 1;
		} else {
			column++;
		}
		result.text += p_text[i++];
	}
	while (message != p_messages.end()) {
		result.expected += std::string("UNPLACED MESSAGE: ") + *message++ + "\n";
	}
	return result;
}
