// kas3c: the command-line front end.
#include "arena.h"
#include "builtins.h"
#include "ast_printer.h"
#include "diagnostics.h"
#include "interpreter.h"
#include "lower.h"
#include "names.h"
#include "parser.h"
#include "server.h"
#include "source.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const char *const USAGE =
		"usage: kas3c parse [--dump-ast] <file>...\n"
		"       kas3c check [--dump-names] [--dump-types] [--no-godot] [--declarations <file>]... <file>...\n"
		"       kas3c run <file>...\n"
		"       kas3c ir [--godot] <file>...\n"
		"       kas3c lsp [--stdio] [--no-godot] [--declarations <file>]...\n"
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
		"run    Check the files as one program and run it: the first file's class is\n"
		"       constructed, and what it traces goes to standard output. An error that\n"
		"       nothing catches goes to standard error with its stack trace.\n"
		"\n"
		"ir     Check the files as one program and print the typed IR the interpreter runs.\n"
		"       --godot  the files are scripts for Godot: load Godot's API, and expect no\n"
		"                class to start the program with\n"
		"\n"
		"lsp    Serve an editor over standard input and output with the Language Server\n"
		"       Protocol: errors as you type, go to definition, find references, hover,\n"
		"       outline and symbol search. The project is the folder the editor opens.\n"
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
	struct File {
		const char *path;
		std::string text;
		bool declaration;
	};
	std::vector<File> read;
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
		read.push_back(File{ p_argv[i], std::move(text), declaration });
		files += declaration ? 0 : 1;
	}
	if (unreadable) {
		return 2;
	}
	if (files == 0) {
		return usage_error();
	}
	// Godot's API takes most of the time of a small check: it is loaded only for files that name its
	// package. A file that never says `godot` can't reach it.
	godot = godot && std::any_of(read.begin(), read.end(), [](const File &p_file) {
		return p_file.text.find("godot") != std::string::npos;
	});
	kas3::Compilation compilation;
	if (godot) {
		// Before the program's files: a name the program defines again is then reported in the program.
		compilation.add(kas3::GODOT_DECLARATIONS_PATH, kas3::godot_declarations(), true);
	}
	for (File &file : read) {
		compilation.add(file.path, std::move(file.text), file.declaration);
	}
	// Through syntax errors: what they can't have caused is reported too, as the language server does.
	compilation.check(true);
	std::fputs(compilation.format_diagnostics().c_str(), stderr);
	if (dump_names) {
		std::fputs(compilation.dump_names().c_str(), stdout);
	}
	if (dump_types) {
		std::fputs(compilation.dump_types().c_str(), stdout);
	}
	return compilation.has_errors() ? 1 : 0;
}

// Checks the files and lowers them. Prints the errors and returns false if there are any. With p_godot,
// `--godot` may be given: the files are then scripts for Godot, lowered as the extension lowers them.
static bool compile(int p_argc, char **p_argv, kas3::ir::Module *r_module, int *r_status, bool p_godot = false) {
	kas3::Compilation compilation;
	int files = 0;
	bool library = false;
	for (int i = 2; i < p_argc; i++) {
		if (p_godot && !library && std::strcmp(p_argv[i], "--godot") == 0) {
			library = true;
			compilation.add(kas3::GODOT_DECLARATIONS_PATH, kas3::godot_declarations(), true);
			continue;
		}
		if (p_argv[i][0] == '-' && p_argv[i][1] != 0) {
			*r_status = usage_error();
			return false;
		}
		std::string text;
		if (!kas3::read_file(p_argv[i], &text)) {
			std::fprintf(stderr, "kas3c: cannot read %s\n", p_argv[i]);
			*r_status = 2;
			return false;
		}
		compilation.add(p_argv[i], std::move(text));
		files++;
	}
	if (files == 0) {
		*r_status = usage_error();
		return false;
	}
	compilation.check();
	const kas3::ir::LowerMode mode = library ? kas3::ir::LowerMode::Library : kas3::ir::LowerMode::Program;
	bool lowered = !compilation.has_errors() && kas3::ir::lower(compilation, r_module, mode);
	std::fputs(compilation.format_diagnostics().c_str(), stderr);
	*r_status = 1;
	return lowered && !compilation.has_errors();
}

static int run_ir(int p_argc, char **p_argv) {
	kas3::ir::Module module;
	int status = 0;
	if (!compile(p_argc, p_argv, &module, &status, true)) {
		return status;
	}
	std::fputs(kas3::ir::print(module).c_str(), stdout);
	return 0;
}

static int run_run(int p_argc, char **p_argv) {
	kas3::ir::Module module;
	int status = 0;
	if (!compile(p_argc, p_argv, &module, &status)) {
		return status;
	}
	bool ok = false;
	{
		kas3::vm::StandaloneOutput output;
		// KAS3_NOW fixes the time Date takes as now, in milliseconds since 1970 (for tests); TZ sets the zone,
		// as the C library reads it.
		const char *fixed = std::getenv("KAS3_NOW");
		kas3::vm::SystemClock clock(fixed ? std::optional<double>(std::strtod(fixed, nullptr)) : std::nullopt);
		kas3::vm::Interpreter interpreter(module, kas3::vm::Host(output, kas3::vm::NoEngine::instance(), kas3::vm::NoEngine::instance(), clock));
		ok = interpreter.run_program();
	}
	std::fflush(stdout);
	// Everything the program made should be gone by now: as the interpreter goes, its cycle collector frees
	// what is left in loops (vm/collector.h).
	if (std::getenv("KAS3_REPORT_LEAKS") && (kas3::vm::live_objects() || kas3::vm::live_strings())) {
		std::fprintf(stderr, "kas3c: %zu objects and %zu strings were never freed\n", kas3::vm::live_objects(),
				kas3::vm::live_strings());
		return 3;
	}
	return ok ? 0 : 1;
}

static int run_lsp(int p_argc, char **p_argv) {
	kas3::Workspace::Options options;
	for (int i = 2; i < p_argc; i++) {
		if (std::strcmp(p_argv[i], "--no-godot") == 0) {
			options.godot = false;
		} else if (std::strcmp(p_argv[i], "--stdio") == 0) {
			// Standard input and output are the only way in; editors' clients say so anyway.
		} else if (std::strcmp(p_argv[i], "--declarations") == 0 && i + 1 < p_argc) {
			options.declarations.push_back(p_argv[++i]);
		} else {
			return usage_error();
		}
	}
#ifndef _WIN32
	// An editor that goes away must not kill the server in the middle of a write.
	std::signal(SIGPIPE, SIG_IGN);
#endif
	return kas3::run_server(0, 1, std::move(options));
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
	if (command == "run") {
		return run_run(p_argc, p_argv);
	}
	if (command == "ir") {
		return run_ir(p_argc, p_argv);
	}
	if (command == "lsp") {
		return run_lsp(p_argc, p_argv);
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
