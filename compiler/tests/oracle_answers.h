// What a workspace says, for the incremental-checking tests: the whole program's state, and the answers an
// editor would be given at places of a file. An incremental workspace and a fresh one must say the same.
#pragma once

#include "check.h"
#include "scratch.h"

#include "workspace.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace oracle {

using namespace kas3;

// A declaration file whose class has tags, two of them mistaken: what it says must not depend on which
// files use the class, or on how often they are checked.
inline const std::string &tagged_api() {
	static const std::string path = [] {
		const std::string written = (check::fresh_folder("api") / "tagged.as").string();
		std::ofstream(written, std::ios::binary)
				<< "package api {\n\t[Operators]\n\t[Operator(op=\"+\", right=\"V2\", returns=\"V2\")]\n"
				   "\t[Operator(op=\"+\", right=\"V2\", returns=\"V2\")]\n\t[Operator(op=\"-\", right=\"Nope\", returns=\"V2\")]\n"
				   "\t[Indexed(element=\"int\")]\n\tpublic class V2 {\n\t\tpublic var x:Number;\n\t\tpublic native function V2(x:Number = 0);\n\t}\n}\n";
		return written;
	}();
	return path;
}

inline Workspace::Options options(bool p_godot, bool p_tagged = false) {
	Workspace::Options result;
	result.godot = p_godot;
	result.support_folder = (check::run_folder() / "support").string();
	if (p_tagged) {
		result.declarations.push_back(tagged_api());
	}
	return result;
}

inline std::string program_state(Workspace &p_workspace) {
	Compilation &compilation = p_workspace.compilation();
	return compilation.format_diagnostics() + "-- names\n" + compilation.dump_names() + "-- types\n" + compilation.dump_types();
}

inline std::string place_text(const Place &p_place) {
	return p_place.unit ? p_place.unit->source.path() + ":" + std::to_string(p_place.offset) + "+" + std::to_string(p_place.length)
						: std::string("none");
}

// What the editor would be told at each offset of the file.
inline std::string answers(Workspace &p_workspace, const std::string &p_path, const std::vector<uint32_t> &p_offsets) {
	Unit *unit = p_workspace.unit(p_path);
	if (!unit) {
		return "no unit\n";
	}
	Queries &queries = p_workspace.queries();
	std::string out;
	for (uint32_t offset : p_offsets) {
		out += "@" + std::to_string(offset) + "\n";
		const CompletionResult completion = queries.complete(*unit, offset);
		out += completion.incomplete ? "incomplete\n" : "";
		for (const Completion &item : completion.items) {
			out += " " + item.name + "|" + item.detail + "|" + std::to_string((int)item.kind) + (item.is_keyword ? "k" : "");
			if (item.edit) {
				out += "|" + item.edit->text;
			}
			if (item.additional_edit) {
				out += "|+" + item.additional_edit->text;
			}
			out += "\n";
		}
		if (const Occurrence *found = queries.at(*unit, offset)) {
			out += "hover " + queries.describe(found->symbol) + " / " + queries.context(found->symbol) + "\n";
		}
		out += "definition " + place_text(queries.definition(*unit, offset)) + "\n";
		if (auto help = queries.signature_help(*unit, offset)) {
			out += "help " + help->label + " " + std::to_string(help->active_parameter) + "\n";
		}
	}
	return out;
}

inline std::string first_difference(const std::string &p_got, const std::string &p_wanted) {
	std::istringstream got(p_got);
	std::istringstream wanted(p_wanted);
	std::string a;
	std::string b;
	for (int line = 1;; ++line) {
		const bool more_a = (bool)std::getline(got, a);
		const bool more_b = (bool)std::getline(wanted, b);
		if (!more_a && !more_b) {
			return "no difference";
		}
		if (a != b || more_a != more_b) {
			return "line " + std::to_string(line) + "\n  incremental: " + (more_a ? a : "(end)") + "\n  full check:  " + (more_b ? b : "(end)");
		}
	}
}

} // namespace oracle
