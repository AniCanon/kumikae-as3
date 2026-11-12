// kas3c: the command-line front end.
#include "arena.h"
#include "builtins.h"
#include "ast_printer.h"
#include "diagnostics.h"
#include "names.h"
#include "parser.h"
#include "source.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char *const USAGE =
		"usage: kas3c parse [--dump-ast] <file>...\n"
		"       kas3c check [--dump-names] [--dump-types] [--no-godot] [--declarations <file>]... <file>...\n"
		"       kas3c --version\n"
		"       kas3c --help\n"
		"\n"
		"parse  Read ActionScript 3 source files and report syntax errors.\n"
		"       --dump-ast  also print each file's syntax tree\n"
		"\n"
		"check  Read the files as one program and report syntax errors, names that are\n"
		"       unknown, ambiguous, duplicated or misused, and members that don't\n"
		"       exist or can't be reached.\n"
		"       --dump-names           also print what every name refers to\n"
		"       --dump-types           also print the type of every statement and initializer\n"
		"       --declarations <file>  a file that declares what exists elsewhere\n"
		"       --no-godot             don't load Godot's API (the `godot` package)\n"
		"\n"
		"Exit status: 0 no errors, 1 errors found, 2 bad usage or an unreadable file.\n";

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

static int run_check(int p_argc, char **p_argv) {
	bool dump_names = false;
	bool dump_types = false;
	bool unreadable = false;
	bool godot = true;
	int files = 0;
	for (int i = 2; i < p_argc; i++) {
		godot = godot && std::strcmp(p_argv[i], "--no-godot") != 0;
	}
	kas3::Compilation compilation;
	if (godot) {
		// Before the program's files: a name the program defines again is then reported in the program.
		compilation.add(kas3::GODOT_DECLARATIONS_PATH, kas3::godot_declarations(), true);
	}
	for (int i = 2; i < p_argc; i++) {
		bool declaration = false;
		if (std::strcmp(p_argv[i], "--no-godot") == 0) {
			continue;
		}
		if (std::strcmp(p_argv[i], "--dump-names") == 0) {
			dump_names = true;
			continue;
		}
		if (std::strcmp(p_argv[i], "--dump-types") == 0) {
			dump_types = true;
			continue;
		}
		if (std::strcmp(p_argv[i], "--declarations") == 0) {
			if (++i >= p_argc) {
				return usage_error();
			}
			declaration = true;
		} else if (p_argv[i][0] == '-' && p_argv[i][1] != 0) {
			return usage_error();
		}
		std::string text;
		if (!kas3::read_file(p_argv[i], &text)) {
			std::fprintf(stderr, "kas3c: cannot read %s\n", p_argv[i]);
			unreadable = true;
			continue;
		}
		compilation.add(p_argv[i], std::move(text), declaration);
		files += declaration ? 0 : 1;
	}
	if (unreadable) {
		return 2;
	}
	if (files == 0) {
		return usage_error();
	}
	compilation.check();
	std::fputs(compilation.format_diagnostics().c_str(), stderr);
	if (dump_names) {
		std::fputs(compilation.dump_names().c_str(), stdout);
	}
	if (dump_types) {
		std::fputs(compilation.dump_types().c_str(), stdout);
	}
	return compilation.has_errors() ? 1 : 0;
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
	if (command == "check") {
		return run_check(p_argc, p_argv);
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
