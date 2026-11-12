// The interpreter as a library: what a debugger will use, and what the host is told.
#include "check.h"

#include "interpreter.h"
#include "lower.h"
#include "names.h"

#include <string>
#include <vector>

using namespace kas3;

struct Recorder : vm::Host {
	std::string printed, errors;
	void print(std::string_view p_text) override { printed += std::string(p_text) + "\n"; }
	void error(std::string_view p_text) override { errors += std::string(p_text) + "\n"; }
};

static const char *const PROGRAM =
		"package {\n" // 1
		"public class Main {\n" // 2
		"	public function Main() {\n" // 3
		"		var n:int = 1;\n" // 4
		"		n = twice(n);\n" // 5
		"		trace(n);\n" // 6
		"	}\n"
		"	private function twice(value:int):int {\n" // 8
		"		var result:int = value * 2;\n" // 9
		"		return result;\n" // 10
		"	}\n"
		"}\n"
		"}\n";

struct Steps {
	std::string seen;
	int limit = 1000;
	int count = 0;
};

static bool on_statement(vm::Interpreter &p_interpreter, void *p_data) {
	Steps *steps = static_cast<Steps *>(p_data);
	size_t top = p_interpreter.depth() - 1;
	steps->seen += p_interpreter.function_at(top).name + ":" + std::to_string(p_interpreter.line_at(top)) + " ";
	return ++steps->count < steps->limit;
}

static void test_statement_hook() {
	Compilation compilation;
	compilation.add("Main.as", PROGRAM);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module));

	// With no hook the program just runs.
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK(interpreter.run_program());
		CHECK_EQ(host.printed, std::string("2\n"));
		CHECK_EQ(host.errors, std::string());
	}
	// The hook is told of every statement before it runs, with the frames to walk.
	{
		Recorder host;
		Steps steps;
		vm::Interpreter interpreter(module, host);
		interpreter.set_statement_hook(on_statement, &steps);
		CHECK(interpreter.run_program());
		CHECK_EQ(steps.seen, std::string("Main:4 Main:5 Main/twice:9 Main/twice:10 Main:6 "));
		CHECK_EQ(host.printed, std::string("2\n"));
	}
	// Returning false stops the program there, without an error.
	{
		Recorder host;
		Steps steps;
		steps.limit = 3;
		vm::Interpreter interpreter(module, host);
		interpreter.set_statement_hook(on_statement, &steps);
		CHECK(!interpreter.run_program());
		CHECK_EQ(steps.seen, std::string("Main:4 Main:5 Main/twice:9 "));
		CHECK_EQ(host.printed, std::string());
		CHECK_EQ(host.errors, std::string());
		CHECK_EQ(interpreter.depth(), (size_t)0);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
	// Named locals are in the IR for a debugger to show.
	std::string locals;
	for (const ir::Function &function : module.functions) {
		if (function.name == "Main/twice") {
			for (const ir::Local &local : function.locals) {
				locals += local.name + "=r" + std::to_string(local.reg) + " ";
			}
		}
	}
	CHECK_EQ(locals, std::string("this=r0 value=r1 result=r2 "));
}

static void test_refused_program() {
	Compilation compilation;
	Unit *unit = compilation.add("Main.as", "package { public class Main { public function Main() { var a:Array = []; } } }");
	compilation.check();
	ir::Module module;
	CHECK(!ir::lower(compilation, &module));
	CHECK(unit->diagnostics.has_errors());
	CHECK(compilation.format_diagnostics().find("is not supported by the interpreter yet") != std::string::npos);
}

int main() {
	test_statement_hook();
	test_refused_program();
	return check::finish();
}
