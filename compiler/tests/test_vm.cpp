// The interpreter as a library: what a debugger will use, and what the host is told.
#include "check.h"

#include "any.h"
#include "builtins.h"
#include "interpreter.h"
#include "reload.h"
#include "lower.h"
#include "names.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace kas3;

struct Recorder : vm::Output {
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
	// A debugger reads a variable by name.
	vm::Slot value;
	ir::Kind kind = ir::Kind::Void;
	if (p_interpreter.local_at(top, "result", &value, &kind) && kind == ir::Kind::I32 && p_interpreter.line_at(top) == 10) {
		steps->seen += "(result=" + std::to_string(value.i) + ") ";
	}
	CHECK(!p_interpreter.local_at(top, "no such local", &value, &kind));
	// A caller is on the line of its call, not the one after.
	if (top > 0) {
		steps->seen += "(from " + std::to_string(p_interpreter.line_at(top - 1)) + ") ";
	}
	return ++steps->count < steps->limit;
}

// A run that goes on too long is stopped with Flash's Error #1502, which no catch stops; the limit is for
// each call from outside, with what it calls.
static void test_time_limit() {
	Compilation compilation;
	compilation.add("Spin.as", "package { public class Spin {\n"
							   "  public var n:int;\n"
							   "  public function forever():void { while (true) {} }\n"
							   "  public function loops():void { for (var i:int = 0; ; i++) { n = i; } }\n"
							   "  public function down():void { do { n++; } while (true); }\n"
							   "  public function counting():void { for (var i:int = 0; ; i++) {} }\n"
							   "  public function empty():void { for (var j:int = 0; j >= 0; ) ; }\n"
							   "  public function skipping():void { while (true) { if (n > 0) continue; n++; } }\n"
							   "  public function caught():String { while (true) { try { for (;;) {} } catch (e:*) { n++; } finally { n++; } } return \"out\"; }\n"
							   "  public function quick():int { var s:int = 0; for (var i:int = 0; i < 1000; i++) { s += i; } return s; }\n"
							   "} }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	uint32_t spin = 0;
	for (size_t i = 0; i < module.classes.size(); i++) {
		spin = module.classes[i].name == "Spin" ? (uint32_t)i : spin;
	}
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		interpreter.set_time_limit(50);
		CHECK(interpreter.start());
		vm::Object *object = interpreter.construct(spin);
		for (const char *method : { "forever", "loops", "down", "counting", "empty", "skipping", "caught" }) {
			host.errors.clear();
			const auto started = std::chrono::steady_clock::now();
			vm::Slot result;
			result.bits = 0;
			CHECK(!interpreter.call_method(object, method, nullptr, 0, &result));
			const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
			CHECK(took >= 50 && took < 5000);
			CHECK_EQ(host.errors.substr(0, host.errors.find('\n')),
					std::string("Error: Error #1502: A script has executed for longer than the timeout period of 0.05 seconds."));
			CHECK(host.errors.find(std::string("at Spin/") + method + "()") != std::string::npos);
		}
		// The next call has its own time, and a short one is not stopped.
		host.errors.clear();
		vm::Slot sum;
		sum.bits = 0;
		CHECK(interpreter.call_method(object, "quick", nullptr, 0, &sum));
		CHECK_EQ(sum.i, 499500);
		CHECK_EQ(host.errors, std::string());
		interpreter.release_object(object);
	}
	// Flash's own words for its own limit, 15 seconds.
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK_EQ(interpreter.time_limit(), (uint32_t)0);
		CHECK_EQ(vm::Interpreter::FLASH_TIME_LIMIT, (uint32_t)15000);
		CHECK_EQ(vm::timeout_message(vm::Interpreter::FLASH_TIME_LIMIT),
				std::string("Error #1502: A script has executed for longer than the default timeout period of 15 seconds."));
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

static void test_statement_hook() {
	Compilation compilation;
	compilation.add("Main.as", PROGRAM);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
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
		CHECK_EQ(steps.seen, std::string("Main:4 Main:5 Main/twice:9 (from 5) Main/twice:10 (result=2) (from 5) Main:6 "));
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
	Unit *unit = compilation.add("Main.as", "package { public class Main { public function Main() { var r:* = /a+/; } } }");
	compilation.check();
	ir::Module module;
	CHECK(!ir::lower(compilation, &module));
	CHECK(unit->diagnostics.has_errors());
	CHECK(compilation.format_diagnostics(false).find("is not supported by the interpreter yet") != std::string::npos);
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

struct Reentrant : vm::Output {
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
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
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

// A script that extends a Godot class, run against a host that records what it is asked to do.
static const char *const PLAYER =
		"package {\n"
		"	import godot.*;\n"
		"	public class Player extends CharacterBody2D {\n"
		"		private var speed:Number = 200;\n"
		"		public var steps:int = 0;\n"
		"		override public function _physicsProcess(delta:Number):void {\n"
		"			velocity = new Vector2(speed, 0);\n"
		"			moveAndSlide();\n"
		"			steps++;\n"
		"		}\n"
		"		public function where():Number { var at:Vector2 = position; return at.x + delta2(); }\n"
		"		private function delta2():Number { return getPhysicsProcessDeltaTime(); }\n"
		"		public function rename(to:String):String { name = to; return name; }\n"
		"		public function wrong():void { setPhysicsProcess(true); queueFree(); }\n"
		"	}\n"
		"}\n";

// Stands in for the engine: every value it hands out is a numbered box, and it writes down each request.
struct FakeEngine : vm::Output, vm::EngineHandles, vm::EngineSource {
	std::string log, errors;
	int boxes = 0; // Handed out and not yet dropped.
	int next = 1;
	bool fail_calls = false;
	bool fail_sets = false;
	std::string name = "Player";

	static std::string show(const vm::Argument &p_argument) {
		switch (p_argument.kind) {
			case ir::Kind::F64:
				return vm::number_to_string(p_argument.slot.d);
			case ir::Kind::I32:
				return std::to_string(p_argument.slot.i);
			case ir::Kind::I64:
				return std::to_string(p_argument.slot.l);
			case ir::Kind::Bool:
				return p_argument.slot.b ? "true" : "false";
			case ir::Kind::Str:
				return p_argument.slot.p ? "\"" + std::string(static_cast<vm::Str *>(p_argument.slot.p)->view()) + "\"" : "null";
			default:
				return "#" + std::to_string((intptr_t)p_argument.slot.p);
		}
	}
	static std::string list(const vm::Argument *p_arguments, size_t p_count) {
		std::string out;
		for (size_t i = 0; i < p_count; i++) {
			out += (i ? ", " : "") + show(p_arguments[i]);
		}
		return out;
	}
	void *box() {
		boxes++;
		return (void *)(intptr_t)next++;
	}
	void give(ir::Kind p_kind, vm::Slot *r_result, double p_number, const std::string &p_text) {
		if (!r_result) {
			return;
		}
		r_result->bits = 0;
		switch (p_kind) {
			case ir::Kind::F64:
				r_result->d = p_number;
				break;
			case ir::Kind::I32:
				r_result->i = (int32_t)p_number;
				break;
			case ir::Kind::I64:
				r_result->l = (int64_t)p_number;
				break;
			case ir::Kind::Bool:
				r_result->b = p_number != 0;
				break;
			case ir::Kind::Str:
				r_result->p = vm::new_string(p_text);
				break;
			case ir::Kind::Val:
			case ir::Kind::Ref:
				r_result->p = box();
				break;
			default:
				break;
		}
	}
	void print(std::string_view p_text) override { log += "print " + std::string(p_text) + "\n"; }
	void error(std::string_view p_text) override { errors += std::string(p_text) + "\n"; }
	[[nodiscard]] void *attach_new(uint32_t p_class, vm::Object *, std::string *) override {
		void *made = box();
		log += "attach " + (module ? module->classes[p_class].name : std::to_string(p_class)) + " -> #" + std::to_string((intptr_t)made) + "\n";
		return made;
	}
	[[nodiscard]] void *construct(std::string_view p_type, const vm::Argument *p_arguments, size_t p_count, std::string *) override {
		void *made = box();
		log += "construct " + std::string(p_type) + "(" + list(p_arguments, p_count) + ") -> #" + std::to_string((intptr_t)made) + "\n";
		return made;
	}
	[[nodiscard]] void *collection(std::string_view p_type, ir::Kind p_kind, const vm::Slot *p_elements, size_t p_count, std::string *) override {
		void *made = box();
		std::vector<vm::Argument> elements;
		for (size_t i = 0; i < p_count; i++) {
			elements.push_back(vm::Argument{ p_kind, p_elements[i] });
		}
		log += "collection " + std::string(p_type) + "[" + list(elements.data(), p_count) + "] -> #" + std::to_string((intptr_t)made) + "\n";
		return made;
	}
	[[nodiscard]] bool call(void *p_receiver, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_wanted,
			vm::Slot *r_result, std::string *r_error) override {
		ir::Kind p_kind = p_wanted.kind;
		log += "call #" + std::to_string((intptr_t)p_receiver) + "." + std::string(p_method) + "(" + list(p_arguments, p_count) + ")\n";
		if (fail_calls) {
			*r_error = "the engine refused " + std::string(p_method);
			return false;
		}
		give(p_kind, r_result, 0.25, "text");
		return true;
	}
	[[nodiscard]] bool get(void *p_receiver, std::string_view p_property, const vm::Wanted &p_wanted, vm::Slot *r_result, std::string *) override {
		log += "get #" + std::to_string((intptr_t)p_receiver) + "." + std::string(p_property) + "\n";
		give(p_wanted.kind, r_result, 7.5, name);
		return true;
	}
	[[nodiscard]] bool index_get(void *p_receiver, const vm::Argument &p_key, const vm::Wanted &p_wanted, vm::Slot *r_result, std::string *) override {
		log += "get #" + std::to_string((intptr_t)p_receiver) + "[" + show(p_key) + "]\n";
		give(p_wanted.kind, r_result, 2.5, "text");
		return true;
	}
	[[nodiscard]] bool index_set(void *p_receiver, const vm::Argument &p_key, const vm::Argument &p_value, std::string *) override {
		log += "set #" + std::to_string((intptr_t)p_receiver) + "[" + show(p_key) + "] = " + show(p_value) + "\n";
		return true;
	}
	const ir::Module *module = nullptr;
	[[nodiscard]] void *bind(vm::Object *p_object, uint32_t p_function, void *p_owner, std::string *) override {
		void *made = box();
		log += "bind " + (module ? module->functions[p_function].name : std::to_string(p_function)) + " of " +
				(module ? module->classes[p_object->klass].name : std::string("?")) + " owner #" + std::to_string((intptr_t)p_owner) + " -> #" +
				std::to_string((intptr_t)made) + "\n";
		return made;
	}
	[[nodiscard]] bool member(void *p_receiver, std::string_view p_name, const vm::Wanted &p_wanted, vm::Slot *r_result, std::string *) override {
		log += "member #" + std::to_string((intptr_t)p_receiver) + "[" + std::string(p_name) + "] as " + std::string(p_wanted.type) + "\n";
		give(p_wanted.kind, r_result, 0, "");
		return true;
	}
	std::string missing; // A singleton, constant or static method the engine does not have.
	[[nodiscard]] bool evaluate(std::string_view p_expression, const vm::Wanted &p_wanted, vm::Slot *r_result, std::string *) override {
		log += "evaluate " + std::string(p_expression) + " as " + std::string(p_wanted.type) + "\n";
		give(p_wanted.kind, r_result, 0, "");
		return true;
	}
	[[nodiscard]] void *singleton(std::string_view p_class, std::string *r_error) override {
		if (p_class == missing) {
			*r_error = "Godot has no '" + missing + "' here";
			return nullptr;
		}
		void *made = box();
		log += "singleton " + std::string(p_class) + " -> #" + std::to_string((intptr_t)made) + "\n";
		return made;
	}
	[[nodiscard]] bool constant(std::string_view p_class, std::string_view p_name, const vm::Wanted &p_wanted, vm::Slot *r_result, std::string *r_error) override {
		log += "constant " + std::string(p_class) + "." + std::string(p_name) + " as " + std::string(p_wanted.type) + "\n";
		if (p_name == missing) {
			*r_error = "Godot has no constant '" + missing + "'";
			return false;
		}
		give(p_wanted.kind, r_result, 0, "");
		return true;
	}
	[[nodiscard]] bool call_static(std::string_view p_class, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count,
			const vm::Wanted &p_wanted, vm::Slot *r_result, std::string *r_error) override {
		log += "static " + std::string(p_class) + "." + std::string(p_method) + "(" + list(p_arguments, p_count) + ")\n";
		if (p_method == missing) {
			*r_error = "Godot has no '" + missing + "'";
			return false;
		}
		give(p_wanted.kind, r_result, 0.5, "text");
		return true;
	}
	[[nodiscard]] bool present(void *p_value) override { return p_value != nullptr; }
	[[nodiscard]] bool is_class(void *p_value, std::string_view p_class) override {
		log += "is #" + std::to_string((intptr_t)p_value) + " " + std::string(p_class) + "\n";
		return p_value != nullptr;
	}
	[[nodiscard]] vm::Str *text(void *p_value) override {
		log += "text #" + std::to_string((intptr_t)p_value) + "\n";
		return vm::new_string("<#" + std::to_string((intptr_t)p_value) + ">");
	}
	[[nodiscard]] bool operate(std::string_view p_operator, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_wanted, vm::Slot *r_result,
			std::string *r_error) override {
		log += "operate " + std::string(p_operator) + " (" + list(p_arguments, p_count) + ") as " + ir::kind_name(p_wanted.kind) +
				(p_wanted.type.empty() ? "" : " " + std::string(p_wanted.type)) + "\n";
		if (fail_calls) {
			*r_error = "the engine refused " + std::string(p_operator);
			return false;
		}
		give(p_wanted.kind, r_result, 1, "text");
		return true;
	}
	[[nodiscard]] bool set(void *p_receiver, std::string_view p_property, const vm::Argument &p_value, std::string *r_error) override {
		log += "set #" + std::to_string((intptr_t)p_receiver) + "." + std::string(p_property) + " = " + show(p_value) + "\n";
		if (fail_sets) {
			*r_error = "the engine refused to set " + std::string(p_property);
			return false;
		}
		if (p_property == "name" && p_value.slot.p) {
			name = std::string(static_cast<vm::Str *>(p_value.slot.p)->view());
		}
		return true;
	}
	[[nodiscard]] void *copy(void *p_value) override {
		if (p_value) {
			boxes++;
		}
		return p_value;
	}
	void drop(void *p_value) override {
		if (p_value) {
			boxes--;
		}
	}
	// What keep() answers, and what it does first: free another object the engine kept, as Godot freeing one
	// object can free another.
	bool keeps = false;
	vm::Interpreter *interpreter = nullptr;
	vm::Object *free_first = nullptr;
	[[nodiscard]] bool keep(vm::Object *p_object) override {
		log += "keep " + (module ? module->classes[p_object->klass].name : std::string("?")) + "\n";
		if (free_first && interpreter) {
			vm::Object *other = free_first;
			free_first = nullptr;
			interpreter->free_kept(other);
		}
		return keeps;
	}
};

// A host that only prints implements Output alone, and a program that asks for the engine is told
// there is none.
static_assert(!std::is_base_of<vm::EngineHandles, vm::StandaloneOutput>::value, "the stand-alone host answers no engine entry");
static_assert(!std::is_base_of<vm::EngineSource, Recorder>::value, "nor does the tests' recorder");

// A script object whose Godot object is reference-counted is kept when the program lets go of it, if the
// engine says it still holds the object, and freed with what it holds when the engine lets go too.
static void test_kept_by_the_engine() {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Counted.as", "package { import godot.*; public class Counted extends RefCounted { public var text:String = \"kept\"; } }");
	compilation.add("Plain.as", "package { import godot.*; public class Plain extends Node { public var text:String = \"plain\"; } }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	uint32_t counted = 0, plain = 0;
	for (size_t i = 0; i < module.classes.size(); i++) {
		counted = module.classes[i].name == "Counted" ? (uint32_t)i : counted;
		plain = module.classes[i].name == "Plain" ? (uint32_t)i : plain;
	}
	CHECK(module.classes[counted].counted);
	CHECK(!module.classes[plain].counted);
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		engine.interpreter = &interpreter;
		CHECK(interpreter.start());
		const size_t before = vm::live_objects();
		// Kept: the object and its field stay until the engine lets go.
		engine.keeps = true;
		vm::Object *object = interpreter.construct(counted, engine.box());
		vm::Object *second = interpreter.construct(counted, engine.box());
		interpreter.release_object(object);
		interpreter.release_object(second);
		CHECK_EQ(engine.log, std::string("keep Counted\nkeep Counted\n"));
		CHECK_EQ(vm::live_objects(), before + 2);
		CHECK_EQ(std::string(static_cast<vm::Str *>(object->fields()[1].p)->view()), std::string("kept"));
		// The engine lets go of the first; the engine's freeing it frees the second on the way.
		engine.keeps = false;
		engine.free_first = second;
		engine.log.clear();
		interpreter.free_kept(object);
		CHECK_EQ(engine.log, std::string("keep Counted\nkeep Counted\n"));
		CHECK_EQ(vm::live_objects(), before);
		// Not kept: freed at once. Another class is never asked.
		engine.log.clear();
		interpreter.release_object(interpreter.construct(counted, engine.box()));
		interpreter.release_object(interpreter.construct(plain, engine.box()));
		CHECK_EQ(engine.log, std::string("keep Counted\n"));
		CHECK_EQ(vm::live_objects(), before);
		CHECK_EQ(engine.boxes, 0);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

static void test_no_engine() {
	const char *source = "package { import godot.*; public class Maker extends Node {\n"
						 "  public function make():Vector2 { return new Vector2(1, 2); }\n"
						 "  public function ask():Boolean { return Input.isActionPressed(\"jump\"); }\n"
						 "} }\n";
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Maker.as", source);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	{
		Recorder recorder;
		vm::Interpreter interpreter(module, recorder);
		CHECK(interpreter.start());
		uint32_t maker = 0;
		for (size_t i = 0; i < module.classes.size(); i++) {
			maker = module.classes[i].name == "Maker" ? (uint32_t)i : maker;
		}
		vm::Object *object = interpreter.construct(maker, nullptr);
		CHECK(object != nullptr);
		vm::Slot result;
		CHECK(!interpreter.call_method(object, "make", nullptr, 0, &result));
		CHECK(recorder.errors.find("Error: there is no engine here") == 0);
		recorder.errors.clear();
		CHECK(!interpreter.call_method(object, "ask", nullptr, 0, &result));
		CHECK(recorder.errors.find("Error: there is no engine here") == 0);
		interpreter.release_object(object);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// A method where Godot takes a function goes bound to its object; a signal by name is the engine's to find.
static void test_engine_listeners() {
	const char *base = "package { import godot.*;\n"
					   "public class Base extends Node {\n"
					   "  private function mine():void {}\n"
					   "  public function shared():void {}\n"
					   "  public function listenBase():void { renamed.add(mine); renamed.add(shared); }\n"
					   "} }\n";
	const char *source = "package { import godot.*;\n"
						 "public class Child extends Base {\n"
						 "  private function mine():void {}\n"
						 "  override public function shared():void {}\n"
						 "  public function listen(other:Child, name:String):void {\n"
						 "    renamed.add(this.mine, true);\n"
						 "    other.renamed.addOnce(other.shared);\n"
						 "    other[name].remove(mine);\n"
						 "  }\n"
						 "  public function nobody():void { var other:Child; renamed.add(other.shared); }\n"
						 "  public static function wire(other:Child):void { other.renamed.add(other.shared); }\n"
						 "} }\n";
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Base.as", base);
	compilation.add("Child.as", source);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		uint32_t child = 0;
		for (size_t i = 0; i < module.classes.size(); i++) {
			child = module.classes[i].name == "Child" ? (uint32_t)i : child;
		}
		vm::Object *object = interpreter.construct(child, engine.box()); // #1
		vm::Object *other = interpreter.construct(child, engine.box()); // #2
		CHECK(object != nullptr && other != nullptr);
		// A private method is the one written in that class; any other is the object's own override.
		engine.log.clear();
		CHECK(interpreter.call_method(object, "listenBase", nullptr, 0, nullptr));
		CHECK_EQ(engine.log, std::string("get #1.renamed\nbind Base/mine of Child owner #1 -> #4\ncall #3.@add(#4)\n"
										 "get #1.renamed\nbind Child/shared of Child owner #1 -> #6\ncall #5.@add(#6)\n"));
		// The object written before the method is the one bound; a name known only now goes to the engine.
		engine.log.clear();
		vm::Slot arguments[2];
		arguments[0].bits = 0;
		arguments[0].p = other;
		arguments[1].bits = 0;
		arguments[1].p = vm::new_string("renamed");
		CHECK(interpreter.call_method(object, "listen", arguments, 2, nullptr));
		CHECK_EQ(engine.log, std::string("get #1.renamed\nbind Child/mine of Child owner #1 -> #8\ncall #7.@add(#8, true)\n"
										 "get #2.renamed\nbind Child/shared of Child owner #2 -> #10\ncall #9.@addOnce(#10)\n"
										 "bind Child/mine of Child owner #1 -> #11\nmember #2[renamed] as Signal\ncall #12.@remove(#11)\n"));
		vm::free_string(static_cast<vm::Str *>(arguments[1].p));
		CHECK_EQ(engine.boxes, 2);
		// A method of a missing object is an AS3 error.
		CHECK(!interpreter.call_method(object, "nobody", nullptr, 0, nullptr));
		CHECK(engine.errors.find("TypeError: Error #1009") == 0);
		interpreter.release_object(object);
		interpreter.release_object(other);
		CHECK_EQ(engine.boxes, 0);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// A singleton is an object the engine looks up; a constant and a static method are the engine's to answer.
static void test_engine_statics() {
	const char *source = "package { import godot.*; public class Pad extends Node {\n"
						 "  private static var ticks:int = Engine.physicsTicksPerSecond;\n"
						 "  private var spot:Vector2;\n"
						 "  private var items:GodotArray;\n"
						 "  private var held:Vector2 = null;\n"
						 "  public function pressed(action:String):Boolean { return Input.isActionPressed(action, true); }\n"
						 "  public function slow(n:Number):void { Engine.timeScale = n; Engine.timeScale += n; }\n"
						 "  public function listen():void { Input.joyConnectionChanged.add(onJoy); }\n"
						 "  private function onJoy(device:int, connected:Boolean):void {}\n"
						 "  public function itself():GodotObject { return Input.singleton; }\n"
						 "  public function numbers():Number { return Key.ESCAPE + NOTIFICATION_READY + Input.MOUSE_MODE_CAPTURED; }\n"
						 "  public function up():Number { return Vector2.UP.y; }\n"
						 "  public function made(a:Number, b:Number):Vector2 { return Vector2.fromAngle(a + b); }\n"
						 "  public function shown():void { Node.printOrphanNodes(); }\n"
						 "  public function mixed(a:Number):Number { return lerpf(a, 2, 0.5); }\n"
						 "  private function note():int { trace(\"note\"); return 0; }\n"
						 "  public function order(other:Pad):Node { return other.getChild(note()); }\n"
						 "  public function told(n:int):void { print(\"n is \", n); }\n"
						 "  public function start():int { return ticks; }\n"
						 "  public function unset():Vector2 { var v:Vector2; return v; }\n"
						 "  public function twice():String { var out:String = \"\"; for (var i:int = 0; i < 2; i++) { var v:Vector2; out += v; v = null; } return out; }\n"
						 "  public function field():Vector2 { return spot; }\n"
						 "  public function nothing():Boolean { return items == null && held == null; }\n"
						 "} }\n";
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Pad.as", source);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		uint32_t pad = 0;
		for (size_t i = 0; i < module.classes.size(); i++) {
			pad = module.classes[i].name == "Pad" ? (uint32_t)i : pad;
		}
		engine.log.clear();
		vm::Object *object = interpreter.construct(pad, engine.box()); // #1
		CHECK(object != nullptr);
		// The class is used for the first time: its static field is set from the singleton. Then the object's
		// fields of a Godot value with parts start at their zero, a field with a value written too (an
		// initializer above it may read it).
		CHECK_EQ(engine.log, std::string("singleton Engine -> #2\nget #2.physics_ticks_per_second\nconstruct Vector2() -> #3\n"
										 "construct Vector2() -> #4\n"));
		engine.log.clear();
		vm::Slot result;
		result.bits = 0;
		CHECK(interpreter.call_method(object, "start", nullptr, 0, &result));
		CHECK_EQ(engine.log, std::string());
		CHECK_EQ(result.i, 7);
		// A singleton's method, property and signal are the object's.
		engine.log.clear();
		vm::Slot arguments[2];
		arguments[0].bits = 0;
		arguments[0].p = vm::new_string("jump");
		CHECK(interpreter.call_method(object, "pressed", arguments, 1, &result));
		CHECK_EQ(engine.log, std::string("singleton Input -> #5\ncall #5.is_action_pressed(\"jump\", true)\n"));
		vm::free_string(static_cast<vm::Str *>(arguments[0].p));
		engine.log.clear();
		arguments[0].bits = 0;
		arguments[0].d = 0.5;
		CHECK(interpreter.call_method(object, "slow", arguments, 1, nullptr));
		// A singleton is looked up once, then kept: Engine was found when the class was set up.
		CHECK_EQ(engine.log, std::string("set #2.time_scale = 0.5\nget #2.time_scale\nset #2.time_scale = 8\n"));
		engine.log.clear();
		CHECK(interpreter.call_method(object, "listen", nullptr, 0, nullptr));
		CHECK_EQ(engine.log, std::string("get #5.joy_connection_changed\nbind Pad/onJoy of Pad owner #1 -> #7\ncall #6.@add(#7)\n"));
		engine.log.clear();
		CHECK(interpreter.call_method(object, "itself", nullptr, 0, &result));
		CHECK_EQ(engine.log, std::string());
		CHECK_EQ((intptr_t)result.p, (intptr_t)5);
		engine.drop(result.p);
		// A constant with a value in the declarations asks the engine nothing.
		engine.log.clear();
		CHECK(interpreter.call_method(object, "numbers", nullptr, 0, &result));
		CHECK_EQ(engine.log, std::string());
		CHECK_EQ(result.d, 4194305.0 + 13.0 + 2.0);
		engine.log.clear();
		CHECK(interpreter.call_method(object, "up", nullptr, 0, &result));
		CHECK_EQ(engine.log, std::string("constant Vector2.UP as Vector2\nget #8.y\n"));
		// A static method is called on no object.
		engine.log.clear();
		arguments[0].d = 1;
		arguments[1].bits = 0;
		arguments[1].d = 2;
		CHECK(interpreter.call_method(object, "made", arguments, 2, &result));
		CHECK_EQ(engine.log, std::string("static Vector2.from_angle(3)\n"));
		engine.drop(result.p);
		engine.log.clear();
		CHECK(interpreter.call_method(object, "shown", nullptr, 0, nullptr));
		CHECK_EQ(engine.log, std::string("static Node.print_orphan_nodes 3218959716()\n"));
		// A global function is called on no class, with its signature for the engine's side.
		engine.log.clear();
		arguments[0].d = 1;
		CHECK(interpreter.call_method(object, "mixed", arguments, 1, &result));
		CHECK_EQ(engine.log, std::string("static .lerpf 998901048 float float,float,float(1, 2, 0.5)\n"));
		engine.log.clear();
		arguments[0].bits = 0;
		arguments[0].i = 7;
		CHECK(interpreter.call_method(object, "told", arguments, 1, nullptr));
		CHECK_EQ(engine.log, std::string("static .print 2648703342 void - varargs(\"n is \", 7)\n"));
		// A Godot member of a missing script object: the arguments are worked out first, as for one of
		// AS3's own members, and then the missing object is reported.
		engine.log.clear();
		arguments[0].bits = 0;
		CHECK(!interpreter.call_method(object, "order", arguments, 1, &result));
		CHECK_EQ(engine.log, std::string("print note\n"));
		CHECK(engine.errors.find("TypeError: Error #1009") == 0);
		engine.errors.clear();
		// A local of a Godot value with parts starts at its zero, once: the declaration is not run again.
		engine.log.clear();
		CHECK(interpreter.call_method(object, "unset", nullptr, 0, &result));
		CHECK_EQ(engine.log, std::string("construct Vector2() -> #10\n"));
		engine.drop(result.p);
		engine.log.clear();
		CHECK(interpreter.call_method(object, "twice", nullptr, 0, &result));
		CHECK_EQ(engine.log, std::string("construct Vector2() -> #11\ntext #11\n"));
		CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("<#11>null"));
		vm::free_string(static_cast<vm::Str *>(result.p));
		CHECK(interpreter.call_method(object, "field", nullptr, 0, &result));
		CHECK_EQ((intptr_t)result.p, (intptr_t)3);
		engine.drop(result.p);
		// A Godot value without parts, and one set to null, are missing.
		CHECK(interpreter.call_method(object, "nothing", nullptr, 0, &result));
		CHECK(result.b);
		CHECK_EQ(engine.boxes, 4); // The owner, the field, and the two singletons kept.
		// What the engine does not have is an AS3 error, and nothing is left behind. (A singleton not yet
		// looked up: this interpreter has Input already.)
		CHECK_EQ(engine.errors, std::string());
		{
			vm::Interpreter fresh(module, engine);
			CHECK(fresh.start());
			vm::Object *other = fresh.construct(pad, engine.box());
			engine.missing = "Input";
			CHECK(!fresh.call_method(other, "listen", nullptr, 0, nullptr));
			CHECK(engine.errors.find("Error: Godot has no 'Input' here") == 0);
			engine.errors.clear();
			engine.missing.clear();
			fresh.release_object(other);
		}
		engine.missing = "UP";
		CHECK(!interpreter.call_method(object, "up", nullptr, 0, &result));
		CHECK(engine.errors.find("Error: Godot has no constant 'UP'") == 0);
		engine.errors.clear();
		engine.missing = "from_angle";
		CHECK(!interpreter.call_method(object, "made", arguments, 2, &result));
		CHECK(engine.errors.find("Error: Godot has no 'from_angle'") == 0);
		CHECK_EQ(engine.boxes, 4);
		interpreter.release_object(object);
		CHECK_EQ(engine.boxes, 2); // The singletons, until the interpreter goes.
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// Operators on Godot's values are the engine's to carry out.
static void test_engine_operators() {
	const char *source = "package { import godot.*; public class Mover extends Node2D {\n"
						 "  public function step(by:Vector2, n:int):Boolean {\n"
						 "    var v:Vector2 = position + by * n;\n"
						 "    v -= by;\n"
						 "    return -v == by;\n"
						 "  }\n"
						 "  public function nobody():Vector2 { var v:Vector2 = null; return v + v; }\n"
						 "  public function isNobody(n:Node):Boolean { return n == this.getParent() || n != null; }\n"
						 "  public function say(v:Vector2, n:Node):String { trace(v); return \"v=\" + v + n.toString() + String(n); }\n"
						 "  public function sayNothing():String { var n:Node; var v:Vector2 = null; trace(n); return v + \"\"; }\n"
						 "  public function sayNobody():String { var n:Node; return n.toString(); }\n"
						 "  public function nudge(n:Number):void { position.x = n; }\n"
						 "  public function nudgeMore(n:Number):void { transform.origin.x += n; }\n"
						 "  public function element(i:int, n:Number):Number { position[i] += n; return position[i + 1]; }\n"
						 "} }\n";
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Mover.as", source);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	{
		FakeEngine engine;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		uint32_t mover = 0;
		for (size_t i = 0; i < module.classes.size(); i++) {
			mover = module.classes[i].name == "Mover" ? (uint32_t)i : mover;
		}
		vm::Object *object = interpreter.construct(mover, engine.box()); // #1
		CHECK(object != nullptr);
		vm::Slot arguments[2];
		arguments[0].bits = 0;
		arguments[0].p = engine.box(); // #2: the caller's, and the caller's to drop.
		arguments[1].bits = 0;
		arguments[1].i = 3;
		vm::Slot result;
		engine.log.clear();
		CHECK(interpreter.call_method(object, "step", arguments, 2, &result));
		// Left to right, each operand as the checker typed it, and each result as the type the checker gave.
		CHECK_EQ(engine.log, std::string("get #1.position\n"
										 "operate * (#2, 3) as GodotValue Vector2\n"
										 "operate + (#3, #4) as GodotValue Vector2\n"
										 "operate - (#5, #2) as GodotValue Vector2\n"
										 "operate - (#6) as GodotValue Vector2\n"
										 "operate == (#7, #2) as Boolean\n"));
		CHECK(result.b);
		CHECK_EQ(engine.boxes, 2); // The owner and the caller's argument: every other handle was dropped.
		// Arithmetic on a missing value is an AS3 error before the engine is asked.
		engine.log.clear();
		CHECK(!interpreter.call_method(object, "nobody", nullptr, 0, nullptr));
		CHECK_EQ(engine.log, std::string());
		CHECK(engine.errors.find("TypeError: Error #1009") == 0);
		// A comparison takes a missing value.
		engine.log.clear();
		engine.errors.clear();
		vm::Slot none;
		none.bits = 0;
		CHECK(interpreter.call_method(object, "isNobody", &none, 1, &result));
		CHECK_EQ(engine.log, std::string("call #1.get_parent()\noperate == (#0, #8) as Boolean\n"));
		CHECK_EQ(engine.errors, std::string());
		// Text is the engine's; a missing value reads "null" without asking it.
		engine.log.clear();
		vm::Slot pair[2];
		pair[0] = arguments[0];
		pair[1].bits = 0;
		pair[1].p = engine.box(); // #9
		CHECK(interpreter.call_method(object, "say", pair, 2, &result));
		CHECK_EQ(engine.log, std::string("text #2\nprint <#2>\ntext #2\ntext #9\ntext #9\n"));
		CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("v=<#2><#9><#9>"));
		vm::free_string(static_cast<vm::Str *>(result.p));
		engine.drop(pair[1].p);
		engine.log.clear();
		CHECK(interpreter.call_method(object, "sayNothing", nullptr, 0, &result));
		CHECK_EQ(engine.log, std::string("print null\n"));
		CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("null"));
		vm::free_string(static_cast<vm::Str *>(result.p));
		CHECK(!interpreter.call_method(object, "sayNobody", nullptr, 0, nullptr));
		CHECK(engine.errors.find("TypeError: Error #1009") == 0);
		engine.errors.clear();
		// One part of a value: the value is read after the new part is worked out, changed, and written back.
		engine.log.clear();
		engine.next = 20;
		vm::Slot amount;
		amount.bits = 0;
		amount.d = 5;
		CHECK(interpreter.call_method(object, "nudge", &amount, 1, nullptr));
		CHECK_EQ(engine.log, std::string("get #1.position\nset #20.x = 5\nset #1.position = #20\n"));
		engine.log.clear();
		CHECK(interpreter.call_method(object, "nudgeMore", &amount, 1, nullptr));
		CHECK_EQ(engine.log, std::string("get #1.transform\nget #21.origin\nget #22.x\nset #22.x = 12.5\nset #21.origin = #22\n"
										 "set #1.transform = #21\n"));
		CHECK_EQ(engine.boxes, 2);
		// An element: the key is worked out once.
		engine.log.clear();
		engine.next = 30;
		vm::Slot element[2];
		element[0].bits = 0;
		element[0].i = 1;
		element[1].bits = 0;
		element[1].d = 4;
		CHECK(interpreter.call_method(object, "element", element, 2, &result));
		CHECK_EQ(engine.log, std::string("get #1.position\nget #30[1]\nset #30[1] = 6.5\nset #1.position = #30\nget #1.position\nget #31[2]\n"));
		CHECK_EQ(result.d, 2.5);
		CHECK_EQ(engine.boxes, 2);
		// A write the engine refuses leaves no handle behind.
		engine.fail_sets = true;
		CHECK(!interpreter.call_method(object, "nudge", &amount, 1, nullptr));
		CHECK(engine.errors.find("Error: the engine refused to set x") == 0);
		CHECK_EQ(engine.boxes, 2);
		engine.fail_sets = false;
		engine.errors.clear();
		// An operator the engine refuses is an AS3 error, and nothing is left behind.
		engine.fail_calls = true;
		CHECK(!interpreter.call_method(object, "step", arguments, 2, nullptr));
		CHECK(engine.errors.find("Error: the engine refused *") == 0);
		interpreter.release_object(object);
		CHECK_EQ(engine.boxes, 1);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

static void test_engine_calls() {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Player.as", PLAYER);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	uint32_t player = ir::NONE;
	for (size_t i = 0; i < module.classes.size(); i++) {
		if (module.classes[i].name == "Player") {
			player = (uint32_t)i;
		}
	}
	CHECK(player != ir::NONE);
	if (player == ir::NONE) {
		return;
	}
	// The class says which Godot class it extends and which of its methods answer the engine's virtuals.
	CHECK_EQ(module.classes[player].native_base, std::string("CharacterBody2D"));
	CHECK_EQ(module.classes[player].virtuals.size(), (size_t)1);
	CHECK_EQ(module.classes[player].virtuals[0].first, std::string("_physics_process"));
	{
		FakeEngine engine;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		void *owner = engine.box(); // #1: the node the script is attached to.
		vm::Object *object = interpreter.construct(player, owner);
		CHECK(object != nullptr);
		engine.log.clear();
		vm::Slot delta;
		delta.bits = 0;
		delta.d = 1.0 / 60;
		for (int frame = 0; frame < 2; frame++) {
			CHECK(interpreter.call_method(object, "_physicsProcess", &delta, 1, nullptr));
		}
		CHECK_EQ(engine.log, std::string("construct Vector2(200, 0) -> #2\n"
										 "set #1.velocity = #2\n"
										 "call #1.move_and_slide()\n"
										 "construct Vector2(200, 0) -> #3\n"
										 "set #1.velocity = #3\n"
										 "call #1.move_and_slide()\n"));
		// The AS3 field counted on its own side.
		vm::Slot steps;
		CHECK(interpreter.get_field(object, "steps", &steps));
		CHECK_EQ(steps.i, 2);
		CHECK(!interpreter.get_field(object, "missing", &steps));
		// Every box handed out during the calls was given back: only the owner's handle, now the script
		// object's own, remains.
		CHECK_EQ(engine.boxes, 1);

		// A property read into a local, a member of a value, a call that returns a number.
		engine.log.clear();
		vm::Slot result;
		CHECK(interpreter.call_method(object, "where", nullptr, 0, &result));
		CHECK_EQ(result.d, 7.75);
		CHECK_EQ(engine.log, std::string("get #1.position\nget #4.x\ncall #1.get_physics_process_delta_time()\n"));
		CHECK_EQ(engine.boxes, 1);
		// Strings cross both ways.
		engine.log.clear();
		vm::Slot text;
		text.bits = 0;
		text.p = vm::new_string("Hero");
		CHECK(interpreter.call_method(object, "rename", &text, 1, &result));
		vm::Str *renamed = static_cast<vm::Str *>(result.p);
		CHECK(renamed && std::string(renamed->view()) == "Hero");
		CHECK_EQ(engine.log, std::string("set #1.name = \"Hero\"\nget #1.name\n"));
		for (vm::Str *string : { static_cast<vm::Str *>(text.p), renamed }) {
			if (vm::release(string)) {
				vm::free_string(string);
			}
		}
		// A call the engine refuses is an AS3 error with the engine's words, at the right line.
		engine.fail_calls = true;
		engine.log.clear();
		CHECK(!interpreter.call_method(object, "wrong", nullptr, 0, nullptr));
		CHECK_EQ(engine.log, std::string("call #1.set_physics_process(true)\n"));
		CHECK(engine.errors.find("Error: the engine refused set_physics_process\n\tat Player/wrong()[Player.as:14]") == 0);
		interpreter.release_object(object);
		CHECK_EQ(engine.boxes, 0); // The script object gave up its handle on its owner.
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);

	// An override of an override: the engine's virtual goes to the last one; `super` on one of Godot's
	// virtuals does nothing more (Godot's own does nothing); a class may name one from a later file.
	{
		Compilation program;
		program.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
		program.add("A.as", "package { import godot.*; public class A extends Node { public var other:Z; public var n:int;\n"
							"override public function _ready():void { n++; super._ready(); }\n"
							"public function need(a:int, b:int, c:int = 3):int { return a; } } }");
		program.add("Z.as", "package { public class Z extends A { override public function _ready():void { n += 100; } } }");
		program.check();
		CHECK_EQ(program.format_diagnostics(false), std::string());
		ir::Module out;
		CHECK(ir::lower(program, &out, ir::LowerMode::Library));
		CHECK_EQ(program.format_diagnostics(false), std::string());
		std::string found;
		for (const ir::Class &klass : out.classes) {
			for (const auto &entry : klass.virtuals) {
				found += klass.name + ":" + entry.first + "=" + out.functions[entry.second].name + " ";
			}
		}
		CHECK_EQ(found, std::string("A:_ready=A/_ready Z:_ready=Z/_ready "));
		for (const ir::Function &function : out.functions) {
			if (function.name == "A/_ready") {
				for (const ir::Instruction &instruction : function.code) {
					CHECK(instruction.op != ir::Op::HostCall);
				}
			}
			if (function.name == "A/need") {
				CHECK_EQ(function.required, 3u); // `this` and the two parameters without a default.
			}
		}
	}
	// A handle whose release calls back into the interpreter (an object's destructor running a script):
	// the registers it sits in are already let go of.
	{
		struct Vengeful : FakeEngine {
			vm::Interpreter *interpreter = nullptr;
			vm::Object *object = nullptr;
			int reentered = 0;
			void drop(void *p_value) override {
				FakeEngine::drop(p_value);
				if (p_value && interpreter && object && reentered < 50) {
					reentered++;
					vm::Slot result;
					interpreter->call_method(object, "where", nullptr, 0, &result);
				}
			}
		};
		Vengeful engine;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		void *owner = engine.box();
		vm::Object *object = interpreter.construct(player, owner);
		engine.interpreter = &interpreter;
		engine.object = object;
		vm::Slot result;
		CHECK(interpreter.call_method(object, "where", nullptr, 0, &result));
		CHECK(engine.reentered > 10);
		CHECK_EQ(engine.errors, std::string());
		engine.interpreter = nullptr;
		interpreter.release_object(object);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);

	// What this slice does not do yet is refused by name.
	auto refused = [](const char *p_body) {
		Compilation program;
		program.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
		program.add("Player.as", std::string("package { import godot.*; public class Player extends Node2D {\n") + p_body + "\n} }");
		program.check();
		std::string checked = program.format_diagnostics(false);
		ir::Module out;
		bool lowered = ir::lower(program, &out, ir::LowerMode::Library);
		std::string text = program.format_diagnostics(false);
		return std::string(checked.empty() ? "" : "CHECK ") + (lowered ? "LOWERED " : "") + text;
	};
	const char *bodies[] = {
		"function f():void { Vector2.ZERO.x = 5; }",
		"function f():void { Vector2.ZERO[0] = 5; }",
		"function f():void { Vector2.ZERO.x++; }",
		"function f(n:Node):void { var x:int = n[\"name\"].length; }",
		"function f():void { position.normalized().x = 5; }",
		"function f():void { position.normalized()[0] = 5; }",
		"function f(v:Vector2):int { return v ? 1 : 0; }",
		"function f(a:GodotArray):void { if (a) { trace(1); } }",
	};
	for (const char *body : bodies) {
		std::string result = refused(body);
		check::checks()++;
		if (result.find("CHECK ") == 0 || result.find("LOWERED") != std::string::npos ||
				result.find("is not supported by the interpreter yet") == std::string::npos) {
			check::failures()++;
			std::printf("FAIL not refused: %s\n  %s\n", body, result.c_str());
		}
	}
}

// A loop's condition is somewhere a debugger can stop on every pass.
static uint32_t class_named(const ir::Module &p_module, const char *p_name) {
	for (size_t i = 0; i < p_module.classes.size(); i++) {
		if (p_module.classes[i].name == p_name) {
			return (uint32_t)i;
		}
	}
	return ir::NONE;
}

static void compile(const char *p_source, ir::Module *r_module, const char *p_file = "Counter.as") {
	Compilation compilation;
	compilation.add(p_file, p_source);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	CHECK(ir::lower(compilation, r_module, ir::LowerMode::Library));
}

// Which classes changed code that runs once (a constructor, the field values it sets, `_ready`, and what
// those call), so that a reload knows the running objects will not show it.
static void test_run_once_changes() {
	const char *base = "package { public class Game {\n"
					   "  public var speed:Number = 300; public static var best:int = 1;\n"
					   "  public function _ready():void { setup(); }\n"
					   "  private function setup():void { speed += 1; }\n"
					   "  public function _process(delta:Number):void { speed += delta; }\n"
					   "} }\n";
	const auto changes = [&](const std::string &p_after) {
		ir::Module before, after;
		compile(base, &before, "Game.as");
		compile(p_after.c_str(), &after, "Game.as");
		const std::vector<std::string> found = ir::run_once_changes(before, after);
		std::string out;
		for (const std::string &name : found) {
			out += name + ";";
		}
		return out;
	};
	std::string same = base;
	CHECK_EQ(changes(same), std::string());
	// Moving code down a line, or changing what runs every frame, is not a change to what ran once.
	CHECK_EQ(changes(std::string("\n\n") + base), std::string());
	std::string frame = base;
	frame.replace(frame.find("speed += delta"), 14, "speed += delta * 2");
	CHECK_EQ(changes(frame), std::string());
	std::string field = base;
	field.replace(field.find("= 300"), 5, "= 500");
	CHECK_EQ(changes(field), std::string("Game;"));
	std::string ready = base;
	ready.replace(ready.find("{ setup(); }"), 12, "{ setup(); speed = 2; }");
	CHECK_EQ(changes(ready), std::string("Game;"));
	std::string called = base;
	called.replace(called.find("speed += 1;"), 11, "speed += 5;");
	CHECK_EQ(changes(called), std::string("Game;"));
	// A static field keeps its value through a reload, so starting the scene again would not show it.
	std::string statics = base;
	statics.replace(statics.find("best:int = 1"), 12, "best:int = 9");
	CHECK_EQ(changes(statics), std::string());
	// A new field with a first value: objects alive have it empty.
	std::string added = base;
	added.replace(added.find("public function _process"), 23, "public var lives:int = 3; public function _process");
	CHECK_EQ(changes(added), std::string("Game;"));
	// A field renamed: objects alive have the new one empty, though their code has run.
	std::string renamed = base;
	for (size_t at = renamed.find("speed"); at != std::string::npos; at = renamed.find("speed", at)) {
		renamed.replace(at, 5, "pace");
	}
	CHECK_EQ(changes(renamed), std::string("Game;"));
	// Fields and methods added before the others move what comes after: nothing that ran changed.
	std::string field_before = base;
	field_before.replace(field_before.find("public var speed"), 16, "public var unused:int; public var speed");
	CHECK_EQ(changes(field_before), std::string());
	std::string method_before = base;
	method_before.replace(method_before.find("private function setup"), 22, "public function unused():void {} private function setup");
	CHECK_EQ(changes(method_before), std::string());
}

// Which classes have objects that a reload moved: a class that changed but has none alive needs no restart.
static void test_reload_alive() {
	const char *source = "package { public class Game { public var held:Shot = new Shot(); } }\n"
						 "class Shot { public var n:int = 1; }\n"
						 "class Unused { public var n:int = 1; }\n";
	ir::Module old_module, new_module;
	compile(source, &old_module, "Game.as");
	compile(source, &new_module, "Game.as");
	{
		Recorder recorder;
		vm::Interpreter running(old_module, recorder);
		CHECK(running.start());
		vm::Object *game = running.construct(class_named(old_module, "Game"), nullptr);
		vm::Interpreter fresh(new_module, recorder);
		{
			vm::Reload reload(running, fresh);
			vm::Object *moved = reload.object(game);
			CHECK(reload.alive({ "Shot" }));
			CHECK(reload.alive({ "Unused", "Game" }));
			CHECK(!reload.alive({ "Unused" }));
			CHECK(!reload.alive({}));
			fresh.release_object(moved);
		}
		running.release_object(game);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// A base class's `_ready` that calls a method a subclass overrides: the override is what runs.
static void test_run_once_overrides() {
	const char *base = "package { public class Game extends Base {\n"
					   "  override protected function setup():void { n = 2; }\n"
					   "} }\n"
					   "class Base { public var n:int; public function _ready():void { setup(); } protected function setup():void { n = 1; } }\n";
	std::string after = base;
	after.replace(after.find("n = 2"), 5, "n = 3");
	ir::Module before_module, after_module;
	compile(base, &before_module, "Game.as");
	compile(after.c_str(), &after_module, "Game.as");
	const std::vector<std::string> found = ir::run_once_changes(before_module, after_module);
	CHECK_EQ(found.size(), (size_t)1);
	CHECK(!found.empty() && found[0] == "Game");
}

// Vectors across a reload: one is moved with its elements, and the objects in it are moved too.
static void test_reload_vectors() {
	const char *before = "package { public class Bag {\n"
						 "  public var numbers:Vector.<int> = new <int>[1, 2]; public var words:Vector.<String> = new <String>[\"a\"];\n"
						 "  public var things:Vector.<Thing> = new Vector.<Thing>(); public var locked:Vector.<Number> = new Vector.<Number>(2, true);\n"
						 "  public function fill():int { things.push(new Thing(7), new Thing(8)); things.push(things[0]); numbers.push(3); return things.length; }\n"
						 "} }\n"
						 "class Thing { public var n:int; public function Thing(n:int) { this.n = n; } }\n";
	const char *after = "package { public class Bag {\n"
						"  public var numbers:Vector.<int> = new <int>[9]; public var words:Vector.<String> = new <String>[\"z\"];\n"
						"  public var things:Vector.<Thing> = new Vector.<Thing>(); public var locked:Vector.<Number> = new Vector.<Number>();\n"
						"  public function sum():int { var t:int = 0; for each (var n:int in numbers) { t += n; } return t; }\n"
						"  public function tell():String { return numbers + \"|\" + words + \"|\" + things.length + \"|\" + locked.fixed + locked.length; }\n"
						"  public function same():Boolean { return things[0] == things[2] && things[0] != things[1]; }\n"
						"  public function worth():int { things[0].n += 100; return things[2].worth() + things[1].worth(); }\n"
						"} }\n"
						"class Thing { public var n:int; public function Thing(n:int) { this.n = n; } public function worth():int { return n * 2; } }\n";
	ir::Module old_module, new_module;
	compile(before, &old_module, "Bag.as");
	compile(after, &new_module, "Bag.as");
	{
		Recorder recorder;
		auto running = std::make_unique<vm::Interpreter>(old_module, recorder);
		CHECK(running->start());
		vm::Object *bag = running->construct(class_named(old_module, "Bag"), nullptr);
		vm::Slot result;
		CHECK(running->call_method(bag, "fill", nullptr, 0, &result));
		CHECK_EQ(result.i, 3);
		vm::Interpreter fresh(new_module, recorder);
		vm::Reload reload(*running, fresh);
		vm::Object *moved = reload.object(bag);
		CHECK(moved != nullptr);
		CHECK_EQ(reload.missing(), std::string());
		reload.globals();
		running->release_object(bag);
		running.reset();
		if (moved) {
			CHECK(fresh.call_method(moved, "sum", nullptr, 0, &result));
			CHECK_EQ(result.i, 6);
			CHECK(fresh.call_method(moved, "tell", nullptr, 0, &result));
			CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("1,2,3|a|3|true2"));
			fresh.discard(ir::Kind::Str, result);
			// The same object twice in the vector is still one object.
			CHECK(fresh.call_method(moved, "same", nullptr, 0, &result));
			CHECK(result.b);
			CHECK(fresh.call_method(moved, "worth", nullptr, 0, &result));
			CHECK_EQ(result.i, 214 + 16);
			fresh.release_object(moved);
		}
		CHECK_EQ(recorder.errors, std::string());
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);

	// A field that now holds another type of vector, or objects of another class, starts empty: what it
	// held is not of that type, and the new code would read it as if it were.
	ir::Module retyped;
	compile("package { public class Bag {\n"
			"  public var numbers:Vector.<String> = new <String>[\"s\"]; public var words:Vector.<int> = new <int>[5];\n"
			"  public var things:Vector.<Thing> = new Vector.<Thing>(); public var one:Other; public static var kept:Vector.<Number>;\n"
			"  public function empty():String { return \"\" + (numbers == null) + (words == null) + (things == null) + (one == null) + (kept == null); }\n"
			"} }\n"
			"class Thing { public var n:int; public function Thing(n:int) { this.n = n; } }\n"
			"class Other { public var s:String = \"o\"; }\n",
			&retyped, "Bag.as");
	ir::Module typed;
	compile("package { public class Bag {\n"
			"  public var numbers:Vector.<int> = new <int>[1234567]; public var words:Vector.<String> = new <String>[\"a\"];\n"
			"  public var things:Vector.<Thing> = new <Thing>[new Thing(1)]; public var one:Thing = new Thing(2); public static var kept:Vector.<int> = new <int>[3];\n"
			"} }\n"
			"class Thing { public var n:int; public function Thing(n:int) { this.n = n; } }\n",
			&typed, "Bag.as");
	{
		Recorder recorder;
		auto running = std::make_unique<vm::Interpreter>(typed, recorder);
		CHECK(running->start());
		vm::Object *bag = running->construct(class_named(typed, "Bag"), nullptr);
		vm::Interpreter fresh(retyped, recorder);
		vm::Reload reload(*running, fresh);
		vm::Object *moved = reload.object(bag);
		reload.globals();
		CHECK(moved != nullptr);
		running->release_object(bag);
		running.reset();
		if (moved) {
			vm::Slot result;
			CHECK(fresh.call_method(moved, "empty", nullptr, 0, &result));
			CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("truetruefalsetruetrue"));
			fresh.discard(ir::Kind::Str, result);
			fresh.release_object(moved);
		}
		fresh.clear_globals();
		CHECK_EQ(recorder.errors, std::string());
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);

	// A vector whose type the new program no longer has, or has with other elements, can't be moved.
	ir::Module without;
	compile("package { public class Bag { public var numbers:Vector.<Number> = new Vector.<Number>(); } }", &without, "Bag.as");
	{
		Recorder recorder;
		vm::Interpreter running(old_module, recorder);
		CHECK(running.start());
		vm::Object *bag = running.construct(class_named(old_module, "Bag"), nullptr);
		vm::Interpreter fresh(without, recorder);
		{
			vm::Reload reload(running, fresh);
			vm::Object *moved = reload.object(bag);
			CHECK_EQ(reload.missing(), std::string("Vector.<int>"));
			if (moved) {
				fresh.release_object(moved);
			}
		}
		running.release_object(bag);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// A running program's objects are moved onto a newer build of it: what they hold stays, what they do is new.
static void test_reload() {
	const char *before = "package { public class Counter {\n"
						 "  public var count:int; public var label:String = \"first\"; public var other:Counter;\n"
						 "  public var gone:int = 5; public var kind:int = 3;\n"
						 "  public static var total:int = 100;\n"
						 "  public function bump():int { count++; total++; return count; }\n"
						 "  public function sum():int { return total; }\n"
						 "  public function link(to:Counter):void { other = to; }\n"
						 "} }\n";
	const char *after = "package { public class Counter {\n"
						"  public var added:Number = 1.5; public var kind:String = \"text\"; public var other:Counter;\n"
						"  public var label:String = \"second\"; public var count:int;\n"
						"  public static var total:int = 100; public static var fresh:int = 7;\n"
						"  public function extra():int { return fresh; }\n"
						"  public function bump():int { count += 10; total++; return count; }\n"
						"  public function sum():int { return total; }\n"
						"  public function partner():int { return other.count; }\n"
						"  public function link(to:Counter):void { other = to; }\n"
						"  public function text():String { return label + (kind == null ? \"-\" : kind); }\n"
						"} }\n";
	ir::Module old_module, new_module;
	compile(before, &old_module);
	compile(after, &new_module);
	{
		Recorder recorder;
		auto running = std::make_unique<vm::Interpreter>(old_module, recorder);
		CHECK(running->start());
		const uint32_t klass = class_named(old_module, "Counter");
		vm::Object *first = running->construct(klass, nullptr);
		vm::Object *second = running->construct(klass, nullptr);
		vm::Slot result;
		CHECK(running->call_method(first, "bump", nullptr, 0, &result));
		CHECK(running->call_method(first, "bump", nullptr, 0, &result));
		CHECK_EQ(result.i, 2);
		// Two objects that point at each other: each is moved once.
		vm::Slot argument;
		argument.p = second;
		CHECK(running->call_method(first, "link", &argument, 1, &result));
		argument.p = first;
		CHECK(running->call_method(second, "link", &argument, 1, &result));

		vm::Interpreter fresh(new_module, recorder);
		vm::Reload reload(*running, fresh);
		vm::Object *moved = reload.object(first);
		vm::Object *moved_second = reload.object(second);
		CHECK(moved != nullptr && moved_second != nullptr && moved != first);
		vm::Object *again = reload.object(first);
		CHECK_EQ(again, moved); // The same one, not another: with a reference of the caller's own.
		fresh.release_object(again);
		reload.globals();
		// A method's place in the new program, by its name. One that is gone has none.
		const uint32_t old_bump = running->find_method(klass, "bump");
		CHECK_EQ(reload.function(old_bump), fresh.find_method(class_named(new_module, "Counter"), "bump"));
		// The two refer to each other, which counting alone never frees: the test undoes it by hand.
		argument.p = nullptr;
		CHECK(running->call_method(first, "link", &argument, 1, &result));
		running->release_object(first);
		running->release_object(second);
		running.reset(); // The old program is gone: nothing of it may be needed from here on.

		// What the object held is still there; the code is the new one.
		CHECK(fresh.call_method(moved, "bump", nullptr, 0, &result));
		CHECK_EQ(result.i, 12);
		CHECK(fresh.call_method(moved, "partner", nullptr, 0, &result));
		CHECK_EQ(result.i, 0);
		CHECK(fresh.call_method(moved_second, "partner", nullptr, 0, &result));
		CHECK_EQ(result.i, 12);
		// A static field keeps its value, and is not given its first value again.
		CHECK(fresh.call_method(moved, "sum", nullptr, 0, &result));
		CHECK_EQ(result.i, 103);
		// A static field that is new starts empty: its class's static fields are not given their first
		// values a second time, or `total` would go back to 100.
		CHECK(fresh.call_method(moved, "extra", nullptr, 0, &result));
		CHECK_EQ(result.i, 0);
		// A field that kept its name and kind kept its value, not the new first value. One whose kind
		// changed starts empty, and so does one that is new: the object was not constructed again.
		CHECK(fresh.call_method(moved, "text", nullptr, 0, &result));
		CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("first-"));
		fresh.discard(ir::Kind::Str, result);
		vm::Slot added;
		ir::Kind kind = ir::Kind::Void;
		CHECK(fresh.get_field(moved, "added", &added, &kind));
		CHECK(kind == ir::Kind::F64 && added.d != added.d);
		CHECK(!fresh.get_field(moved, "gone", &added));
		CHECK(fresh.call_method(moved, "link", &argument, 1, &result));
		fresh.release_object(moved);
		fresh.release_object(moved_second);
		CHECK_EQ(recorder.errors, std::string());
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);

	// A class that is gone from the new program: its objects can't be moved, and that is said.
	ir::Module without;
	compile("package { public class Other { public var x:int; } }", &without, "Other.as");
	{
		Recorder recorder;
		vm::Interpreter running(old_module, recorder);
		CHECK(running.start());
		vm::Object *object = running.construct(class_named(old_module, "Counter"), nullptr);
		vm::Interpreter fresh(without, recorder);
		vm::Reload reload(running, fresh);
		CHECK(reload.object(object) == nullptr);
		CHECK_EQ(reload.missing(), std::string("Counter"));
		running.release_object(object);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

static void test_loop_markers() {
	Compilation compilation;
	compilation.add("Main.as", "package { public class Main { public function Main() {\n"
							   "var n:int = 0;\n"
							   "while (n < 2) {\n"
							   "n++;\n"
							   "}\n"
							   "for (var i:int = 0; i < 2; i++) {\n"
							   "n += i;\n"
							   "}\n"
							   "do {\n"
							   "n--;\n"
							   "} while (n > 2);\n"
							   "for (;;) {\n"
							   "if (n < 5) { break; }\n"
							   "}\n"
							   "} } }");
	compilation.check();
	ir::Module module;
	CHECK(ir::lower(compilation, &module));
	Recorder host;
	Steps steps;
	vm::Interpreter interpreter(module, host);
	interpreter.set_statement_hook(on_statement, &steps);
	CHECK(interpreter.run_program());
	CHECK_EQ(steps.seen, std::string("Main:2 Main:3 Main:4 Main:3 Main:4 Main:3 Main:6 Main:6 Main:7 Main:6 Main:7 Main:6 Main:10 Main:11 Main:12 Main:13 Main:13 "));
}

// A class whose static fields could not be given their values is not quietly usable afterwards: the next
// use tries again and fails again. A subclass is no better off than its base.
static void test_failed_static_initializer() {
	Compilation compilation;
	compilation.add("Boom.as", "package { public class Boom {\n"
							   "public static var a:int = 5;\n"
							   "public static var b:int = boom();\n"
							   "public static var c:int = 9;\n"
							   "private static function boom():int { var none:Boom; return none.value; }\n"
							   "public var value:int;\n"
							   "} }\n"
							   "class Child extends Boom { public static var own:int = 3; }\n"
							   "class Fine { public static var n:int = 1; }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	uint32_t boom = ir::NONE, child = ir::NONE, fine = ir::NONE;
	for (size_t i = 0; i < module.classes.size(); i++) {
		boom = module.classes[i].name == "Boom" ? (uint32_t)i : boom;
		child = module.classes[i].name == "Child" ? (uint32_t)i : child;
		fine = module.classes[i].name == "Fine" ? (uint32_t)i : fine;
	}
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK(interpreter.start());
		auto errors = [&]() {
			int count = 0;
			for (size_t at = host.errors.find("Error #1009"); at != std::string::npos; at = host.errors.find("Error #1009", at + 1)) {
				count++;
			}
			return count;
		};
		CHECK(interpreter.construct(boom) == nullptr);
		CHECK_EQ(errors(), 1);
		CHECK(interpreter.construct(boom) == nullptr); // Not a half-made class the second time.
		CHECK_EQ(errors(), 2);
		CHECK(interpreter.construct(child) == nullptr); // Nor through a subclass.
		CHECK_EQ(errors(), 3);
		CHECK(interpreter.construct(child) == nullptr);
		CHECK_EQ(errors(), 4);
		vm::Object *other = interpreter.construct(fine); // Another class is untouched.
		CHECK(other != nullptr);
		interpreter.release_object(other);
		CHECK_EQ(interpreter.depth(), (size_t)0);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// With no memory left, AS3 is told so (Error #1000), and can catch it.
static void test_out_of_memory() {
	Compilation compilation;
	compilation.add("Grow.as", "package { public class Grow {\n"
							   "public function joined(a:String, b:String):String {\n"
							   "	try { return a + b; } catch (e:Error) { return 'caught: ' + e.message; }\n"
							   "	return '';\n"
							   "}\n"
							   "public function made():String {\n"
							   "	try { var g:Grow = new Grow(); return 'made'; } catch (e:Error) { return 'caught: ' + e.message; }\n"
							   "	return '';\n"
							   "}\n"
							   "public function concatenated(a:String, b:String):String {\n"
							   "	try { return a.concat(b); } catch (e:Error) { return 'caught: ' + e.message; }\n"
							   "	return '';\n"
							   "}\n"
							   "public function fixed():String {\n"
							   "	try { return (1.5).toFixed(2); } catch (e:Error) { return 'caught: ' + e.message; }\n"
							   "	return '';\n"
							   "}\n"
							   "public function character():String {\n"
							   "	var s:String = 'xy';\n"
							   "	try { return s.charAt(1); } catch (e:Error) { return 'caught: ' + e.message; }\n"
							   "	return '';\n"
							   "}\n"
							   "} }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	uint32_t grow = ir::NONE;
	for (size_t i = 0; i < module.classes.size(); i++) {
		grow = module.classes[i].name == "Grow" ? (uint32_t)i : grow;
	}
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK(interpreter.start());
		vm::Object *object = interpreter.construct(grow);
		CHECK(object != nullptr);
		const std::string wanted = "caught: " + std::string(vm::OUT_OF_MEMORY);
		for (const char *method : { "joined", "made", "concatenated", "fixed", "character" }) {
			vm::Slot arguments[2];
			arguments[0].p = vm::new_string("left");
			arguments[1].p = vm::new_string("right");
			vm::Slot result;
			result.bits = 0;
			vm::fail_allocations(1);
			const bool two = std::string_view(method) == "joined" || std::string_view(method) == "concatenated";
			CHECK(interpreter.call_method(object, method, arguments, two ? 2 : 0, &result));
			vm::fail_allocations(0);
			CHECK(result.p != nullptr);
			CHECK_EQ(result.p ? std::string(static_cast<vm::Str *>(result.p)->view()) : std::string(), wanted);
			interpreter.discard(ir::Kind::Str, result);
			interpreter.discard(ir::Kind::Str, arguments[0]);
			interpreter.discard(ir::Kind::Str, arguments[1]);
		}
		interpreter.release_object(object);
		CHECK_EQ(interpreter.depth(), (size_t)0);
	}
	// A program whose constants could not be made does not start, and says why.
	{
		Recorder host;
		vm::fail_allocations(1);
		vm::Interpreter interpreter(module, host);
		vm::fail_allocations(0);
		CHECK(!interpreter.start());
		CHECK(!interpreter.start()); // Nor the second time.
		CHECK_EQ(host.errors, std::string(vm::OUT_OF_MEMORY) + "\n" + std::string(vm::OUT_OF_MEMORY) + "\n");
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

static bool most_kept(vm::Interpreter &p_interpreter, void *p_data) {
	size_t *most = static_cast<size_t *>(p_data);
	*most = std::max(*most, p_interpreter.kept_traces());
	return true;
}

// Errors swallowed in a loop inside finallies keep at most one trace for each finally.
static void test_kept_traces() {
	Compilation compilation;
	compilation.add("Loop.as", "package { public class Loop { public function Loop() {\n"
							   "	for (var i:int = 0; i < 200; i++) {\n"
							   "		try { throw 'x'; } finally { continue; }\n"
							   "	}\n"
							   "	for (var j:int = 0; j < 200; j++) {\n"
							   "		try { try { throw 'x'; } finally { throw 'y'; } } catch (e:String) {}\n"
							   "	}\n"
							   "} } }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module));
	Recorder host;
	size_t most = 0;
	vm::Interpreter interpreter(module, host);
	interpreter.set_statement_hook(most_kept, &most);
	CHECK(interpreter.run_program());
	CHECK(most <= 2);
	CHECK_EQ(interpreter.kept_traces(), (size_t)0);
}

// Untyped arithmetic with no memory for an Int64, and `==` with no memory for an object's text.
static void test_untyped_out_of_memory() {
	Compilation compilation;
	compilation.add("Any.as", "package { public class Any {\n"
							  "public function add(a:*):* { return a + 1; }\n"
							  "public function same(o:*):String { try { return String(o == 'grow'); } catch (e:Error) { return e.message; } return ''; }\n"
							  "public function toString():String { return 'grow'; }\n"
							  "public function glue(a:*):String { try { return a + 'x'; } catch (e:Error) { return e.message; } return ''; }\n"
							  "} }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	uint32_t klass = ir::NONE;
	for (size_t i = 0; i < module.classes.size(); i++) {
		klass = module.classes[i].name == "Any" ? (uint32_t)i : klass;
	}
	const size_t strings = vm::live_strings();
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK(interpreter.start());
		vm::Object *object = interpreter.construct(klass);
		CHECK(object != nullptr);
		vm::Slot wide = vm::any::wide(vm::new_wide(9007199254740992LL));
		vm::Slot result;
		result.bits = 0;
		vm::fail_allocations(1);
		CHECK(!interpreter.call_method(object, "add", &wide, 1, &result)); // Not a Number that lost its last digit.
		vm::fail_allocations(0);
		CHECK(host.errors.find(std::string(vm::OUT_OF_MEMORY)) != std::string::npos);
		interpreter.discard(ir::Kind::Any, wide);
		vm::Slot self = vm::any::object(object);
		vm::fail_allocations(1);
		CHECK(interpreter.call_method(object, "same", &self, 1, &result));
		vm::fail_allocations(0);
		CHECK_EQ(result.p ? std::string(static_cast<vm::Str *>(result.p)->view()) : std::string(), std::string(vm::OUT_OF_MEMORY));
		interpreter.discard(ir::Kind::Str, result);
		// Text joined with an untyped `+`, with no memory for the result.
		vm::Slot text = vm::any::string(vm::new_string("left"));
		vm::fail_allocations(1);
		CHECK(interpreter.call_method(object, "glue", &text, 1, &result));
		vm::fail_allocations(0);
		CHECK_EQ(result.p ? std::string(static_cast<vm::Str *>(result.p)->view()) : std::string(), std::string(vm::OUT_OF_MEMORY));
		interpreter.discard(ir::Kind::Str, result);
		interpreter.discard(ir::Kind::Any, text);
		interpreter.release_object(object);
	}
	CHECK_EQ(vm::live_strings(), strings);
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// An object a host makes is built in the order `new` builds it, AS3's: a class's field values first, at
// the start of its constructor, then its base's (fields, constructor), then the rest of its constructor.
static void test_host_construct_order() {
	Compilation compilation;
	compilation.add("Child.as", "package { public class Child extends Base {\n"
								"public var own:int = say('Child field');\n"
								"public function Child() { trace('Child constructor'); }\n"
								"} }\n"
								"class Base {\n"
								"public var first:int = say('Base field');\n"
								"public function Base() { trace('Base constructor'); }\n"
								"public function say(p_text:String):int { trace(p_text); return 1; }\n"
								"}\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	uint32_t child = ir::NONE;
	for (size_t i = 0; i < module.classes.size(); i++) {
		child = module.classes[i].name == "Child" ? (uint32_t)i : child;
	}
	Recorder host;
	vm::Interpreter interpreter(module, host);
	CHECK(interpreter.start());
	vm::Object *made = interpreter.construct(child);
	CHECK(made != nullptr);
	CHECK_EQ(host.printed, std::string("Child field\nBase field\nBase constructor\nChild constructor\n"));
	interpreter.release_object(made);
}

int main() {
	test_host_construct_order();
	test_kept_traces();
	test_untyped_out_of_memory();
	test_out_of_memory();
	test_reload();
	test_reload_vectors();
	test_run_once_changes();
	test_run_once_overrides();
	test_reload_alive();
	test_failed_static_initializer();
	test_loop_markers();
	test_engine_calls();
	test_engine_operators();
	test_kept_by_the_engine();
	test_no_engine();
	test_engine_listeners();
	test_engine_statics();
	test_entry_points();
	test_time_limit();
	test_statement_hook();
	test_refused_program();
	return check::finish();
}
