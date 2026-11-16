// Random edits for the incremental-checking oracle: what typing, deleting, pasting and undoing do to a file.
// Each returns a short description, printed when a sequence fails.
#pragma once

#include "oracle_project.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace oracle {

inline bool word_byte(char p_c) {
	return std::isalnum(static_cast<unsigned char>(p_c)) || p_c == '_' || p_c == '$';
}

struct Span {
	size_t begin;
	size_t end;
};

inline std::vector<Span> words(const std::string &p_text) {
	std::vector<Span> found;
	for (size_t at = 0; at < p_text.size();) {
		if (word_byte(p_text[at]) && !std::isdigit(static_cast<unsigned char>(p_text[at]))) {
			size_t end = at;
			while (end < p_text.size() && word_byte(p_text[end])) {
				end++;
			}
			found.push_back({ at, end });
			at = end;
		} else {
			at++;
		}
	}
	return found;
}

inline std::vector<Span> lines(const std::string &p_text) {
	std::vector<Span> found;
	size_t begin = 0;
	while (begin < p_text.size()) {
		size_t end = p_text.find('\n', begin);
		end = end == std::string::npos ? p_text.size() : end + 1;
		found.push_back({ begin, end });
		begin = end;
	}
	return found;
}

// Every word written in the project, and the words of AS3 that change what a declaration means.
inline std::vector<std::string> vocabulary(const Files &p_files) {
	std::vector<std::string> found = { "public", "private", "protected", "internal", "static", "override", "final",
		"extends", "implements", "function", "var", "const", "get", "set", "import", "int", "Number", "String" };
	for (const auto &[path, text] : p_files) {
		for (const Span &word : words(text)) {
			found.push_back(text.substr(word.begin, word.end - word.begin));
		}
	}
	std::sort(found.begin(), found.end());
	found.erase(std::unique(found.begin(), found.end()), found.end());
	return found;
}

class Editor {
	Random &random_;
	const std::vector<std::string> &vocabulary_;

	std::string replace_word(std::string &r_text) {
		std::vector<Span> found = words(r_text);
		if (found.empty()) {
			return "no word";
		}
		const Span word = random_.pick(found);
		const std::string with = random_.pick(vocabulary_);
		const std::string was = r_text.substr(word.begin, word.end - word.begin);
		r_text.replace(word.begin, word.end - word.begin, with);
		return "word " + was + " -> " + with + " at " + std::to_string(word.begin);
	}

	std::string line_edit(std::string &r_text, const std::string &p_other) {
		std::vector<Span> found = lines(r_text);
		if (found.empty()) {
			r_text = p_other;
			return "whole text";
		}
		const Span line = random_.pick(found);
		switch (random_.below(3)) {
			case 0:
				r_text.erase(line.begin, line.end - line.begin);
				return "delete line at " + std::to_string(line.begin);
			case 1: {
				std::vector<Span> others = lines(p_other);
				const std::string pasted = others.empty() ? std::string("\n") : [&] {
					const Span from = random_.pick(others);
					return p_other.substr(from.begin, from.end - from.begin);
				}();
				r_text.insert(line.begin, pasted);
				return "paste line at " + std::to_string(line.begin);
			}
			default: {
				const std::string copy = r_text.substr(line.begin, line.end - line.begin);
				r_text.insert(line.end, copy);
				return "duplicate line at " + std::to_string(line.begin);
			}
		}
	}

	std::string character(std::string &r_text) {
		const size_t at = random_.below(r_text.size() + 1);
		if (random_.chance(50) && at < r_text.size()) {
			r_text.erase(at, 1);
			return "delete character at " + std::to_string(at);
		}
		static const std::string typed = "{}();:.,=+ \nax";
		const char c = typed[random_.below(typed.size())];
		r_text.insert(at, 1, c);
		return std::string("type '") + c + "' at " + std::to_string(at);
	}

	std::string modifier(std::string &r_text) {
		static const std::vector<std::string> modifiers = { "public ", "private ", "protected ", "static ", "override ",
			"final ", "internal " };
		std::vector<size_t> places;
		for (const char *word : { "function ", "var ", "const ", "class ", "interface " }) {
			for (size_t at = r_text.find(word); at != std::string::npos; at = r_text.find(word, at + 1)) {
				places.push_back(at);
			}
		}
		if (places.empty()) {
			return "no declaration";
		}
		const size_t at = random_.pick(places);
		for (const std::string &word : modifiers) {
			if (at >= word.size() && r_text.compare(at - word.size(), word.size(), word) == 0 && random_.chance(50)) {
				r_text.erase(at - word.size(), word.size());
				return "remove " + word + "at " + std::to_string(at);
			}
		}
		const std::string &word = random_.pick(modifiers);
		r_text.insert(at, word);
		return "add " + word + "at " + std::to_string(at);
	}

	std::string clause(std::string &r_text) {
		const std::string &type = random_.pick(vocabulary_);
		for (const char *word : { " extends ", " implements " }) {
			const size_t at = r_text.find(word);
			if (at != std::string::npos && random_.chance(60)) {
				const size_t begin = at + std::string(word).size();
				size_t end = begin;
				while (end < r_text.size() && word_byte(r_text[end])) {
					end++;
				}
				if (random_.chance(30)) {
					r_text.erase(at, end - at);
					return std::string("remove") + word;
				}
				r_text.replace(begin, end - begin, type);
				return std::string("change") + word + "to " + type;
			}
		}
		size_t declared = r_text.find("class ");
		const bool is_class = declared != std::string::npos;
		if (!is_class) {
			declared = r_text.find("interface ");
		}
		const size_t at = declared == std::string::npos ? std::string::npos : r_text.find(" {", declared);
		if (at == std::string::npos) {
			return "no clause";
		}
		const std::string word = is_class && random_.chance(70) ? " extends " : " implements ";
		r_text.insert(at, word + type);
		return "add" + word + type;
	}

	std::string import(std::string &r_text) {
		const size_t at = r_text.find("import ");
		if (at != std::string::npos && random_.chance(40)) {
			const size_t end = r_text.find('\n', at);
			r_text.erase(at, (end == std::string::npos ? r_text.size() : end + 1) - at);
			return "remove import";
		}
		static const std::vector<std::string> written = { "import game.*;\n", "import game.util.*;\n", "import game.moved.*;\n",
			"import elsewhere.*;\n", "import api.*;\n",
			"import game.C1;\n", "import game.util.C2;\n", "import game.I0;\n", "import nowhere.*;\n" };
		const size_t brace = r_text.find('{');
		const std::string &line = random_.pick(written);
		r_text.insert(brace == std::string::npos ? 0 : brace + 1, line);
		return "add " + line.substr(0, line.size() - 1);
	}

	std::string body(std::string &r_text) {
		const size_t at = r_text.find("\t\t\t", random_.below(r_text.size() + 1));
		if (at == std::string::npos) {
			return character(r_text);
		}
		const std::string &a = random_.pick(vocabulary_);
		const std::string &b = random_.pick(vocabulary_);
		const std::string statement = random_.chance(50) ? a + " = " + b + ";\n\t\t\t" : "trace(" + a + "." + b + ");\n\t\t\t";
		r_text.insert(at + 3, statement);
		return "statement " + statement.substr(0, statement.find('\n'));
	}

	// One half of a property written into the class, overriding or not: a base may have the other half, or a
	// class between this one and the base that has both.
	std::string accessor(std::string &r_text) {
		const size_t declared = r_text.find("class ");
		const size_t brace = declared == std::string::npos ? std::string::npos : r_text.find(" {\n", declared);
		if (brace == std::string::npos) {
			return body(r_text);
		}
		const std::string &name = random_.pick(member_names());
		const std::string &type = random_.pick(value_types());
		const std::string half = random_.chance(50) ? "function get " + name + "():" + type + " { return null; }"
													: "function set " + name + "(v:" + type + "):void {}";
		const std::string line = std::string("\t\t") + (random_.chance(60) ? "override " : "") + "public " + half + "\n";
		r_text.insert(brace + 3, line);
		return "accessor " + line.substr(2, line.size() - 3);
	}

	// The file moves to another package: one of the project's, or one that no other file is in, which comes
	// to exist with the move and stops existing with the next.
	std::string package(std::string &r_text) {
		const size_t at = r_text.find("package ");
		const size_t brace = at == std::string::npos ? std::string::npos : r_text.find('{', at);
		if (brace == std::string::npos) {
			return body(r_text);
		}
		static const std::vector<std::string> names = { "", "game ", "game.util ", "game.moved ", "elsewhere ", "game.util.deep " };
		const std::string &name = random_.pick(names);
		r_text.replace(at + 8, brace - at - 8, name);
		return "package " + name;
	}

public:
	Editor(Random &p_random, const std::vector<std::string> &p_vocabulary) :
			random_(p_random), vocabulary_(p_vocabulary) {}

	// Changes the text in one of the ways typing would; p_other is another file of the project, to paste from.
	std::string edit(std::string &r_text, const std::string &p_other) {
		switch (random_.below(12)) {
			case 0:
			case 1:
				return replace_word(r_text);
			case 2:
				return line_edit(r_text, p_other);
			case 3:
				return character(r_text);
			case 4:
				return modifier(r_text);
			case 5:
				return clause(r_text);
			case 6:
				return import(r_text);
			case 7:
				return accessor(r_text);
			case 8:
				return package(r_text);
			default:
				return body(r_text);
		}
	}
};

} // namespace oracle
