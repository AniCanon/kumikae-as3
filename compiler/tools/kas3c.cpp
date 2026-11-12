// kas3c: the command-line front end.
#include "arena.h"
#include "ast_printer.h"
#include "diagnostics.h"
#include "parser.h"
#include "source.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char *const USAGE =
		"usage: kas3c parse [--dump-ast] <file>...\n"
		"       kas3c --version\n"
		"       kas3c --help\n"
		"\n"
		"parse  Read ActionScript 3 source files and report syntax errors.\n"
		"       --dump-ast  also print each file's syntax tree\n"
		"\n"
		"Exit status: 0 no errors, 1 syntax errors found, 2 bad usage or an unreadable file.\n";

static int usage_error() {
	std::fputs(USAGE, stderr);
	return 2;
}

static int run_parse(const std::vector<std::string> &p_files, bool p_dump_ast) {
	bool unreadable = false;
	bool errors = false;
	for (const std::string &path : p_files) {
		std::string text;
		if (!kas3::read_file(path, &text)) {
			std::fprintf(stderr, "kas3c: cannot read %s\n", path.c_str());
			unreadable = true;
			continue;
		}
		kas3::Source source(path, std::move(text));
		kas3::Arena arena;
		kas3::Diagnostics diagnostics;
		kas3::Program *program = kas3::parse(source, arena, diagnostics);
		if (diagnostics.has_errors()) {
			errors = true;
			std::fputs(diagnostics.format(source).c_str(), stderr);
		}
		if (p_dump_ast) {
			if (p_files.size() > 1) {
				std::printf("# %s\n", path.c_str());
			}
			std::fputs(kas3::print_ast(program, source).c_str(), stdout);
		}
	}
	if (unreadable) {
		return 2;
	}
	return errors ? 1 : 0;
}

int main(int p_argc, char **p_argv) {
	if (p_argc < 2) {
		return usage_error();
	}
	std::string command = p_argv[1];
	if (command == "--version") {
		std::puts("kas3c 0.1.0");
		return 0;
	}
	if (command == "--help") {
		std::fputs(USAGE, stdout);
		return 0;
	}
	if (command != "parse") {
		return usage_error();
	}
	bool dump_ast = false;
	std::vector<std::string> files;
	for (int i = 2; i < p_argc; i++) {
		if (std::strcmp(p_argv[i], "--dump-ast") == 0) {
			dump_ast = true;
		} else if (p_argv[i][0] == '-' && p_argv[i][1] != 0) {
			return usage_error();
		} else {
			files.push_back(p_argv[i]);
		}
	}
	if (files.empty()) {
		return usage_error();
	}
	return run_parse(files, dump_ast);
}
