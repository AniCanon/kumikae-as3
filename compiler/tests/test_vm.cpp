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
	// A caller is on the line of its call, not the one after.
	if (top > 0) {
		steps->seen += "(from " + std::to_string(p_interpreter.line_at(top - 1)) + ") ";
	}
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
		CHECK_EQ(steps.seen, std::string("Main:4 Main:5 Main/twice:9 (from 5) Main/twice:10 (from 5) Main:6 "));
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
		CHECK_EQ(steps.seen, std::string("Main:4 Main:5 Main/twice:9 (from 5) "));
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

// The engine will call into one interpreter again and again, and a script's call out may come back in.
static const char *const LIBRARY =
		"package {\n"
		"public class Counter {\n"
		"	public var total:int = 0;\n"
		"	public static var made:int = 10;\n"
		"	public function Counter() { made++; }\n"
		"	public function add(amount:int, label:String = \"none\"):int { total += amount; return total; }\n"
		"	public function name():String { return \"counter \" + total; }\n"
		"	public function fail():void { var nothing:Counter; nothing.add(1); }\n"
		"	public function shout(depth:int):int { trace(\"depth\", depth); return depth; }\n"
		"	public function forever(n:int):int { return forever(n + 1); }\n"
		"}\n"
		"}\n";

struct Reentrant : vm::Host {
	vm::Interpreter *interpreter = nullptr;
	vm::Object *object = nullptr;
	std::string printed, errors;
	int nest = 0; // How many more times a trace calls back in.
	bool fail_inside = false;
	int results = 0;
	void print(std::string_view p_text) override {
		printed += std::string(p_text) + "\n";
		if (nest > 0) {
			nest--;
			vm::Slot argument;
			argument.bits = 0;
			argument.i = nest;
			vm::Slot result;
			result.bits = 0;
			if (fail_inside && nest == 0) {
				bool ok = interpreter->call_method(object, "fail", nullptr, 0, nullptr);
				results += ok ? 100 : 0;
			} else if (interpreter->call_method(object, "shout", &argument, 1, &result)) {
				results += result.i + 1;
			}
		}
	}
	void error(std::string_view p_text) override { errors += std::string(p_text) + "\n"; }
};

static void test_entry_points() {
	Compilation compilation;
	compilation.add("Counter.as", LIBRARY);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	uint32_t counter_class = ir::NONE;
	for (size_t i = 0; i < module.classes.size(); i++) {
		if (module.classes[i].name == "Counter") {
			counter_class = (uint32_t)i;
		}
	}
	CHECK(counter_class != ir::NONE);
	{
		Reentrant host;
		vm::Interpreter interpreter(module, host);
		host.interpreter = &interpreter;
		CHECK(interpreter.start());
		CHECK(interpreter.start()); // Once only: the statics are not set again.
		vm::Object *object = interpreter.construct(counter_class);
		CHECK(object != nullptr);
		host.object = object;

		// A method by name, with arguments and a result; a default for what is left out.
		vm::Slot arguments[2];
		arguments[0].bits = 0;
		arguments[0].i = 5;
		vm::Slot result;
		result.bits = 0;
		CHECK(interpreter.call_method(object, "add", arguments, 1, &result));
		CHECK_EQ(result.i, 5);
		CHECK(interpreter.call_method(object, "add", arguments, 1, &result));
		CHECK_EQ(result.i, 10);
		// A string comes back with a reference the caller owns.
		CHECK(interpreter.call_method(object, "name", nullptr, 0, &result));
		vm::Str *text = static_cast<vm::Str *>(result.p);
		CHECK(text && std::string(text->view()) == "counter 10");
		if (vm::release(text)) {
			vm::free_string(text);
		}
		// No such method: false, and nothing reported.
		CHECK(!interpreter.call_method(object, "missing", nullptr, 0, nullptr));
		CHECK_EQ(host.errors, std::string());

		// An error ends that call, is reported once, and leaves the interpreter fit for the next.
		CHECK(!interpreter.call_method(object, "fail", nullptr, 0, nullptr));
		CHECK(host.errors.find("TypeError: Error #1009") == 0);
		CHECK(host.errors.find("\tat Counter/fail()[Counter.as:8]") != std::string::npos);
		host.errors.clear();
		CHECK(interpreter.call_method(object, "add", arguments, 1, &result));
		CHECK_EQ(result.i, 15);
		CHECK_EQ(interpreter.depth(), (size_t)0);

		// A call out that comes back in, many levels deep: each level has its own registers and result.
		host.nest = 40;
		arguments[0].i = 1000;
		host.printed.clear();
		CHECK(interpreter.call_method(object, "shout", arguments, 1, &result));
		CHECK_EQ(result.i, 1000);
		CHECK_EQ(host.results, 40 * 41 / 2); // 39 + 1, 38 + 1, ... 0 + 1.
		CHECK(host.printed.find("depth 1000\ndepth 39\n") == 0);
		CHECK(host.printed.find("depth 0\n") != std::string::npos);
		CHECK_EQ(host.errors, std::string());

		// An error inside a nested call is reported and cleared there; the outer call carries on.
		host.nest = 3;
		host.fail_inside = true;
		host.results = 0;
		CHECK(interpreter.call_method(object, "shout", arguments, 1, &result));
		CHECK_EQ(result.i, 1000);
		CHECK(host.errors.find("TypeError: Error #1009") == 0);
		CHECK_EQ(host.results, 2 + 1 + 1 + 1); // The two levels that returned; the failing one added nothing.
		host.errors.clear();
		host.fail_inside = false;

		// Nesting without end is an AS3 error, not a crash.
		host.nest = 100000;
		host.results = 0;
		CHECK(interpreter.call_method(object, "shout", arguments, 1, &result));
		CHECK(host.errors.find("Error #1023") != std::string::npos);
		host.nest = 0;
		host.errors.clear();
		// So is runaway recursion inside one call, and the next call still works.
		CHECK(!interpreter.call_method(object, "forever", arguments, 1, &result));
		CHECK(host.errors.find("Error #1023") != std::string::npos);
		CHECK(interpreter.call_method(object, "add", arguments, 1, &result));

		// A halt from the statement hook ends that call only.
		Steps steps;
		steps.limit = 1;
		interpreter.set_statement_hook(on_statement, &steps);
		CHECK(!interpreter.call_method(object, "add", arguments, 1, &result));
		interpreter.set_statement_hook(nullptr, nullptr);
		host.errors.clear();
		CHECK(interpreter.call_method(object, "add", arguments, 1, &result));
		CHECK_EQ(host.errors, std::string());

		interpreter.release_object(object);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
	// A library has no entry class; a program still needs one.
	ir::Module program;
	Compilation again;
	again.add("Counter.as", "package { public function lonely():void {} }");
	again.check();
	CHECK(!ir::lower(again, &program));
	ir::Module library;
	Compilation third;
	third.add("Counter.as", "package { public function lonely():void {} }");
	third.check();
	CHECK(ir::lower(third, &library, ir::LowerMode::Library));
}

int main() {
	test_entry_points();
	test_statement_hook();
	test_refused_program();
	return check::finish();
}
