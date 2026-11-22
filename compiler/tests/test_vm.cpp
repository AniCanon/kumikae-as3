// The interpreter as a library: what a debugger will use, and what the host is told.
#include "check.h"

#include "any.h"
#include "builtins.h"
#include "interpreter.h"
#include "reload.h"
#include "table.h"
#include "lower.h"
#include "names.h"

#include <algorithm>
#include <map>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
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
		// The time a run has taken is the host's clock's to say: this one moves on ten milliseconds each
		// time it is asked, so how long the machine really takes changes nothing here.
		struct Stepping : Recorder, vm::Clock {
			double at = 0;
			double now() override { return 0; }
			double local_offset(double) override { return 0; }
			double elapsed() override { return at += 10; }
		};
		Stepping host;
		vm::Interpreter interpreter(module, host);
		interpreter.set_time_limit(50);
		CHECK(interpreter.start());
		vm::Object *object = interpreter.construct(spin);
		for (const char *method : { "forever", "loops", "down", "counting", "empty", "skipping", "caught" }) {
			host.errors.clear();
			const double started = host.at;
			vm::Slot result;
			result.bits = 0;
			CHECK(!interpreter.call_method(object, method, nullptr, 0, &result));
			// Stopped at the first look past the limit: the call began at the first tick after `started`.
			CHECK_EQ(host.at - started, 70.0);
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
	// Whoever keeps native code's time is told of each call from outside that no other is running inside,
	// however it is made: the start, an object made, a method called. Not of one made inside another.
	{
		struct Told final : vm::NativeCode {
			vm::Interpreter *program = nullptr;
			uint32_t began = 0;
			uint32_t inside = 0; // How many of those began with a call already in progress.
			void call_began() override {
				began++;
				inside += program->in_call() ? 1 : 0;
			}
			[[nodiscard]] bool hand_over(vm::NativeError *) override { return false; }
			void clear_globals() override {}
			vm::Slot *global(uint32_t) override { return nullptr; }
			bool statics_given(uint32_t) const override { return false; }
			[[nodiscard]] bool run_native(uint32_t, uint32_t, uint32_t, vm::Slot *, uint32_t, uint32_t, vm::Slot *) override { return false; }
		} told;
		Recorder host;
		vm::Interpreter interpreter(module, host);
		told.program = &interpreter;
		interpreter.set_native(&told);
		CHECK(interpreter.start());
		const uint32_t started = told.began;
		vm::Object *object = interpreter.construct(spin);
		CHECK_EQ(told.began, started + 1);
		vm::Slot sum;
		sum.bits = 0;
		CHECK(interpreter.call_method(object, "quick", nullptr, 0, &sum));
		CHECK_EQ(told.began, started + 2);
		// A call a host counts itself (native code run for the engine), and one made inside it.
		interpreter.entered();
		CHECK_EQ(told.began, started + 3);
		CHECK(interpreter.call_method(object, "quick", nullptr, 0, &sum));
		interpreter.left();
		CHECK_EQ(told.began, started + 3);
		CHECK_EQ(told.inside, (uint32_t)0);
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
	Unit *unit = compilation.add("Main.as", "package { public class Main { public function Main() { var f:* = function():void { trace(this); }; } } }");
	compilation.check();
	ir::Module module;
	CHECK(!ir::lower(compilation, &module));
	CHECK(unit->diagnostics.has_errors());
	CHECK(compilation.format_diagnostics(false).find("is not supported by the interpreter yet") != std::string::npos);
}

// Date asks the host the time and its zone: a host that keeps a clock gives both, whatever the system's are.
struct Clocked : vm::Output, vm::Clock {
	std::string printed, errors;
	std::vector<double> asked; // The moments the zone was asked about.
	void print(std::string_view p_text) override { printed += std::string(p_text) + "\n"; }
	void error(std::string_view p_text) override { errors += std::string(p_text) + "\n"; }
	double now() override { return 31536000000.0; } // 1971 begins, in UTC.
	double local_offset(double p_utc) override {
		asked.push_back(p_utc);
		return 5.5 * 3600000; // Half an hour past a whole number of hours, as in India.
	}
};

static void test_host_clock() {
	Compilation compilation;
	compilation.add("Main.as", "package { public class Main { public function Main() {\n"
							   "var now:Date = new Date(); var start:Date = new Date(0);\n"
							   "trace(now.time, now, start.hours, start.minutes, start.timezoneOffset);\n"
							   "trace(new Date(1971, 0, 1, 5, 30).time, Date.parse(\"Jan 1 1971\"));\n"
							   "} } }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module));
	Clocked host;
	{
		vm::Interpreter interpreter(module, host);
		CHECK(interpreter.run_program());
	}
	CHECK_EQ(host.errors, std::string());
	CHECK_EQ(host.printed, std::string("31536000000 Fri Jan 1 05:30:00 GMT+0530 1971 5 30 -330\n31536000000 31516200000\n"));
	CHECK(!host.asked.empty());
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
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

		// An error inside a nested call comes out of the instruction whose call out made it (the trace that
		// called back in), level by level, and is reported once, at the top.
		host.nest = 3;
		host.fail_inside = true;
		host.results = 0;
		CHECK(!interpreter.call_method(object, "shout", arguments, 1, &result));
		CHECK(host.errors.find("TypeError: Error #1009") == 0);
		CHECK_EQ(host.errors.find("TypeError", 1), std::string::npos);
		CHECK(host.errors.find("\tat Counter/fail()") != std::string::npos && host.errors.find("\tat Counter/shout()") != std::string::npos);
		CHECK_EQ(host.results, 0); // No level returned.
		host.errors.clear();
		host.fail_inside = false;

		// Nesting without end is an AS3 error, not a crash: it comes out to the top, as any other.
		host.nest = 100000;
		host.results = 0;
		CHECK(!interpreter.call_method(object, "shout", arguments, 1, &result));
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
	// What the engine runs of the program before it answers a call or gives a value's text, as Godot runs a
	// listener when a signal is emitted, or a script's toString().
	std::function<void()> calls_back;

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
	// The Vector2s among the boxes, each as a slot holds one whole: two reals.
	std::unordered_map<void *, uint64_t> wholes;
	static uint64_t whole_of(double p_x, double p_y) {
		const float parts[2] = { static_cast<float>(p_x), static_cast<float>(p_y) };
		uint64_t bits = 0;
		std::memcpy(&bits, parts, sizeof(bits));
		return bits;
	}
	static std::string show_whole(uint64_t p_bits) {
		float parts[2];
		std::memcpy(parts, &p_bits, sizeof(parts));
		return "(" + vm::number_to_string(parts[0]) + ", " + vm::number_to_string(parts[1]) + ")";
	}
	[[nodiscard]] void *construct(std::string_view p_type, const vm::Argument *p_arguments, size_t p_count, std::string *) override {
		void *made = box();
		log += "construct " + std::string(p_type) + "(" + list(p_arguments, p_count) + ") -> #" + std::to_string((intptr_t)made) + "\n";
		if (p_type == "Vector2") {
			const auto part = [&](size_t p_at) {
				const vm::Argument &given = p_arguments[p_at];
				return given.kind == ir::Kind::I32 ? given.slot.i : given.kind == ir::Kind::I64 ? static_cast<double>(given.slot.l) : given.slot.d;
			};
			wholes[made] = p_count == 2 ? whole_of(part(0), part(1)) : 0;
		}
		return made;
	}
	[[nodiscard]] void *boxed(ir::Bare p_type, vm::Slot p_value, std::string *) override {
		void *made = box();
		log += "boxed " + std::string(ir::bare_name(p_type)) + show_whole(p_value.bits) + " -> #" + std::to_string((intptr_t)made) + "\n";
		wholes[made] = p_value.bits;
		return made;
	}
	[[nodiscard]] bool unboxed(ir::Bare, void *p_handle, vm::Slot *r_value) override {
		const auto found = wholes.find(p_handle);
		r_value->bits = found == wholes.end() ? 0 : found->second;
		return found != wholes.end();
	}
	[[nodiscard]] void *collection(std::string_view p_type, const ir::Class &p_class, const vm::Vector *p_vector, std::string *) override {
		void *made = box();
		std::string elements;
		for (uint32_t i = 0; i < p_vector->length; i++) {
			const vm::Slot held = vm::place(p_vector, i);
			elements += (i ? ", " : "") + (p_class.element_bare != ir::Bare::None ? show_whole(held.bits) : show(vm::Argument{ p_class.element, held }));
		}
		log += "collection " + std::string(p_type) + "[" + elements + "] -> #" + std::to_string((intptr_t)made) + "\n";
		return made;
	}
	[[nodiscard]] bool call(void *p_receiver, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_wanted,
			vm::Slot *r_result, std::string *r_error) override {
		ir::Kind p_kind = p_wanted.kind;
		log += "call #" + std::to_string((intptr_t)p_receiver) + "." + std::string(p_method) + "(" + list(p_arguments, p_count) + ")\n";
		if (calls_back) {
			calls_back();
		}
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
		if (p_method.substr(0, p_method.find(' ')) == missing) {
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
		if (calls_back) {
			calls_back();
		}
		return vm::new_string("<#" + std::to_string((intptr_t)p_value) + ">");
	}
	[[nodiscard]] bool operate(int, std::string_view p_operator, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_wanted, vm::Slot *r_result,
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
	// For the cycle collector: boxes that stand for nodes (a Godot object a script listens to), and what
	// each holds. A node's objects are references the test took for it, as a listener takes one.
	struct Node {
		uint64_t references = 1;
		std::vector<vm::Object *> objects;
		std::vector<vm::Object *> joined;
		bool exact = false;
		bool survives = false; // Something the collector can't see holds a copy of it.
	};
	std::map<intptr_t, uint64_t> node_boxes;
	std::map<uint64_t, Node> nodes;
	vm::Interpreter *running = nullptr;
	int handles_asked = 0; // How many times the collector asked whether a handle is on a node.
	[[nodiscard]] uint64_t node(void *p_handle) override {
		handles_asked++;
		auto found = node_boxes.find((intptr_t)p_handle);
		return found == node_boxes.end() ? 0 : found->second;
	}
	int asked = 0; // How many times the collector asked what a node holds.
	[[nodiscard]] bool node_edges(uint64_t p_node, vm::EngineNode *r_node) override {
		asked++;
		auto found = nodes.find(p_node);
		if (found == nodes.end()) {
			return false;
		}
		r_node->references = found->second.references;
		r_node->exact = found->second.exact;
		r_node->objects = found->second.objects;
		r_node->joined = found->second.joined;
		return true;
	}
	[[nodiscard]] std::vector<uint64_t> let_go(const std::vector<uint64_t> &p_nodes) override {
		std::vector<uint64_t> survivors;
		for (uint64_t at : p_nodes) {
			log += "let go of node " + std::to_string(at) + "\n";
			if (nodes[at].survives) {
				survivors.push_back(at);
			}
		}
		if (!survivors.empty()) {
			log += "put back\n";
			return survivors;
		}
		for (uint64_t at : p_nodes) {
			std::vector<vm::Object *> held = nodes[at].objects;
			nodes.erase(at);
			for (vm::Object *object : held) {
				running->release_object(object);
			}
		}
		return survivors;
	}
	// The script objects the engine's objects carry, by their handles.
	std::unordered_map<void *, vm::Object *> scripts;
	[[nodiscard]] vm::Object *script_of(void *p_handle, uint32_t p_class, bool p_taken) override {
		log += "script #" + std::to_string((intptr_t)p_handle) + " as " + (module ? module->classes[p_class].name : std::to_string(p_class)) +
				(p_taken ? ", taken\n" : "\n");
		const auto found = scripts.find(p_handle);
		if (found == scripts.end() || !module || !vm::is_class(*module, found->second, p_class)) {
			return nullptr;
		}
		if (p_taken) {
			vm::retain(found->second);
		}
		return found->second;
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
// engine says it still holds the object, and freed with what it holds when the engine lets go too. A
// script whose base is Godot's Object may be on a RefCounted too: the engine is asked about its objects.
static void test_kept_by_the_engine() {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Counted.as", "package { import godot.*; public class Counted extends RefCounted { public var text:String = \"kept\"; } }");
	compilation.add("Plain.as", "package { import godot.*; public class Plain extends Node { public var text:String = \"plain\"; } }");
	compilation.add("Unjoined.as", "package { import godot.*; public class Unjoined extends GodotObject {} }");
	compilation.add("Further.as", "package { import godot.*; public class Further extends Unjoined {} }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	uint32_t counted = 0, plain = 0, unjoined = 0, further = 0;
	for (size_t i = 0; i < module.classes.size(); i++) {
		counted = module.classes[i].name == "Counted" ? (uint32_t)i : counted;
		plain = module.classes[i].name == "Plain" ? (uint32_t)i : plain;
		unjoined = module.classes[i].name == "Unjoined" ? (uint32_t)i : unjoined;
		further = module.classes[i].name == "Further" ? (uint32_t)i : further;
	}
	CHECK(module.classes[counted].counted);
	CHECK(!module.classes[plain].counted);
	CHECK(module.classes[unjoined].counted);
	CHECK(module.classes[further].counted);
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
		CHECK_EQ(std::string(static_cast<vm::Str *>(vm::slot_at(object, 1).p)->view()), std::string("kept"));
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
		interpreter.release_object(interpreter.construct(unjoined, engine.box()));
		CHECK_EQ(engine.log, std::string("keep Counted\nkeep Unjoined\n"));
		CHECK_EQ(vm::live_objects(), before);
		CHECK_EQ(engine.boxes, 0);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// One of the engine's objects that carries a script, where the script's class is declared: `is`, `as` and a
// store of an untyped value give the script's own object, by what the engine says the object carries. An
// object with no script, or with a script of another class, is not one: null from `as`, and AS3's TypeError
// #1034 from a store.
static void test_script_object_arrives() {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Mate.as", "package { import godot.*; public class Mate extends Node { public var n:int = 3;\n"
							   "public function tell(node:Node, any:*):String {\n"
							   "  var found:Mate = node as Mate;\n"
							   "  var kept:Mate = any;\n"
							   "  return (node is Mate) + \" \" + (found ? found.n : -1) + \" \" + (any is Mate) + \" \" + (kept ? kept.n : -1) + \" \" + (any is Further);\n"
							   "}\n"
							   "public function further(any:*):Boolean { var kept:Further = any; return kept != null; } } }");
	compilation.add("Further.as", "package { public class Further extends Mate {} }");
	compilation.add("Other.as", "package { import godot.*; public class Other extends Node {} }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	uint32_t mate = 0, further = 0, other = 0;
	for (size_t i = 0; i < module.classes.size(); i++) {
		mate = module.classes[i].name == "Mate" ? (uint32_t)i : mate;
		further = module.classes[i].name == "Further" ? (uint32_t)i : further;
		other = module.classes[i].name == "Other" ? (uint32_t)i : other;
	}
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		// An untyped value keeps its tag in a handle's low bits: handles as an engine's are, sixteen bytes apart.
		alignas(16) static char rooms[4][16];
		void *handles[4] = { rooms[0], rooms[1], rooms[2], rooms[3] };
		engine.boxes += 3; // The three that scripts are given as their own.
		vm::Object *first = interpreter.construct(mate, handles[0]);
		vm::Object *second = interpreter.construct(further, handles[1]);
		vm::Object *third = interpreter.construct(other, handles[2]);
		vm::Slot five;
		five.bits = 0;
		five.i = 5;
		for (size_t i = 0; i < module.classes[further].fields.size(); i++) {
			if (module.classes[further].fields[i].name == "n") {
				interpreter.set_field(second, (uint32_t)i, five);
			}
		}
		engine.scripts = { { handles[0], first }, { handles[1], second }, { handles[2], third } };
		const auto tell = [&](void *p_node) {
			vm::Slot arguments[2];
			arguments[0].bits = 0;
			arguments[0].p = p_node;
			arguments[1] = p_node ? vm::any::godot_object(p_node) : vm::any::null();
			vm::Slot result;
			result.bits = 0;
			engine.errors.clear();
			const bool ok = interpreter.call_method(first, "tell", arguments, 2, &result);
			std::string said = ok && result.p ? std::string(static_cast<vm::Str *>(result.p)->view()) : engine.errors.substr(0, engine.errors.find('\n'));
			if (ok && result.p && vm::release(static_cast<vm::Str *>(result.p))) {
				vm::free_string(static_cast<vm::Str *>(result.p));
			}
			return said;
		};
		CHECK_EQ(tell(handles[0]), std::string("true 3 true 3 false"));
		CHECK_EQ(tell(handles[1]), std::string("true 5 true 5 true"));
		CHECK_EQ(tell(nullptr), std::string("false -1 false -1 false"));
		// With a script of another class, and with none: `as` gives null, and the store is refused.
		CHECK_EQ(tell(handles[2]), std::string("TypeError: Error #1034: Type Coercion failed: cannot convert Object to Mate."));
		CHECK_EQ(tell(handles[3]), std::string("TypeError: Error #1034: Type Coercion failed: cannot convert Object to Mate."));
		// A class the object's class extends is not the object's.
		vm::Slot any = vm::any::godot_object(handles[0]);
		CHECK(!interpreter.call_method(first, "further", &any, 1, nullptr));
		CHECK(engine.errors.find("cannot convert Object to Further.") != std::string::npos);
		const uint32_t references = first->references;
		any = vm::any::godot_object(handles[1]);
		vm::Slot kept;
		CHECK(interpreter.call_method(first, "further", &any, 1, &kept));
		CHECK(kept.b);
		// What a call was given is let go of when it ends.
		CHECK_EQ(first->references, references);
		interpreter.release_object(first);
		interpreter.release_object(second);
		interpreter.release_object(third);
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
						 "  public function opened(iv:PackedByteArray):FileAccess { return FileAccess.openEncrypted(\"p\", 1, iv, iv); }\n"
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
		CHECK_EQ(engine.log, std::string("static Vector2.from_angle method 889263119 Vector2 float(3)\n"));
		engine.drop(result.p);
		engine.log.clear();
		CHECK(interpreter.call_method(object, "shown", nullptr, 0, nullptr));
		CHECK_EQ(engine.log, std::string("static Node.print_orphan_nodes method 3218959716 void -()\n"));
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
		// A static method's default that AS3 can't write is Godot's, for a missing value given in its place.
		engine.log.clear();
		arguments[0].bits = 0;
		CHECK(interpreter.call_method(object, "opened", arguments, 1, &result));
		CHECK(engine.log.find("evaluate PackedByteArray() as PackedByteArray\n") != std::string::npos);
		engine.drop(result.p);
		CHECK_EQ(engine.boxes, 4);
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

// An engine that works out each operator once, as the program is loaded, and carries out with it what it
// worked out; one it can't carry out (a value of another type) is left to its requests by name.
struct PreparingEngine : FakeEngine {
	struct Worked : vm::Prepared {
		std::string what;
	};
	std::vector<std::unique_ptr<Worked>> worked;
	int prepared = 0;
	bool refuse_to_run = false; // Leave every one to the requests by name.
	[[nodiscard]] const vm::Prepared *prepare(const ir::Module &p_module, const ir::Function &, const ir::Instruction &p_in) override {
		prepared++;
		if (p_in.op != ir::Op::HostOperate) {
			return nullptr;
		}
		worked.push_back(std::make_unique<Worked>());
		worked.back()->what = p_module.engine_names[p_in.b].key;
		return worked.back().get();
	}
	[[nodiscard]] bool run(const vm::Prepared &p_prepared, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_wanted, vm::Slot *r_result,
			std::string *) override {
		if (refuse_to_run) {
			return false;
		}
		log += "run " + static_cast<const Worked &>(p_prepared).what + " (" + list(p_operands, p_count) + ")\n";
		if (in_place && p_wanted.reuse) {
			log += "into #" + std::to_string((intptr_t)p_wanted.reuse) + "\n";
			r_result->p = p_wanted.reuse; // The handle the register holds, written over: no new one.
			return true;
		}
		give(p_wanted.kind, r_result, 1, "");
		return true;
	}
	// Write results into the handle the register holds, and assign values into handles, where it can.
	bool in_place = false;
	int assigned = 0;
	[[nodiscard]] bool assign(void *, const void *) override {
		assigned += in_place;
		return in_place;
	}
};

static void test_engine_prepared() {
	const char *source = "package { import godot.*; public class Mover extends Node2D {\n"
						 "  public function step(by:Vector2, n:int):Boolean { var v:Vector2 = position + by * n; return v == by; }\n"
						 "  public function grow(by:Vector2, n:int):Vector2 { var v:Vector2 = by; for (var i:int = 0; i < n; i++) { v = v + by; } return v; }\n"
						 "} }\n";
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Mover.as", source);
	compilation.check();
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	size_t named = 0; // Engine instructions: those that name something of Godot's.
	for (const ir::Function &function : module.functions) {
		for (const ir::Instruction &in : function.code) {
			named += std::strchr(ir::op_shape(in.op), 'E') != nullptr;
		}
	}
	PreparingEngine engine;
	{
		vm::Interpreter interpreter(module, engine);
		// Each was worked out once, when the program was loaded, and none is again as it runs.
		CHECK_EQ((size_t)engine.prepared, named);
		CHECK(interpreter.start());
		uint32_t mover = 0;
		for (size_t i = 0; i < module.classes.size(); i++) {
			mover = module.classes[i].name == "Mover" ? (uint32_t)i : mover;
		}
		vm::Object *object = interpreter.construct(mover, engine.box());
		vm::Slot arguments[2];
		arguments[0].bits = 0;
		arguments[0].p = engine.box();
		arguments[1].bits = 0;
		arguments[1].i = 3;
		vm::Slot result;
		for (bool refuse : { false, true }) {
			engine.refuse_to_run = refuse;
			engine.log.clear();
			CHECK(interpreter.call_method(object, "step", arguments, 2, &result));
			CHECK_EQ((size_t)engine.prepared, named);
			// What the engine worked out runs, or what it leaves runs by name: never both.
			CHECK_EQ(engine.log, refuse ? std::string("get #1.position\noperate * (#2, 3) as GodotValue Vector2\noperate + (#6, #7) as GodotValue Vector2\n"
													  "operate == (#8, #2) as Boolean\n")
										: std::string("get #1.position\nrun @operator.OP_MULTIPLY (#2, 3)\nrun @operator.OP_ADD (#3, #4)\n"
													  "run @operator.OP_EQUAL (#5, #2)\n"));
		}
		CHECK_EQ(engine.boxes, 2);
		// A loop's value made in the handle its register already holds, and assigned into the variable's:
		// no handle is made for either once the loop runs, and none is lost.
		engine.refuse_to_run = false;
		engine.in_place = true;
		engine.log.clear();
		CHECK(interpreter.call_method(object, "grow", arguments, 2, &result));
		CHECK_EQ(engine.log, std::string("run @operator.OP_ADD (#2, #2)\nrun @operator.OP_ADD (#2, #2)\ninto #9\nrun @operator.OP_ADD (#2, #2)\n"
										 "into #9\n"));
		CHECK_EQ(engine.assigned, 3); // Into the zero the variable starts at, then each time round.
		CHECK_EQ(engine.boxes, 3); // The owner, the argument, and the result.
		engine.drop(result.p);
		engine.drop(arguments[0].p);
		interpreter.release_object(object);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// A vector of one of Godot's value types with parts starts with that type's zero in each place, as a C#
// array of structs does, and so does each place it grows by; a vector of any other Godot value starts null.
// A Vector2 is held whole in its place: the engine is asked for nothing as the vector is made or grows, a
// place read is a new handle on a copy, a place written takes the value out of the handle it is given, no
// value (null) is refused with the vector as it was, and the places go to the engine as they are.
static void test_vector_of_values() {
	const char *source = "package { import godot.*; public class Holder extends Node {\n"
						 "  public function made():int { var v:Vector.<Vector2> = new Vector.<Vector2>(3); v.length = 5; v.push(new Vector2(1, 2));\n"
						 "    var missing:int = 0; for (var i:int = 0; i < v.length; i++) { if (v[i] == null) missing++; } return missing; }\n"
						 "  public function arrays():int { var a:Vector.<GodotArray> = new Vector.<GodotArray>(2); return a[0] == null ? 2 : 0; }\n"
						 "  public function stores(target:Line2D):String { var v:Vector.<Vector2> = new Vector.<Vector2>(1); var none:Vector2 = null;\n"
						 "    v.push(new Vector2(1, 2)); v[2] = new Vector2(3, 4.5); var said:String = \"\";\n"
						 "    try { v.push(none); } catch (a:TypeError) { said += a.message; }\n"
						 "    try { v[3] = none; } catch (b:TypeError) { said += \"|\" + v.length; }\n"
						 "    try { v.splice(0, 2, none); } catch (c:TypeError) { said += \"|\" + v.length; }\n"
						 "    try { v.unshift(v[1], none); } catch (d:TypeError) { said += \"|\" + v.length; }\n"
						 "    v.reverse(); target.points = v.concat(v.slice(1)); v.length = 1; return said; }\n"
						 "  public var path:Vector.<Vector2> = new <Vector2>[new Vector2(1, 2), new Vector2(3, 4)];\n"
						 "  private var bytes:Vector.<byte> = new <byte>[0, 255, 256];\n"
						 "  public function byteSum():uint { return bytes[0] + bytes[1] + bytes[2]; }\n"
						 "  private var xs:Vector.<float> = new <float>[16777217, -0.0];\n"
						 "  public function single():Boolean { return xs[0] == 16777216 && 1 / xs[1] < 0; }\n"
						 "  public function give(target:Line2D):void { target.points = path; }\n"
						 "} }\n";
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Holder.as", source);
	compilation.check();
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	{
		FakeEngine engine;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		uint32_t holder = 0;
		for (size_t i = 0; i < module.classes.size(); i++) {
			holder = module.classes[i].name == "Holder" ? (uint32_t)i : holder;
		}
		vm::Object *object = interpreter.construct(holder, engine.box());
		vm::Slot result;
		engine.log.clear();
		CHECK(interpreter.call_method(object, "made", nullptr, 0, &result));
		CHECK_EQ(result.i, 0);
		size_t zeroes = 0;
		for (size_t at = engine.log.find("construct Vector2() "); at != std::string::npos; at = engine.log.find("construct Vector2() ", at + 1)) {
			zeroes++;
		}
		CHECK_EQ(zeroes, (size_t)0); // Three made and two grown by, each a slot that holds the zero whole.
		CHECK_EQ(engine.boxes, 1); // Every handle a place was read as has been let go of.
		vm::Slot argument;
		argument.bits = 0;
		argument.p = engine.box();
		engine.log.clear();
		CHECK(interpreter.call_method(object, "stores", &argument, 1, &result));
		CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("Error #1034: Type Coercion failed: cannot convert null to Vector2.|3|3|3"));
		CHECK(engine.log.find("collection PackedVector2Array[(3, 4.5), (1, 2), (0, 0), (1, 2), (0, 0)] -> ") != std::string::npos);
		vm::free_string(static_cast<vm::Str *>(result.p));
		engine.drop(argument.p);
		CHECK_EQ(engine.boxes, 1);
		engine.log.clear();
		CHECK(interpreter.call_method(object, "arrays", nullptr, 0, &result));
		CHECK_EQ(result.i, 2);
		CHECK_EQ(engine.log.find("construct"), std::string::npos);
		CHECK_EQ(engine.boxes, 1);
		// A reload carries such a vector's places as they are.
		ir::Module again;
		CHECK(ir::lower(compilation, &again, ir::LowerMode::Library));
		vm::Interpreter fresh(again, engine);
		vm::Reload reload(interpreter, fresh);
		vm::Object *moved = reload.object(object);
		CHECK(moved != nullptr);
		CHECK_EQ(reload.missing(), std::string());
		reload.globals();
		interpreter.release_object(object);
		if (moved) {
			argument.p = engine.box();
			engine.log.clear();
			CHECK(fresh.call_method(moved, "byteSum", nullptr, 0, &result));
			CHECK_EQ(result.u, 255u);
			CHECK(fresh.call_method(moved, "single", nullptr, 0, &result));
			CHECK(result.b);
			CHECK(fresh.call_method(moved, "give", &argument, 1, &result));
			CHECK(engine.log.find("collection PackedVector2Array[(1, 2), (3, 4)] -> ") != std::string::npos);
			engine.drop(argument.p);
			fresh.release_object(moved);
		}
		CHECK_EQ(engine.errors, std::string());
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
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
		// A class of Godot's constants is no type a value can be cast to.
		"function f(x:*):void { var k:* = Key(x); }",
		"function f(a:GodotArray):void { if (a) { trace(1); } }",
		// `super` written in a function expression is not the method's.
		"function f():void { var g:Function = function():void { super.queueFree(); }; }",
		"function f():void { var n:int = 1; trace(delete n); }",
		"function f():Boolean { return \"length\" in \"text\"; }",
		"function f(v:Vector2):void { for (var k:* in v) { trace(k); } }",
		"function f():void { var g:Function = function():void { trace(this.name); }; }",
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

// A constant is never written by the program, so after a reload it has what the new code gives it; a
// static variable keeps what the program made of it.
static void test_reload_constants(const char *p_package, const char *p_class) {
	const auto source = [&](int p_first) {
		const std::string n = std::to_string(p_first);
		return std::string("package ") + p_package + " { public class Game { public static const SPEED:Number = " + n + "; public static const NAMES:Array = [" + n +
				"]; public static var best:int = " + n +
				"; public function play():void { best = 5; } public function speed():Number { return SPEED; }"
				" public function name():int { return NAMES[0]; } public function bestNow():int { return best; } } }\n";
	};
	ir::Module old_module, new_module;
	compile(source(1).c_str(), &old_module, "Game.as");
	compile(source(9).c_str(), &new_module, "Game.as");
	{
		Recorder recorder;
		vm::Interpreter running(old_module, recorder);
		CHECK(running.start());
		vm::Object *game = running.construct(class_named(old_module, p_class), nullptr);
		vm::Slot result;
		CHECK(running.call_method(game, "play", nullptr, 0, &result));
		CHECK(running.call_method(game, "speed", nullptr, 0, &result) && result.d == 1);
		vm::Interpreter fresh(new_module, recorder);
		vm::Object *moved = nullptr;
		{
			vm::Reload reload(running, fresh);
			reload.globals(ir::static_changes(old_module, new_module));
			moved = reload.object(game);
			CHECK(moved != nullptr);
		}
		CHECK(fresh.call_method(moved, "speed", nullptr, 0, &result) && result.d == 9);
		CHECK(fresh.call_method(moved, "name", nullptr, 0, &result) && result.i == 9);
		CHECK(fresh.call_method(moved, "bestNow", nullptr, 0, &result) && result.i == 5);
		fresh.release_object(moved);
		running.release_object(game);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// A game being edited, for the tests of a reload that updates objects in place: one object the host holds
// (a Game, made with no arguments), moved from build to build as a host moves it.
struct Edited {
	Recorder recorder;
	std::unique_ptr<ir::Module> module;
	std::unique_ptr<vm::Interpreter> program;
	vm::Object *game = nullptr;
	bool godot = false; // The sources name Godot's classes.
	bool remember = true;
	vm::NativeCode *native = nullptr; // Stands for a program's native code, which this one does not run.
	// That native code is built to tell what its objects are constructed with (a program library built with
	// --remember): the test tells for it (told()), since the interpreter runs the constructors here.
	bool native_tells = false;
	// The old program's objects are let go of as soon as they are moved, as Godot's host lets go of each
	// instance's: they are gone by the time the objects alive are updated.
	bool let_go_early = false;
	// The host asks every program it makes to remember, whatever the one before it did (the Godot
	// extension's, in a debug build): the program a game that ran as native code is moved onto.
	bool host_remembers = false;

	std::unique_ptr<ir::Module> build(const std::string &p_source) {
		auto built = std::make_unique<ir::Module>();
		Compilation compilation;
		if (godot) {
			compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
		}
		compilation.add("Game.as", p_source);
		compilation.check();
		CHECK_EQ(compilation.format_diagnostics(false), std::string());
		CHECK(ir::lower(compilation, built.get(), ir::LowerMode::Library));
		return built;
	}
	void start(const std::string &p_source) {
		module = build(p_source);
		program = std::make_unique<vm::Interpreter>(*module, recorder);
		if (native) {
			program->set_native(native);
		}
		if (remember) {
			program->remember_constructions(native_tells);
		}
		CHECK(program->start());
		game = program->construct(class_named(*module, "Game"), nullptr);
		CHECK(game != nullptr);
	}
	// The game moved onto a build of these sources, as a host does it; what the update says, as one text.
	// With p_tries, the move is first made and thrown away that many times, as a host does when the new
	// build lacks a container's class.
	std::string edit(const std::string &p_source, int p_tries = 0) {
		std::unique_ptr<ir::Module> next = build(p_source);
		for (int i = 0; i < p_tries; i++) {
			vm::Interpreter dropped(*next, recorder);
			vm::Reload reload(*program, dropped);
			dropped.release_object(reload.object(game));
		}
		auto fresh = std::make_unique<vm::Interpreter>(*next, recorder);
		if (host_remembers) {
			fresh->remember_constructions();
		}
		vm::InPlace done;
		vm::Object *moved = nullptr;
		{
			vm::Reload reload(*program, *fresh);
			reload.globals(ir::static_changes(*module, *next));
			moved = reload.object(game);
			if (let_go_early) {
				program->release_object(game);
			}
			done = reload.update_in_place(ir::run_once_changes(*module, *next));
		}
		if (!let_go_early) {
			program->release_object(game);
		}
		game = moved;
		program = std::move(fresh);
		module = std::move(next);
		std::string said = "updated:";
		for (const std::string &name : done.updated) {
			said += " " + name;
		}
		for (const auto &[name, why] : done.left) {
			said += "; " + name + ": " + why;
		}
		return said;
	}
	void call(const char *p_method) {
		vm::Slot result;
		CHECK(program->call_method(game, p_method, nullptr, 0, &result));
	}
	// What native code says as the constructor of the object a field of the game holds begins: the
	// arguments it was called with, after the object itself.
	void told(const char *p_field, std::vector<vm::Slot> p_arguments) {
		vm::Object *made = static_cast<vm::Object *>(field(p_field).p);
		CHECK(made != nullptr);
		vm::Slot self;
		self.bits = 0;
		self.p = made;
		p_arguments.insert(p_arguments.begin(), self);
		program->constructing(module->classes[made->klass].constructor, p_arguments.data(), p_arguments.size());
	}
	// An object of the class, made by the host as Godot makes a script's, held by a field of the game.
	void hold(const char *p_field, const char *p_class) {
		vm::Slot made;
		made.bits = 0;
		made.p = program->construct(class_named(*module, p_class), nullptr);
		CHECK(made.p != nullptr);
		program->set_field(game, *module->classes[game->klass].field(p_field), made);
	}
	// A field of the game, or with a dot of the object one of its fields holds ("token.w").
	vm::Slot field(const std::string &p_path) {
		vm::Slot value;
		value.bits = 0;
		const size_t dot = p_path.find('.');
		CHECK(program->get_field(game, p_path.substr(0, dot), &value));
		if (dot != std::string::npos) {
			vm::Object *held = static_cast<vm::Object *>(value.p);
			value.bits = 0;
			CHECK(held && program->get_field(held, p_path.substr(dot + 1), &value));
		}
		return value;
	}
	double number(const std::string &p_path) { return field(p_path).d; }
	std::string text(const std::string &p_path) {
		const vm::Str *held = static_cast<const vm::Str *>(field(p_path).p);
		return held ? std::string(held->view()) : std::string("null");
	}
	void end() {
		program->release_object(game);
		program.reset();
		module.reset();
		CHECK_EQ(vm::live_objects(), (size_t)0);
		CHECK_EQ(vm::live_strings(), (size_t)0);
	}
};

static std::string with(std::string p_source, const char *p_from, const char *p_to) {
	const size_t at = p_source.find(p_from);
	CHECK(at != std::string::npos);
	return at == std::string::npos ? p_source : p_source.replace(at, std::string(p_from).size(), p_to);
}

// A game's own shape: a constant of one class, read by a constructor that hands it up through two
// `super(...)` calls to the class that sets the fields. Editing the constant while the game runs changes the
// width of the objects alive, and leaves where the game moved one to.
static const char *const TOKENS =
		"package { public class Game { public var token:Token; public var other:Token;\n"
		"  public function make():void { token = new Token(100, 200); other = new Token(300, 50); }\n"
		"  public function pull():void { token.x += 7; } public function stretch():void { other.h = 99; } } }\n"
		"class Tuning { public static const TOKEN_W:Number = 24; public static const TOKEN_H:Number = 90; }\n"
		"class Box { public var x:Number; public var y:Number; public var w:Number; public var h:Number;\n"
		"  public function Box(p_x:Number, p_y:Number, p_w:Number, p_h:Number) { x = p_x; this.y = p_y; w = p_w; h = p_h; } }\n"
		"class Thing extends Box { public var active:Boolean = true;\n"
		"  public function Thing(p_x:Number, p_y:Number, p_w:Number, p_h:Number) { super(p_x, p_y, p_w, p_h); } }\n"
		"class Token extends Thing {\n"
		"  public function Token(sx:Number, sy:Number) { super(sx - Tuning.TOKEN_W / 2, sy - Tuning.TOKEN_H / 2, Tuning.TOKEN_W, Tuning.TOKEN_H); } }\n";

static void test_reload_in_place() {
	Edited edited;
	edited.start(TOKENS);
	edited.call("make");
	edited.call("pull");
	CHECK_EQ(edited.number("token.x"), 95.0);
	CHECK_EQ(edited.program->remembered(), (size_t)3);
	// The constant alone is edited: no code of Token's changed.
	CHECK_EQ(edited.edit(with(TOKENS, "TOKEN_W:Number = 24", "TOKEN_W:Number = 30")), std::string("updated: Token"));
	CHECK_EQ(edited.number("token.w"), 30.0);
	CHECK_EQ(edited.number("other.w"), 30.0);
	CHECK_EQ(edited.number("token.x"), 95.0); // Where the game moved it to.
	CHECK_EQ(edited.number("other.x"), 285.0); // Never moved: where the new width puts it.
	CHECK_EQ(edited.number("token.h"), 90.0);
	CHECK_EQ(edited.number("token.y"), 155.0);
	CHECK_EQ(edited.program->remembered(), (size_t)3);
	// A second edit starts from what the first one gave; a field the game wrote since keeps what it wrote.
	edited.call("stretch");
	const std::string twice = with(with(TOKENS, "TOKEN_W:Number = 24", "TOKEN_W:Number = 40"), "TOKEN_H:Number = 90", "TOKEN_H:Number = 60");
	CHECK_EQ(edited.edit(twice), std::string("updated: Token"));
	CHECK_EQ(edited.number("token.w"), 40.0);
	CHECK_EQ(edited.number("other.x"), 280.0);
	CHECK_EQ(edited.number("token.x"), 95.0);
	CHECK_EQ(edited.number("token.h"), 60.0);
	CHECK_EQ(edited.number("other.h"), 99.0);
	CHECK_EQ(edited.number("other.y"), 20.0);
	// An edit that changes nothing a constructor gives says nothing, and one to code that is not a constructor's too.
	CHECK_EQ(edited.edit(twice), std::string("updated:"));
	CHECK_EQ(edited.edit(with(twice, "token.x += 7", "token.x += 8")), std::string("updated:"));
	// Objects made after a reload are made by the new code, and remembered like any other.
	edited.call("make");
	CHECK_EQ(edited.number("token.w"), 40.0);
	CHECK_EQ(edited.program->remembered(), (size_t)3);
	CHECK_EQ(edited.edit(TOKENS), std::string("updated: Token"));
	CHECK_EQ(edited.number("token.w"), 24.0);
	CHECK_EQ(edited.number("token.x"), 88.0);
	// A constructor's own code edited, and a field's first value.
	CHECK_EQ(edited.edit(with(with(TOKENS, "w = p_w;", "w = p_w * 2;"), "active:Boolean = true", "active:Boolean = false")),
			std::string("updated: Token"));
	CHECK_EQ(edited.number("token.w"), 48.0);
	CHECK(!edited.field("token.active").b);
	edited.end();
}

// First values that are NaN and text, a default left out, a field added, and a move made twice.
static void test_reload_in_place_values() {
	const std::string source =
			"package { public class Game { public var a:Part; public var b:Part;\n"
			"  public function make():void { a = new Part(); b = new Part(2, \"given\"); b.label = \"mine\"; } } }\n"
			"class Words { public static const GAP:Number = 0 / 0; public static const NAME:String = \"one\"; }\n"
			"class Part { public var gap:Number = Words.GAP; public var label:String; public var size:Number; public var free:*; public var any:* = Words.NAME;\n"
			"  public function Part(p_size:Number = 5, p_label:String = null) { size = p_size; label = p_label == null ? Words.NAME : p_label + Words.NAME; } }\n";
	Edited edited;
	edited.start(source);
	edited.call("make");
	CHECK(edited.number("a.gap") != edited.number("a.gap"));
	// NaN is the value the field still holds: it takes the new one. Text is compared by what it says.
	std::string next = with(with(source, "GAP:Number = 0 / 0", "GAP:Number = 3"), "NAME:String = \"one\"", "NAME:String = \"two\"");
	CHECK_EQ(edited.edit(next, 2), std::string("updated: Part"));
	CHECK_EQ(edited.number("a.gap"), 3.0);
	CHECK_EQ(edited.text("a.label"), std::string("two"));
	CHECK_EQ(edited.text("b.label"), std::string("mine"));
	CHECK(vm::any::tag(edited.field("a.any")) == vm::AnyTag::String);
	CHECK(vm::any::tag(edited.field("a.free")) == vm::AnyTag::Undefined);
	// A default the object was made without is the new default; an argument that was given is given again.
	next = with(next, "p_size:Number = 5", "p_size:Number = 8");
	CHECK_EQ(edited.edit(next), std::string("updated: Part"));
	CHECK_EQ(edited.number("a.size"), 8.0);
	CHECK_EQ(edited.number("b.size"), 2.0);
	// A field that is new has its first value on the objects alive, and NaN again is no change.
	next = with(with(next, "public var size:Number;", "public var size:Number; public var lives:int = 3;"), "GAP:Number = 3", "GAP:Number = 0 / 0");
	CHECK_EQ(edited.edit(next), std::string("updated: Part"));
	CHECK_EQ(edited.field("a.lives").i, 3);
	CHECK(edited.number("b.gap") != edited.number("b.gap"));
	CHECK_EQ(edited.edit(with(next, "lives:int = 3", "lives:int = 4")), std::string("updated: Part"));
	CHECK_EQ(edited.field("b.lives").i, 4);
	edited.end();
}

// What can't be made again is left as it is, and named with the reason.
static void test_reload_in_place_refused() {
	const std::string source =
			"package { public class Game { public var a:Reads; public var b:Calls; public var c:Makes; public var d:Holds; public var e:Fine;\n"
			"  public var f:Fails; public var g:Takes;\n"
			"  public function make():void { a = new Reads(); b = new Calls(); c = new Makes(); d = new Holds(c); e = new Fine(); f = new Fails(); g = new Takes(1); } } }\n"
			"class K { public static const N:Number = 1; public static var count:int = 0; public static function next():Number { return count + N; } }\n"
			"class Reads { public var n:Number = K.N + K.count; }\n"
			"class Calls { public var n:Number; public function Calls() { n = K.next(); } }\n"
			"class Makes { public var n:Number = K.N; public var list:Array = []; }\n"
			"class Holds { public var n:Number = K.N; public var held:Makes; public function Holds(p_held:Makes) { held = p_held; } }\n"
			"class Fine { public var n:Number = K.N; }\n"
			"class Fails { public var n:Number = K.N; public var name:String; public function Fails() { n += 0; } }\n"
			"class Takes { public var n:Number; public function Takes(p_n:Number) { n = p_n + K.N; } }\n"
			"class Unmade { public var n:Number = K.N + K.count; }\n";
	Edited edited;
	edited.start(source);
	edited.call("make");
	const size_t errors = edited.recorder.errors.size();
	// Fails's constructor raises in the new code (a member of null); Takes's takes other arguments.
	const std::string next = with(with(with(source, "N:Number = 1;", "N:Number = 2;"), "{ n += 0; }", "{ n += name.length; }"), "Takes(p_n:Number)", "Takes(p_n:int)");
	CHECK_EQ(edited.edit(next),
			std::string("updated: Fine"
						"; Calls: K/next() reads the static variable K.count"
						"; Fails: its constructor failed when it was run again with the new code"
						"; Holds: an object of it was made with an argument or a first value that is not a number, a Boolean, text or null"
						"; Makes: Makes() makes an object"
						"; Reads: Reads() reads the static variable K.count"
						"; Takes: its constructor's parameters changed"));
	CHECK(edited.recorder.errors.size() > errors); // The error is the host's to show.
	CHECK_EQ(edited.number("e.n"), 2.0);
	CHECK_EQ(edited.number("a.n"), 1.0);
	CHECK_EQ(edited.number("d.n"), 1.0);
	CHECK_EQ(edited.number("f.n"), 1.0);
	CHECK_EQ(edited.number("g.n"), 2.0);
	edited.end();
	// A class of Godot's is never made again: the throwaway would be one of Godot's objects.
	Edited scripts;
	scripts.godot = true;
	const std::string node = "package { import godot.*; public class Game { public var spinner:Spinner; } }\n"
							 "class K { public static const N:Number = 1; }\n"
							 "import godot.Node;\nclass Spinner extends Node { public var speed:Number = K.N; }\n";
	scripts.start(node);
	scripts.hold("spinner", "Spinner");
	CHECK_EQ(scripts.edit(with(node, "N:Number = 1", "N:Number = 2")), std::string("updated:; Spinner: it extends Godot's Node"));
	CHECK_EQ(scripts.number("spinner.speed"), 1.0);
	CHECK_EQ(scripts.program->remembered(), (size_t)1); // The game alone: nothing of an object of Godot's is kept.
	scripts.end();
}

// Nothing is remembered that is not asked for, what is remembered goes with its object, and an object that
// takes a freed one's place is not taken for it.
static void test_reload_in_place_memory() {
	const std::string source = "package { public class Game { public var kept:Shot; public var loose:Shot;\n"
							   "  public function churn():void { for (var i:int = 0; i < 500; i++) { loose = new Shot(i); } kept = new Shot(7); loose = null; }\n"
							   "  public function fill():Number { var all:Array = []; for (var i:int = 0; i < 300; i++) { all.push(new Shot(i)); }\n"
							   "    var sum:Number = 0; for (var j:int = 0; j < 300; j += 7) { all[j] = null; } return sum; } } }\n"
							   "class K { public static const N:Number = 1; }\n"
							   "class Shot { public var n:Number; public var label:String; public function Shot(p_n:Number) { n = p_n + K.N; label = \"s\" + p_n; } }\n";
	Edited off;
	off.remember = false;
	off.start(source);
	off.call("churn");
	CHECK_EQ(off.program->remembered(), (size_t)0);
	CHECK_EQ(off.edit(with(source, "N:Number = 1", "N:Number = 5")), std::string("updated:; Shot: what its objects were made with is not remembered"));
	CHECK_EQ(off.number("kept.n"), 8.0);
	off.end();
	Edited on;
	on.start(source);
	on.call("fill"); // Many alive at once, and gone again: each gives up what was kept of it.
	CHECK_EQ(on.program->remembered(), (size_t)1);
	on.call("churn");
	CHECK_EQ(on.program->remembered(), (size_t)2); // The game and the one it keeps.
	CHECK_EQ(on.edit(with(source, "N:Number = 1", "N:Number = 5")), std::string("updated: Shot"));
	CHECK_EQ(on.number("kept.n"), 12.0);
	CHECK_EQ(on.text("kept.label"), std::string("s7"));
	CHECK_EQ(on.program->remembered(), (size_t)2);
	on.call("churn");
	CHECK_EQ(on.program->remembered(), (size_t)2);
	on.end();
	// Asked for once an object has been made, nothing is remembered: what is remembered is kept with each
	// object, and one made before has no room for it.
	Edited late;
	late.remember = false;
	late.start(source);
	late.program->remember_constructions();
	late.call("churn");
	CHECK_EQ(late.program->remembered(), (size_t)0);
	CHECK_EQ(late.edit(with(source, "N:Number = 1", "N:Number = 5")), std::string("updated:; Shot: what its objects were made with is not remembered"));
	CHECK_EQ(late.number("kept.n"), 8.0);
	late.end();
	// Nor of a program with native code, which keeps freed objects to use again and tells nobody.
	struct NoNative final : vm::NativeCode {
		[[nodiscard]] bool hand_over(vm::NativeError *) override { return false; }
		void clear_globals() override {}
		vm::Slot *global(uint32_t) override { return nullptr; }
		bool statics_given(uint32_t) const override { return false; }
		[[nodiscard]] bool run_native(uint32_t, uint32_t, uint32_t, vm::Slot *, uint32_t, uint32_t, vm::Slot *) override { return false; }
	} none;
	Edited compiled;
	compiled.native = &none;
	compiled.start(source);
	compiled.call("churn");
	CHECK_EQ(compiled.program->remembered(), (size_t)0);
	compiled.end();
}

// A program whose code is native (a program library's) moved onto a build the interpreter runs, as an edit
// of a game that runs as native code does it: its static fields are where native code keeps them, a class
// native code gave its static fields is not given them again, and its objects, of which nothing is
// remembered, are said to have been made as native code, at this reload and at those after.
static void test_reload_from_native() {
	const std::string source =
			"package { public class Game { public static var count:int = 3; public var kept:Shot = new Shot(2); public var seen:Number = 0;\n"
			"  public function look():void { seen = count + Late.v; } public function more():void { kept = new Shot(4); } } }\n"
			"class K { public static const N:Number = 1; }\n"
			"class Late { public static var v:int = 7; }\n"
			"class Shot { public var n:Number; public function Shot(p:Number) { n = p + K.N; } }\n";
	struct Native final : vm::NativeCode {
		std::vector<vm::Slot> statics;
		std::vector<uint8_t> used;
		[[nodiscard]] bool hand_over(vm::NativeError *) override { return false; }
		void clear_globals() override {}
		vm::Slot *global(uint32_t p_index) override { return &statics[p_index]; }
		bool statics_given(uint32_t p_class) const override { return used[p_class] != 0; }
		[[nodiscard]] bool run_native(uint32_t, uint32_t, uint32_t, vm::Slot *, uint32_t, uint32_t, vm::Slot *) override { return false; }
	} native;
	Edited edited;
	edited.native = &native;
	edited.host_remembers = true;
	edited.start(source);
	CHECK_EQ(edited.program->remembered(), (size_t)0);
	// What native code would hold by now: its own values of the static fields, and Late used, which the
	// interpreter standing for it here never touched.
	const auto global = [&](const char *p_name) {
		const auto found = std::find_if(edited.module->globals.begin(), edited.module->globals.end(), [&](const ir::Global &p_global) { return p_global.name == p_name; });
		CHECK(found != edited.module->globals.end());
		return static_cast<size_t>(found - edited.module->globals.begin());
	};
	native.statics.assign(edited.module->globals.size(), vm::Slot{});
	native.used.assign(edited.module->classes.size(), 0);
	native.statics[global("Game.count")].i = 42;
	native.statics[global("Late.v")].i = 9;
	native.used[class_named(*edited.module, "Game")] = 1;
	native.used[class_named(*edited.module, "Late")] = 1;
	const std::string natively = "an object of it was made while the scripts ran as native code that does not remember what objects are made with";
	const std::string next = with(source, "N:Number = 1", "N:Number = 5");
	CHECK_EQ(edited.edit(next), "updated:; Game: Game() makes an object; Shot: " + natively);
	edited.call("look");
	CHECK_EQ(edited.number("seen"), 51.0); // 42 and 9: native code's, and Late's 9 not made 7 again.
	CHECK_EQ(edited.number("kept.n"), 3.0); // As it was made.
	// The interpreter runs it from here on: an edit after is of a program that remembers, and the object
	// made before is still one of which nothing is.
	CHECK_EQ(edited.edit(with(next, "N:Number = 5", "N:Number = 6")), "updated:; Game: Game() makes an object; Shot: " + natively);
	CHECK_EQ(edited.number("kept.n"), 3.0);
	// One made since is remembered, and updated in place.
	edited.call("more");
	CHECK_EQ(edited.number("kept.n"), 10.0);
	CHECK_EQ(edited.edit(with(next, "N:Number = 5", "N:Number = 8")), std::string("updated: Shot; Game: Game() makes an object"));
	CHECK_EQ(edited.number("kept.n"), 12.0);
	edited.end();
}

// The same where the native code tells what its objects are constructed with, as a program library built
// for a game that is being edited does: an object made as native code has its arguments remembered, in the
// room every object is made with, and the reload onto the interpreter updates it in place as it does an
// object the interpreter made. Its first values are what the old program's chain gives for the arguments,
// run again by the old program. A class whose chain could not be run again as the library was built is
// told of by nobody, and says so; an object made with an object for an argument says that.
static void test_reload_from_native_that_tells() {
	const std::string source =
			"package { public class Game { public var kept:Shot = new Shot(2); public var moved:Shot = new Shot(3, \"m\");\n"
			"  public var loud:Loud = new Loud(1); public var held:Holder = new Holder(kept);\n"
			"  public function push():void { moved.n = 99; } public function more():void { kept = new Shot(4); } } }\n"
			"class K { public static const N:Number = 1; public static const TAG:String = \"-\"; }\n"
			"class Shot { public var n:Number; public var tag:String;\n"
			"  public function Shot(p:Number, t:String = \"s\") { n = p + K.N; tag = t + K.TAG; } }\n"
			"class Loud { public var n:Number; public function Loud(p:Number) { n = p + K.N; trace(\"loud\"); } }\n"
			"class Holder { public var n:Number; public function Holder(of:Shot) { n = K.N; } }\n";
	struct Native final : vm::NativeCode {
		std::vector<vm::Slot> statics;
		[[nodiscard]] bool hand_over(vm::NativeError *) override { return false; }
		void clear_globals() override {}
		vm::Slot *global(uint32_t p_index) override { return &statics[p_index]; }
		bool statics_given(uint32_t) const override { return true; }
		[[nodiscard]] bool run_native(uint32_t, uint32_t, uint32_t, vm::Slot *, uint32_t, uint32_t, vm::Slot *) override { return false; }
	};
	const auto number = [](double p_value) {
		vm::Slot slot;
		slot.bits = 0;
		slot.d = p_value;
		return slot;
	};
	for (const bool early : { false, true }) {
		Native native;
		Edited edited;
		edited.native = &native;
		edited.native_tells = true;
		edited.host_remembers = true;
		edited.let_go_early = early;
		// The static fields are native code's: here, what the interpreter standing for it gave them.
		edited.start(source);
		const auto global = [&](const char *p_name) {
			const auto found = std::find_if(edited.module->globals.begin(), edited.module->globals.end(), [&](const ir::Global &p_global) { return p_global.name == p_name; });
			CHECK(found != edited.module->globals.end());
			return static_cast<size_t>(found - edited.module->globals.begin());
		};
		native.statics.assign(edited.module->globals.size(), vm::Slot{});
		vm::Str *const dash = vm::new_string("-");
		native.statics[global("K.N")].d = 1;
		native.statics[global("K.TAG")].p = dash;
		// Nothing is remembered of a constructor the interpreter ran under native code: only what is told.
		CHECK(edited.program->remembers());
		CHECK_EQ(edited.program->remembered(), (size_t)0);
		vm::Slot tag;
		tag.bits = 0;
		tag.p = vm::new_string("m");
		edited.told("kept", { number(2) });
		edited.told("moved", { number(3), tag });
		edited.told("held", { edited.field("kept") }); // An object for an argument: told of, and not remembered.
		if (vm::release(static_cast<vm::Str *>(tag.p))) {
			vm::free_string(static_cast<vm::Str *>(tag.p));
		}
		CHECK_EQ(edited.program->remembered(), (size_t)2);
		edited.call("push"); // The game changes a field of one.
		const std::string once = "an object of it was made while the scripts ran as native code, by a constructor that could not be run again then";
		const std::string not_plain = "an object of it was made with an argument or a first value that is not a number, a Boolean, text or null";
		const std::string next = with(with(source, "N:Number = 1", "N:Number = 5"), "TAG:String = \"-\"", "TAG:String = \"+\"");
		CHECK_EQ(edited.edit(next), "updated: Shot; Game: Game() makes an object; Holder: " + not_plain + "; Loud: Loud() prints");
		CHECK_EQ(edited.number("kept.n"), 7.0); // 2 and the constant's 5.
		CHECK_EQ(edited.text("kept.tag"), std::string("s+")); // Its default, given again.
		CHECK_EQ(edited.number("moved.n"), 99.0); // What the game gave it is kept.
		CHECK_EQ(edited.text("moved.tag"), std::string("m+"));
		CHECK_EQ(edited.number("loud.n"), 2.0);
		CHECK_EQ(edited.number("held.n"), 1.0);
		// A second edit starts from what the first gave; the field the game changed is still the game's.
		const std::string third = with(next, "N:Number = 5", "N:Number = 6");
		CHECK_EQ(edited.edit(third), "updated: Shot; Game: Game() makes an object; Holder: " + not_plain + "; Loud: Loud() prints");
		CHECK_EQ(edited.number("kept.n"), 8.0);
		CHECK_EQ(edited.number("moved.n"), 99.0);
		// An edit that lets Loud's constructor be run again: the object made as native code has nothing
		// remembered, since nobody told of it, and that is what is said.
		const std::string quiet = with(with(third, " trace(\"loud\");", ""), "N:Number = 6", "N:Number = 7");
		CHECK_EQ(edited.edit(quiet), "updated: Shot; Game: Game() makes an object; Holder: " + not_plain + "; Loud: " + once);
		CHECK_EQ(edited.number("loud.n"), 2.0);
		CHECK_EQ(edited.number("kept.n"), 9.0);
		// One made since, by the interpreter, is remembered as any of its own.
		edited.call("more");
		CHECK_EQ(edited.number("kept.n"), 11.0);
		CHECK_EQ(edited.edit(with(quiet, "N:Number = 7", "N:Number = 8")), "updated: Shot; Game: Game() makes an object; Holder: " + not_plain + "; Loud: " + once);
		CHECK_EQ(edited.number("kept.n"), 12.0);
		if (vm::release(dash)) {
			vm::free_string(dash);
		}
		edited.end();
	}
}

// Which objects are remembered at all, and that what is remembered of one goes with it however it ends: a
// class whose constructor takes any number of arguments is not, a constructor that raises leaves nothing
// behind, and text that was an argument or a first value is let go of with the object.
static void test_reload_in_place_kept() {
	const std::string source =
			"package { public class Game { public var a:Named; public var b:Named; public var rest:Rest; public var made:int = 0;\n"
			"  public function make():void { a = new Named(\"a\" + made, made); b = new Named(\"b\" + made); rest = new Rest(1, 2); made++;\n"
			"    for (var i:int = 0; i < 40; i++) { try { new Named(\"bad\" + i, i); } catch (e:Error) { } var gone:Named = new Named(\"go\", \"any\" + i); } } } }\n"
			"class K { public static const N:Number = 1; public static const TAG:String = \"-\"; }\n"
			"class Named { public var name:String; public var extra:*; public var n:Number = K.N; public var none:String;\n"
			"  public function Named(p_name:String, p_extra:* = null) { name = p_name + K.TAG; extra = p_extra;\n"
			"    if (p_name.length > 3) { n += none.length; } } }\n"
			"class Rest { public var n:Number = K.N; public function Rest(...p_all) { n += p_all.length; } }\n";
	Edited edited;
	edited.start(source);
	edited.call("make");
	CHECK_EQ(edited.program->remembered(), (size_t)3); // The game and its two: not Rest's, and none that is gone.
	edited.call("make"); // The two it held are let go of, with the text they were made with.
	CHECK_EQ(edited.program->remembered(), (size_t)3);
	const std::string next = with(with(source, "N:Number = 1", "N:Number = 4"), "TAG:String = \"-\"", "TAG:String = \"+\"");
	CHECK_EQ(edited.edit(next), std::string("updated: Named; Rest: Rest() takes any number of arguments"));
	CHECK_EQ(edited.text("a.name"), std::string("a1+"));
	CHECK_EQ(edited.text("b.name"), std::string("b1+"));
	CHECK_EQ(edited.number("a.n"), 4.0);
	CHECK_EQ(vm::any::to_number(edited.field("a.extra")), 1.0);
	CHECK_EQ(edited.number("rest.n"), 3.0);
	// A second edit starts from what the first gave, and the objects made since are remembered by the new program.
	edited.call("make");
	CHECK_EQ(edited.program->remembered(), (size_t)3);
	CHECK_EQ(edited.edit(with(next, "TAG:String = \"+\"", "TAG:String = \"*\"")), std::string("updated: Named"));
	CHECK_EQ(edited.text("a.name"), std::string("a2*"));
	CHECK_EQ(edited.number("b.n"), 4.0);
	// A move that ends without update_in_place(): what the first values of the objects moved were went
	// with the old program, and nothing is remembered of them rather than something that may be wrong.
	std::unique_ptr<ir::Module> another = edited.build(with(next, "N:Number = 4", "N:Number = 6"));
	auto fresh = std::make_unique<vm::Interpreter>(*another, edited.recorder);
	vm::Object *moved = nullptr;
	{
		vm::Reload reload(*edited.program, *fresh);
		reload.globals();
		moved = reload.object(edited.game);
	}
	edited.program->release_object(edited.game);
	edited.game = moved;
	edited.program = std::move(fresh);
	edited.module = std::move(another);
	CHECK_EQ(edited.program->remembered(), (size_t)0);
	CHECK_EQ(edited.edit(with(next, "N:Number = 4", "N:Number = 9")),
			std::string("updated:; Named: an object of it was made with an argument or a first value that is not a number, a Boolean, text or null"
						"; Rest: Rest() takes any number of arguments"));
	CHECK_EQ(edited.number("a.n"), 4.0);
	edited.end();
}

// A class whose constructor can be run again at one time and not at another, and a new constructor that
// fails and is then put right: an object alive is updated by the first edit after which it can be.
static void test_reload_in_place_turns() {
	const std::string source =
			"package { public class Game { public var a:Loud; public var b:Quiet; public var c:Quiet;\n"
			"  public function make():void { a = new Loud(2); b = new Quiet(3, \"b\"); c = new Quiet(4); }\n"
			"  public function push():void { c.n = 50; } } }\n"
			"class K { public static const N:Number = 1; public static var count:int = 0; }\n"
			"class Loud { public var n:Number; public var tag:String = \"t\"; public function Loud(p_n:Number) { n = p_n + K.N; K.count++; } }\n"
			"class Quiet { public var n:Number; public var name:String; public var none:String;\n"
			"  public function Quiet(p_n:Number, p_name:String = \"q\") { n = p_n + K.N; name = p_name + K.N; } }\n";
	Edited edited;
	edited.let_go_early = true;
	edited.start(source);
	edited.call("make");
	// Loud's constructor counts: it can't be run again, and its objects are left. Quiet's can.
	std::string next = with(source, "N:Number = 1", "N:Number = 2");
	CHECK_EQ(edited.edit(next), std::string("updated: Quiet; Loud: Loud() reads the static variable K.count"));
	CHECK_EQ(edited.number("a.n"), 3.0);
	CHECK_EQ(edited.number("b.n"), 5.0);
	CHECK_EQ(edited.text("b.name"), std::string("b2"));
	CHECK_EQ(edited.text("c.name"), std::string("q2"));
	// It no longer counts: the object made while it did is updated, from what that constructor left it with.
	next = with(with(next, " K.count++;", ""), "tag:String = \"t\"", "tag:String = \"u\"");
	CHECK_EQ(edited.edit(next), std::string("updated: Loud"));
	CHECK_EQ(edited.number("a.n"), 4.0);
	CHECK_EQ(edited.text("a.tag"), std::string("u"));
	// Quiet's new constructor fails (a member of null): its objects are as they were. Put right, with another
	// constant, they are updated from what they were made with, but for a field the game wrote meanwhile.
	const size_t errors = edited.recorder.errors.size();
	std::string broken = with(next, "name = p_name + K.N; }", "name = p_name + K.N; n += none.length; }");
	CHECK_EQ(edited.edit(broken), std::string("updated:; Quiet: its constructor failed when it was run again with the new code"));
	CHECK(edited.recorder.errors.size() > errors);
	CHECK_EQ(edited.number("b.n"), 5.0);
	edited.call("push");
	next = with(next, "N:Number = 2", "N:Number = 7");
	CHECK_EQ(edited.edit(next), std::string("updated: Loud Quiet"));
	CHECK_EQ(edited.number("b.n"), 10.0);
	CHECK_EQ(edited.text("b.name"), std::string("b7"));
	CHECK_EQ(edited.number("c.n"), 50.0);
	CHECK_EQ(edited.text("c.name"), std::string("q7"));
	CHECK_EQ(edited.number("a.n"), 9.0);
	// Quiet's constructor prints, and then no longer does: left while it did, and updated by the edit after.
	std::string printing = with(next, "name = p_name + K.N; }", "name = p_name + K.N; trace(name); }");
	CHECK_EQ(edited.edit(printing), std::string("updated:; Quiet: Quiet() prints"));
	printing = with(printing, "N:Number = 7", "N:Number = 8");
	CHECK_EQ(edited.edit(printing), std::string("updated: Loud; Quiet: Quiet() prints"));
	CHECK_EQ(edited.number("b.n"), 10.0);
	CHECK_EQ(edited.number("a.n"), 10.0);
	CHECK_EQ(edited.edit(with(printing, " trace(name);", "")), std::string("updated: Quiet"));
	CHECK_EQ(edited.number("b.n"), 11.0);
	CHECK_EQ(edited.text("b.name"), std::string("b8"));
	CHECK_EQ(edited.number("c.n"), 50.0);
	CHECK_EQ(edited.text("c.name"), std::string("q8"));
	// An edit to nothing a constructor gives, then one that is: the objects carried through the first take it.
	next = with(with(printing, " trace(name);", ""), "c.n = 50", "c.n = 51");
	CHECK_EQ(edited.edit(next), std::string("updated:"));
	CHECK_EQ(edited.edit(with(next, "N:Number = 8", "N:Number = 9")), std::string("updated: Loud Quiet"));
	CHECK_EQ(edited.number("b.n"), 12.0);
	CHECK_EQ(edited.number("a.n"), 11.0);
	edited.end();
}

// An object made while its constants' class was still being given its static fields read a constant
// that had no value yet. Its first values are taken to be what the chain gives now, so the field looks
// written by the program and is left as it is: what the game would hold if it were started again.
static void test_reload_in_place_early() {
	const std::string source =
			"package { public class Game { public var seen:Part; public var late:Part;\n"
			"  public function make():void { seen = K.early; late = new Part(); } } }\n"
			"class K { public static var early:Part = new Part(); public static const N:Number = 5; }\n"
			"class Part { public var n:Number = K.N; }\n";
	Edited edited;
	edited.start(source);
	edited.call("make");
	CHECK(edited.number("seen.n") != edited.number("seen.n"));
	CHECK_EQ(edited.number("late.n"), 5.0);
	CHECK_EQ(edited.edit(with(source, "N:Number = 5", "N:Number = 6")), std::string("updated: Part"));
	CHECK(edited.number("seen.n") != edited.number("seen.n"));
	CHECK_EQ(edited.number("late.n"), 6.0);
	edited.end();
}

// A field whose Godot class changed takes the old field's object only if it is of the new class: the
// calls prepared for the new class would otherwise run on an object of another.
static void test_reload_engine_class() {
	struct Classed : FakeEngine {
		std::string refused;
		[[nodiscard]] bool is_class(void *p_value, std::string_view p_class) override { return p_value != nullptr && p_class != refused; }
	};
	const auto lowered = [](const char *p_type, ir::Module *r_module) {
		Compilation program;
		program.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
		program.add("Rig.as", std::string("package { import godot.*; public class Rig extends Node { public var cam:") + p_type +
									  "; public var other:Node2D; } }");
		program.check();
		CHECK_EQ(program.format_diagnostics(false), std::string());
		CHECK(ir::lower(program, r_module, ir::LowerMode::Library));
	};
	ir::Module old_module, new_module;
	lowered("Node2D", &old_module);
	lowered("Camera2D", &new_module);
	{
		Classed engine;
		engine.refused = "Camera2D";
		vm::Interpreter running(old_module, engine);
		CHECK(running.start());
		const uint32_t rig_class = class_named(old_module, "Rig");
		vm::Object *rig = running.construct(rig_class, engine.box());
		const auto field = [&](const char *p_name) {
			const std::vector<ir::Field> &fields = old_module.classes[rig_class].fields;
			for (uint32_t i = 0; i < fields.size(); i++) {
				if (fields[i].name == p_name) {
					return i;
				}
			}
			return uint32_t(0);
		};
		vm::Slot held;
		held.bits = 0;
		held.p = engine.box();
		void *const camera = held.p;
		running.set_field(rig, field("cam"), held);
		held.p = engine.box();
		void *const other = held.p;
		running.set_field(rig, field("other"), held);
		vm::Slot now;
		{
			vm::Interpreter fresh(new_module, engine);
			vm::Object *moved = nullptr;
			{
				vm::Reload reload(running, fresh);
				moved = reload.object(rig);
				CHECK(moved != nullptr);
			}
			CHECK(fresh.get_field(moved, "cam", &now) && now.p == nullptr);
			CHECK(fresh.get_field(moved, "other", &now) && now.p == other);
			fresh.release_object(moved);
		}
		// Where the object is of the new class too, it is carried.
		engine.refused.clear();
		{
			vm::Interpreter fresh(new_module, engine);
			vm::Object *moved = nullptr;
			{
				vm::Reload reload(running, fresh);
				moved = reload.object(rig);
			}
			CHECK(fresh.get_field(moved, "cam", &now) && now.p == camera);
			fresh.release_object(moved);
		}
		running.release_object(rig);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// The container behind Object, Dictionary and an Array's properties: insertion order kept through deletes
// and through the packing that drops deleted entries; every key found again after it.
static void test_entries() {
	vm::Entries entries;
	std::vector<vm::Str *> made;
	for (int i = 0; i < 40; i++) {
		made.push_back(vm::new_string("k" + std::to_string(i)));
		entries.add(vm::any::string(made.back()), vm::any::integer(i));
	}
	for (int i = 0; i < 40; i += 3) {
		const std::optional<uint32_t> at = entries.find_text("k" + std::to_string(i));
		CHECK(at.has_value());
		if (at) {
			entries.remove(*at); // The key is still held in `made`.
		}
	}
	vm::Str *last = vm::new_string("last");
	entries.add(vm::any::string(last), vm::any::integer(99)); // Packs: 14 deleted of 40.
	std::string order;
	for (const vm::Entry &entry : entries.all()) {
		if (vm::any::tag(entry.key) == vm::AnyTag::String) {
			order += std::string(static_cast<vm::Str *>(vm::any::pointer(entry.key))->view()) + " ";
		}
	}
	CHECK(order.find("k0 ") == std::string::npos && order.find("k1 k2 k4 k5 ") == 0);
	CHECK_EQ(order.substr(order.size() - 13), std::string("k37 k38 last "));
	CHECK_EQ(entries.size(), (uint32_t)27);
	const std::optional<uint32_t> found = entries.find_text("k38");
	CHECK(found && vm::any::to_int_payload(entries.value_at(*found)) == 38);
	CHECK(!entries.find_text("k3"));
	vm::Object object;
	entries.add(vm::any::object(&object), vm::any::null());
	CHECK(entries.find_object(&object).has_value());
	CHECK_EQ(entries.take_all().size(), (size_t)28);
	CHECK_EQ(entries.size(), (uint32_t)0);
	for (vm::Str *each : made) {
		vm::free_string(each);
	}
	vm::free_string(last);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// Objects, Dictionaries and an Array's properties across a reload: each key and value carried, the order
// kept, an object key still the same object.
static void test_reload_tables() {
	const char *source = "package { import flash.utils.Dictionary; public class Bag {\n"
						 "  public var o:Object = { b: 1, a: \"two\" }; public var d:Dictionary = new Dictionary(); public var list:Array = [1];\n"
						 "  public var key:Thing = new Thing();\n"
						 "  public function fill():void { d[key] = o; d.x = 5; list.extra = key; o.c = [key]; }\n"
						 "  public function tell():String { var s:String = \"\"; for (var k:String in o) { s += k + o[k] + \",\"; }\n"
						 "    return s + (d[key] == o) + d.x + (list.extra == key) + (o.c[0] == key) + list.length; }\n"
						 "} }\n"
						 "class Thing { }\n";
	ir::Module old_module, new_module;
	compile(source, &old_module, "Bag.as");
	compile(source, &new_module, "Bag.as");
	{
		Recorder recorder;
		auto running = std::make_unique<vm::Interpreter>(old_module, recorder);
		CHECK(running->start());
		vm::Object *bag = running->construct(class_named(old_module, "Bag"), nullptr);
		vm::Slot result;
		CHECK(running->call_method(bag, "fill", nullptr, 0, &result));
		vm::Interpreter fresh(new_module, recorder);
		vm::Object *moved = nullptr;
		{
			vm::Reload reload(*running, fresh);
			moved = reload.object(bag);
			CHECK(moved != nullptr);
			CHECK_EQ(reload.missing(), std::string());
		}
		running->release_object(bag);
		running.reset();
		if (moved) {
			CHECK(fresh.call_method(moved, "tell", nullptr, 0, &result));
			CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("b1,atwo,c[object Thing],true5truetrue1"));
			fresh.discard(ir::Kind::Str, result);
			fresh.release_object(moved);
		}
		CHECK_EQ(recorder.errors, std::string());
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
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

// Code that runs once and calls a method on another object: through the object's declared class, its
// subclasses' overrides, an interface, or a property's getter. What that object's class runs is followed.
static void test_run_once_other_objects() {
	const char *base = "package { public class Game {\n"
					   "  public var helper:Helper = new Helper(); public var thing:Doer = new Thing();\n"
					   "  public function _ready():void { helper.go(); thing.act(); var s:int = helper.size; var m:Maker = new Maker(); }\n"
					   "} }\n"
					   "interface Doer { function act():void; }\n"
					   "class Thing implements Doer { public var done:int; public function act():void { done = 1; } }\n"
					   "class Helper { public var n:int; public function go():void { n = 1; } public function later():void { n = 7; }\n"
					   "  public function get size():int { return 4; } }\n"
					   "class Fancy extends Helper { override public function go():void { n = 5; } }\n"
					   "class Maker { public var k:int = 1; public function Maker() { k = 2; } }\n";
	const auto changes = [&](const char *p_from, const char *p_to) {
		std::string after = base;
		after.replace(after.find(p_from), std::string(p_from).size(), p_to);
		ir::Module before_module, after_module;
		compile(base, &before_module, "Game.as");
		compile(after.c_str(), &after_module, "Game.as");
		std::string out;
		for (const std::string &name : ir::run_once_changes(before_module, after_module)) {
			out += name + ";";
		}
		return out;
	};
	CHECK_EQ(changes("{ n = 1; }", "{ n = 2; }"), std::string("Game;"));
	CHECK_EQ(changes("{ n = 5; }", "{ n = 6; }"), std::string("Game;")); // An override in a subclass may run.
	CHECK_EQ(changes("{ done = 1; }", "{ done = 3; }"), std::string("Game;"));
	CHECK_EQ(changes("return 4;", "return 8;"), std::string("Game;"));
	// An object made there: its constructor, and its fields' first values.
	CHECK_EQ(changes("{ k = 2; }", "{ k = 3; }"), std::string("Game;Maker;")); // Maker too: its own objects ran it.
	CHECK_EQ(changes("k:int = 1", "k:int = 5"), std::string("Game;Maker;"));
	// What the code that runs once does not reach, and a field added before the one it sets, change nothing.
	CHECK_EQ(changes("{ n = 7; }", "{ n = 8; }"), std::string());
	CHECK_EQ(changes("public var n:int;", "public var m:int; public var n:int;"), std::string());
}

// A static field's first value edited: objects alive, and the scene started again, keep the value it had.
static void test_static_changes() {
	const char *base = "package { public class Game { public static var best:int = 1; public static var name:String = pick();\n"
					   "  private static function pick():String { return \"a\"; } public var n:int = 2; } }\n";
	const auto changes = [&](const char *p_from, const char *p_to) {
		std::string after = base;
		after.replace(after.find(p_from), std::string(p_from).size(), p_to);
		ir::Module before_module, after_module;
		compile(base, &before_module, "Game.as");
		compile(after.c_str(), &after_module, "Game.as");
		std::string out;
		for (const std::string &name : ir::static_changes(before_module, after_module)) {
			out += name + ";";
		}
		return out;
	};
	CHECK_EQ(changes("best:int = 1", "best:int = 9"), std::string("Game;"));
	CHECK_EQ(changes("return \"a\";", "return \"b\";"), std::string("Game;"));
	CHECK_EQ(changes("n:int = 2", "n:int = 3"), std::string());
}

// A vector, an Array or an Object held where any value goes, of a type the new build no longer names: it is
// moved all the same, its class added to the new build, empty or not.
static void test_reload_kept_containers() {
	const char *before = "package { public class Bag {\n"
						 "  public var flags:* = new <Boolean>[true, false]; public var none:* = new Vector.<uint>();\n"
						 "  public var rows:* = new [][]int { new []int { 7 } }; public var empty:* = new [1] []String;\n"
						 "  public var list:* = [1, \"two\"]; public var table:* = { a: 1 };\n"
						 "} }\n";
	const char *after = "package { public class Bag {\n"
						"  public var flags:*; public var none:*; public var list:*; public var table:*; public var rows:*; public var empty:*;\n"
						"  public function tell():String { var saved:* = rows[0]; rows[0] = saved;\n"
						"    try { rows[0] = [8]; } catch (e:TypeError) { try { rows.length = 0; } catch (f:RangeError) {\n"
						"      return rows[0][0] + \"|\" + String(flags) + \"|\" + none.length + \"|\" + list + \"|\" + table.a; } } return \"wrong type or length\"; }\n"
						"} }\n";
	ir::Module old_module, new_module, bare_module;
	compile(before, &old_module, "Bag.as");
	compile(after, &new_module, "Bag.as");
	compile(after, &bare_module, "Bag.as");
	{
		Recorder recorder;
		vm::Interpreter running(old_module, recorder);
		CHECK(running.start());
		vm::Object *bag = running.construct(class_named(old_module, "Bag"), nullptr);
		{
			// Without the classes, what can't be moved is named: a reload that sees it is refused.
			vm::Interpreter fresh(bare_module, recorder);
			vm::Reload reload(running, fresh);
			fresh.release_object(reload.object(bag));
			CHECK(reload.missing().rfind("Vector.<", 0) == 0);
		}
		const std::vector<uint8_t> live = vm::Reload::reached(running, { bag });
		vm::Reload::keep_containers(old_module, live, &new_module);
		CHECK(class_named(new_module, "[]String") != ir::NONE);
		vm::Interpreter fresh(new_module, recorder);
		vm::Object *moved = nullptr;
		{
			vm::Reload reload(running, fresh);
			moved = reload.object(bag);
			CHECK(moved != nullptr);
			CHECK_EQ(reload.missing(), std::string());
		}
		running.release_object(bag);
		if (moved) {
			vm::Slot result;
			const bool ok = fresh.call_method(moved, "tell", nullptr, 0, &result);
			CHECK(ok);
			if (ok && result.p) {
				CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("7|true,false|0|1,two|1"));
				fresh.discard(ir::Kind::Str, result);
			}
			fresh.release_object(moved);
		}
		CHECK_EQ(recorder.errors, std::string());
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
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

// A held callback follows its method or closure into the new module even when its index moves.
static void test_reload_callbacks() {
	const std::string head = "package { public class Counter { public var bound:()=>int; public var closure:()=>int; public var legacy:Function; ";
	const std::string body = "public function Counter() { bound = step; closure = make(10); legacy = make(20); } "
			"public function make(n:int):()=>int { return function():int { return ++n; }; } "
			"public function run():int { return bound() + closure() + int(legacy()); } ";
	ir::Module before, after;
	compile((head + body + "public function step():int { return 1; } } }").c_str(), &before);
	compile((head + "public function another():int { return 100; } " + body + "public function step():int { return 2; } } }").c_str(), &after);
	Recorder recorder;
	vm::Interpreter old(before, recorder);
	CHECK(old.start());
	vm::Object *original = old.construct(class_named(before, "Counter"), nullptr);
	vm::Slot answer;
	CHECK(old.call_method(original, "run", nullptr, 0, &answer));
	CHECK_EQ(answer.i, 33);
	vm::Interpreter fresh(after, recorder);
	vm::Reload reload(old, fresh);
	vm::Object *moved = reload.object(original);
	CHECK(moved != nullptr);
	if (moved) {
		CHECK(fresh.call_method(moved, "run", nullptr, 0, &answer));
		CHECK_EQ(answer.i, 36);
		fresh.release_object(moved);
	}
	old.release_object(original);
}

static void test_reload_callback_identity() {
	const std::string head = "package { public class Counter { public var f:Function; public function Counter() { ";
	const std::string tail = "} public function run():int { return f(); } } }";
	const std::string one = "f = function():int { return 1; }; ";
	const std::string two = "f = function():int { return 2; }; ";
	ir::Module before, after;
	compile((head + one + two + tail).c_str(), &before);
	compile((head + two + one + tail).c_str(), &after);
	Recorder recorder;
	vm::Interpreter old(before, recorder);
	CHECK(old.start());
	vm::Object *original = old.construct(class_named(before, "Counter"), nullptr);
	auto migrate = [&](const ir::Module &p_after, bool p_matches) {
		vm::Interpreter fresh(p_after, recorder);
		vm::Reload reload(old, fresh);
		vm::Object *moved = reload.object(original);
		CHECK_EQ(reload.missing(), p_matches ? std::string() : std::string("Function"));
		if (moved && p_matches) {
			vm::Slot answer;
			CHECK(fresh.call_method(moved, "run", nullptr, 0, &answer));
			CHECK_EQ(answer.i, 2);
		}
		fresh.release_object(moved);
	};
	migrate(after, true);
	after = ir::Module();
	compile((head + one + "f = function():int { return 3; }; " + tail).c_str(), &after);
	migrate(after, false); // A new sibling at the old position does not prove identity.
	for (auto &fn : before.functions) {
		if (fn.name.find("/function") != std::string::npos) {
			fn.code = { { ir::Op::Native } };
		}
	}
	migrate(after, false); // Native stubs have no comparable bodies.
	for (auto &fn : before.functions) {
		fn.file = ir::NONE;
	}
	migrate(after, false); // A synthetic function may carry no source file.
	old.release_object(original);
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

// A call through an interface finds the method in the object's class's table. A class whose table lacks it
// (a module the lowerer did not make) is AS3's error for a missing method, never a read past the table.
static void test_interface_table() {
	Compilation compilation;
	compilation.add("Main.as", "package { public class Main { public function Main() {\n"
							   "	var s:Shape = new Square();\n"
							   "	trace(s.area());\n"
							   "	var f:Function = s.area;\n"
							   "} } }\n"
							   "interface Shape { function area():Number; }\n"
							   "class Square implements Shape { public function area():Number { return 4; } }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module));
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK(interpreter.run_program());
		CHECK_EQ(host.printed, std::string("4\n"));
	}
	for (ir::Class &klass : module.classes) {
		klass.interface_methods.clear();
	}
	const std::string missing = "TypeError: Error #1006: area is not a function.";
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK(!interpreter.run_program());
		CHECK_EQ(host.errors.substr(0, host.errors.find('\n')), missing);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	// The method taken as a value, the same.
	for (ir::Function &function : module.functions) {
		for (ir::Instruction &instruction : function.code) {
			if (instruction.op == ir::Op::CallInterface) {
				instruction.op = ir::Op::Stmt;
			}
		}
	}
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK(!interpreter.run_program());
		CHECK_EQ(host.errors.substr(0, host.errors.find('\n')), missing);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
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

// Objects, Dictionaries and an Array's properties with no memory left: Error #1000, caught, and nothing
// left alive.
static void test_tables_out_of_memory() {
	Compilation compilation;
	compilation.add("Tables.as", "package { public class Tables {\n"
								 "public function make():String { try { var o:Object = {}; return 'made'; } catch (e:Error) { return e.message; } return ''; }\n"
								 "public function key(o:*):String { try { o['fresh'] = 1; return 'set'; } catch (e:Error) { return e.message; } return ''; }\n"
								 "public function extra(a:*):String { try { a.extra = 1; return 'set'; } catch (e:Error) { return e.message; } return ''; }\n"
								 "public function keys(o:*):String { var n:int = 0; try { for (var k:* in o) { n++; } return 'went'; } catch (e:Error) { return e.message; } return ''; }\n"
								 "public function table():* { return { a: 1 }; }\n"
								 "public function list():* { return [1]; }\n"
								 "} }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	const size_t strings = vm::live_strings();
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK(interpreter.start());
		vm::Object *object = interpreter.construct(class_named(module, "Tables"));
		CHECK(object != nullptr);
		vm::Slot table, list, result;
		CHECK(interpreter.call_method(object, "table", nullptr, 0, &table));
		CHECK(interpreter.call_method(object, "list", nullptr, 0, &list));
		const auto failing = [&](const char *p_method, vm::Slot *p_argument) {
			vm::fail_allocations(1);
			const bool called = interpreter.call_method(object, p_method, p_argument, p_argument ? 1 : 0, &result);
			vm::fail_allocations(0);
			const std::string said = called && result.p ? std::string(static_cast<vm::Str *>(result.p)->view()) : std::string("failed");
			interpreter.discard(ir::Kind::Str, result);
			return said;
		};
		CHECK_EQ(failing("make", nullptr), std::string(vm::OUT_OF_MEMORY));
		CHECK_EQ(failing("key", &table), std::string(vm::OUT_OF_MEMORY));
		CHECK_EQ(failing("extra", &list), std::string(vm::OUT_OF_MEMORY));
		CHECK_EQ(failing("keys", &table), std::string(vm::OUT_OF_MEMORY));
		CHECK_EQ(failing("keys", &list), std::string(vm::OUT_OF_MEMORY)); // An index's text, as the loop gives it.
		interpreter.discard(ir::Kind::Any, table);
		interpreter.discard(ir::Kind::Any, list);
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

static const char *const CYCLES = "package {\n"
								  "import flash.utils.Dictionary;\n"
								  "public class Cycles {\n"
								  "	public static var kept:Link;\n"
								  "	public function pair():void { var a:Link = new Link(); var b:Link = new Link(); a.next = b; b.next = a; }\n"
								  "	public function three():void { var a:Link = new Link(); a.next = new Link(); a.next.next = new Link(); a.next.next.next = a; }\n"
								  "	public function chain(n:int):void {\n"
								  "		var first:Link = new Link(); var at:Link = first;\n"
								  "		for (var i:int = 1; i < n; i++) { at.next = new Link(); at = at.next; }\n"
								  "		at.next = first;\n"
								  "	}\n"
								  "	public function throughVector():void { var v:Vector.<Link> = new Vector.<Link>(); var a:Link = new Link(); a.any = v; v.push(a); }\n"
								  "	public function throughUntypedVector():void { var v:Vector.<*> = new Vector.<*>(); v.push(v); }\n"
								  "	public function throughMethod():void { var a:Link = new Link(); a.any = a.hop; }\n"
								  "	public function selfObject():void { var o:Object = { name: \"me\" }; o.self = o; }\n"
								  "	public function keyedDictionary():void { var d:Dictionary = new Dictionary(); var a:Link = new Link(); a.any = d; d[a] = 1; }\n"
								  "	public function valueDictionary():void { var d:Dictionary = new Dictionary(); d.x = { back: d }; }\n"
								  "	public function arrayProperty():void { var list:Array = [1]; list.me = list; }\n"
								  "	public function untypedPair():void { var a:Link = new Link(); var b:Link = new Link(); a.any = b; b.any = a; }\n"
								  "	public function recursive():int {\n"
								  "		var down:Function = function (n:int):int { return n <= 0 ? 0 : 1 + down(n - 1); };\n"
								  "		return down(3);\n"
								  "	}\n"
								  "	public function keep():void { var a:Link = new Link(); a.next = new Link(); a.next.next = a; kept = a; }\n"
								  "	public function forget():void { kept = null; }\n"
								  "	public function held():Link { var a:Link = new Link(); a.next = new Link(); a.next.next = a; return a; }\n"
								  "	public function onStack():Boolean {\n"
								  "		var a:Link = new Link(); a.next = new Link(); a.next.next = a;\n"
								  "		pair();\n"
								  "		checkpoint();\n"
								  "		return a.next.next == a;\n"
								  "	}\n"
								  "	public function checkpoint():void { var n:int = 0; n++; }\n"
								  "}\n"
								  "}\n"
								  "class Link { public var next:Link; public var any:*; public function hop():int { return 1; } }\n";

static void compile_cycles(ir::Module *r_module, const char *p_source, const char *p_file = "Cycles.as") {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add(p_file, p_source);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	CHECK(ir::lower(compilation, r_module, ir::LowerMode::Library));
}

// The collector run at a statement of checkpoint(), with frames on the stack: what was alive after it.
static bool collect_at_checkpoint(vm::Interpreter &p_interpreter, void *p_data) {
	if (p_interpreter.function_at(p_interpreter.depth() - 1).name == "Cycles/checkpoint") {
		p_interpreter.collect();
		*static_cast<size_t *>(p_data) = vm::live_objects();
	}
	return true;
}

// Objects that refer to each other in a loop, and nothing else to them, are freed by the cycle collector;
// a loop something still holds is not.
static void test_cycles() {
	ir::Module module;
	compile_cycles(&module, CYCLES);
	{
		Recorder recorder;
		vm::Interpreter interpreter(module, recorder);
		CHECK(interpreter.start());
		vm::Object *cycles = interpreter.construct(class_named(module, "Cycles"));
		const size_t alone = vm::live_objects();
		auto call = [&](const char *p_name, const vm::Slot *p_arguments = nullptr, size_t p_count = 0) {
			vm::Slot result;
			result.bits = 0;
			CHECK(interpreter.call_method(cycles, p_name, p_arguments, p_count, &result));
			return result;
		};
		auto alive = [&](const char *p_name) { return std::string(p_name) + " leaves " + std::to_string(vm::live_objects() - alone); };
		for (const char *name : { "pair", "three", "throughVector", "throughUntypedVector", "throughMethod", "untypedPair", "recursive", "selfObject",
				 "keyedDictionary", "valueDictionary", "arrayProperty" }) {
			call(name);
			CHECK(vm::live_objects() > alone);
			interpreter.collect();
			CHECK_EQ(alive(name), std::string(name) + " leaves 0");
		}
		// A long loop is freed in steps of bounded work, not in one look.
		vm::Slot length;
		length.bits = 0;
		length.i = 10000;
		call("chain", &length, 1);
		CHECK_EQ(alive("chain"), std::string("chain leaves 10000"));
		int steps = 1;
		while (!interpreter.collect_step(500) && steps < 100000) {
			steps++;
		}
		CHECK(steps > 20);
		CHECK_EQ(alive("chain"), std::string("chain leaves 0"));
		// A loop a static field holds stays, until it is let go.
		call("keep");
		interpreter.collect();
		CHECK_EQ(alive("keep"), std::string("keep leaves 2"));
		call("forget");
		interpreter.collect();
		CHECK_EQ(alive("forget"), std::string("forget leaves 0"));
		// So does one the host holds.
		vm::Slot held = call("held");
		interpreter.collect();
		CHECK_EQ(alive("held"), std::string("held leaves 2"));
		interpreter.discard(ir::Kind::Obj, held);
		interpreter.collect();
		CHECK_EQ(alive("discard"), std::string("discard leaves 0"));
		// A loop a register of a running function holds stays while it runs; another, made and let go
		// there, is freed meanwhile.
		size_t during = 0;
		interpreter.set_statement_hook(collect_at_checkpoint, &during);
		CHECK(call("onStack").b);
		interpreter.set_statement_hook(nullptr, nullptr);
		CHECK_EQ(during - alone, (size_t)2);
		interpreter.collect();
		CHECK_EQ(alive("onStack"), std::string("onStack leaves 0"));
		// The program goes on changing what refers to what while a collection is under way.
		for (int round = 0; round < 200; round++) {
			call(round % 3 == 0 ? "pair" : round % 3 == 1 ? "keep" : "forget");
			length.i = round;
			call("chain", &length, 1);
			interpreter.collect_step(37);
		}
		call("forget");
		interpreter.collect();
		CHECK_EQ(alive("while running"), std::string("while running leaves 0"));
		interpreter.release_object(cycles);
		// What is left in a loop when the interpreter goes is freed with it.
		cycles = interpreter.construct(class_named(module, "Cycles"));
		call("pair");
		call("keep");
		interpreter.release_object(cycles);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// What the engine gives for a request may be the very handle the result's register holds, held once more
// (a host that shares a handle on an object where it could copy it, as this one does): the register lets
// go of what it held all the same, so that every hold is given back.
static void test_engine_result_is_the_registers_own() {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Again.as", "package { import godot.*; public class Again extends Node {\n"
								"public function again(node:Node2D):Boolean {\n"
								"  var held:Node = node;\n"
								"  for (var i:int = 0; i < 3; i++) { held = held as Node2D; }\n"
								"  return held != null;\n"
								"} } }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		vm::Object *again = interpreter.construct(class_named(module, "Again"), engine.box());
		vm::Slot node;
		node.bits = 0;
		node.p = engine.box();
		const int before = engine.boxes;
		vm::Slot result;
		CHECK(interpreter.call_method(again, "again", &node, 1, &result));
		CHECK(result.b);
		CHECK_EQ(engine.boxes, before);
		engine.boxes--; // The node's own.
		interpreter.release_object(again);
		CHECK_EQ(engine.boxes, 0);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// A vector of Godot's objects of a class that is never reference-counted (nodes) holds nothing that leads
// back to a script object: the cycle collector does not look at it, however long it is. One whose elements
// may be reference-counted (a RefCounted, or Godot's Object, which any object is) is looked at as ever.
static void test_collector_leaves_vectors_of_nodes() {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Herd.as", "package { import godot.*; public class Herd extends Node {\n"
							   "public var nodes:Vector.<Node2D> = new Vector.<Node2D>();\n"
							   "public var counted:Vector.<RefCounted> = new Vector.<RefCounted>();\n"
							   "public var any:Vector.<GodotObject> = new Vector.<GodotObject>();\n"
							   "public function fill(many:int, few:int):void {\n"
							   "  for (var i:int = 0; i < many; i++) { nodes.push(new Node2D()); }\n"
							   "  for (var j:int = 0; j < few; j++) { counted.push(new RefCounted()); any.push(new Node2D()); }\n"
							   "} } }");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK(!module.classes[class_named(module, "Vector.<Node2D>")].element_counted);
	CHECK(module.classes[class_named(module, "Vector.<RefCounted>")].element_counted);
	CHECK(module.classes[class_named(module, "Vector.<GodotObject>")].element_counted);
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		vm::Object *herd = interpreter.construct(class_named(module, "Herd"), engine.box());
		vm::Slot counts[2];
		counts[0].bits = counts[1].bits = 0;
		counts[0].i = 100;
		counts[1].i = 2;
		CHECK(interpreter.call_method(herd, "fill", counts, 2, nullptr));
		engine.handles_asked = 0;
		interpreter.collect();
		// The script's own Godot object, and the two elements of each of the two vectors that are looked at.
		CHECK(engine.handles_asked > 0);
		CHECK(engine.handles_asked < 100);
		interpreter.release_object(herd);
		CHECK_EQ(engine.boxes, 0);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// A vector of objects of a class that holds no object, like every class that extends it, holds nothing
// that leads back to it: the cycle collector does not look at it, however long it is (a frame of a
// game that keeps twenty thousand such objects in a vector does not walk them). A vector whose elements
// may hold an object is looked at as ever: one of a class that holds one, of a class extended by one that
// does, of an interface, of vectors that are looked at. A loop through such a vector is still freed.
static void test_collector_leaves_vectors_of_plain_objects() {
	const char *source = "package { public class Pool {\n"
						 "public var dots:Vector.<Dot> = new Vector.<Dot>();\n"
						 "public var rows:Vector.<Vector.<Dot>> = new Vector.<Vector.<Dot>>();\n"
						 "public var links:Vector.<Link> = new Vector.<Link>();\n"
						 "public var sorts:Vector.<Sort> = new Vector.<Sort>();\n"
						 "public var named:Vector.<Named> = new Vector.<Named>();\n"
						 "public var chains:Vector.<Vector.<Link>> = new Vector.<Vector.<Link>>();\n"
						 "public function fill(many:int):void {\n"
						 "  for (var i:int = 0; i < many; i++) { dots.push(new Dot()); }\n"
						 "  rows.push(dots); links.push(new Link()); named.push(new Dot()); chains.push(links);\n"
						 "}\n"
						 "public function tie():void { var knot:Knot = new Knot(); knot.back = sorts; sorts.push(knot); sorts = new Vector.<Sort>(); }\n"
						 "} }\n"
						 "interface Named {}\n"
						 "class Dot implements Named { public var x:Number = 1; public var on:Boolean; }\n"
						 "class Link { public var next:Link; }\n"
						 "class Sort { public var n:int; }\n"
						 "class Knot extends Sort { public var back:Vector.<Sort>; }\n";
	ir::Module module;
	compile_cycles(&module, source, "Pool.as");
	{
		Recorder recorder;
		vm::Interpreter interpreter(module, recorder);
		CHECK(interpreter.start());
		const auto looked_at = [&](const char *p_class) { return interpreter.heap().list_for(class_named(module, p_class)) != nullptr; };
		CHECK(!looked_at("Vector.<Dot>"));
		CHECK(!looked_at("Vector.<Vector.<Dot>>"));
		CHECK(looked_at("Vector.<Link>"));
		CHECK(looked_at("Vector.<Sort>")); // A Knot is a Sort, and holds an object.
		CHECK(looked_at("Vector.<Named>")); // Whatever implements it may.
		CHECK(looked_at("Vector.<Vector.<Link>>"));
		vm::Object *pool = interpreter.construct(class_named(module, "Pool"));
		vm::Slot many;
		many.bits = 0;
		many.i = 1000;
		CHECK(interpreter.call_method(pool, "fill", &many, 1, nullptr));
		const size_t filled = vm::live_objects();
		// A vector of Sorts that a Knot inside it holds, and nothing else does.
		CHECK(interpreter.call_method(pool, "tie", nullptr, 0, nullptr));
		CHECK_EQ(vm::live_objects(), filled + 2);
		interpreter.collect();
		CHECK_EQ(vm::live_objects(), filled);
		interpreter.release_object(pool);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// A loop that passes through the engine: a script object holds one of its objects, which holds the script
// object back (as a Godot object holds a listener that is a method of it). The engine is asked to let go
// of what it holds; if something else holds a copy, everything stays.
static void test_cycles_through_engine() {
	const char *source = "package { import godot.*;\n"
						 "public class Holder { public var res:Resource;\n"
						 "	public function make():void { res = new Resource(); }\n"
						 "	public function makeTwice():void { res = new Resource(); res = new Resource(); } }\n"
						 "}\n";
	ir::Module module;
	compile_cycles(&module, source, "Holder.as");
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		engine.running = &interpreter;
		CHECK(interpreter.start());
		const size_t before = vm::live_objects();
		for (const char *make : { "make", "makeTwice" }) {
			vm::Object *holder = interpreter.construct(class_named(module, "Holder"));
			vm::Slot ignored;
			CHECK(interpreter.call_method(holder, make, nullptr, 0, &ignored));
			// The last box is the engine's object; node 7 stands for it, and holds the script object.
			engine.node_boxes[engine.next - 1] = 7;
			engine.nodes[7].objects.push_back(holder);
			vm::retain(holder); // The listener's reference.
			engine.nodes[7].survives = true;
			engine.log.clear();
			// Still held by the host: nothing is asked of the engine.
			interpreter.collect();
			CHECK_EQ(engine.log, std::string());
			// Held only through the engine, but a copy of the listener is held elsewhere: it stays.
			interpreter.release_object(holder);
			interpreter.collect();
			CHECK_EQ(engine.log, std::string("let go of node 7\nput back\n"));
			CHECK_EQ(vm::live_objects(), before + 1);
			// Once nothing else holds the copy, the loop goes, and so does the engine's object: the second
			// time in the smallest steps there are, so that the node is asked about again in each.
			engine.nodes[7].survives = false;
			engine.log.clear();
			if (make == std::string("make")) {
				interpreter.collect();
			} else {
				int steps = 1;
				while (!interpreter.collect_step(1) && steps < 1000) {
					steps++;
				}
				CHECK(steps > 2);
			}
			CHECK_EQ(engine.log, std::string("let go of node 7\n"));
			CHECK_EQ(vm::live_objects(), before);
			CHECK_EQ(engine.boxes, 0);
			engine.node_boxes.clear();
		}
		// Once another interpreter has taken over (a reload), the engine is asked nothing more, even about
		// a node a collection under way has seen; what such a node held stays.
		vm::Object *holder = interpreter.construct(class_named(module, "Holder"));
		vm::Slot ignored;
		CHECK(interpreter.call_method(holder, "make", nullptr, 0, &ignored));
		engine.node_boxes[engine.next - 1] = 8;
		engine.nodes[8].objects.push_back(holder);
		vm::retain(holder);
		interpreter.release_object(holder);
		engine.asked = 0;
		for (int steps = 0; engine.asked == 0 && steps < 1000; steps++) {
			interpreter.collect_step(1);
		}
		CHECK(engine.asked > 0);
		const int asked = engine.asked;
		interpreter.superseded();
		interpreter.collect();
		CHECK_EQ(engine.asked, asked);
		CHECK_EQ(vm::live_objects(), before + 1);
		engine.log.clear();
		CHECK(engine.nodes.count(8) == 1);
		// The engine lets go of its listener: then nothing holds the object, and it goes.
		engine.nodes.erase(8);
		interpreter.release_object(holder);
		CHECK_EQ(vm::live_objects(), before);
		CHECK_EQ(engine.boxes, 0);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// A script object of a reference-counted class in a loop with a plain object. Its Godot object (node 9)
// holds no reference to it but keeps it (joined), and its own handle on that Godot object counts as one of
// the node's references. The loop stays whole while the engine or the host holds either half, and goes
// when neither does.
static void test_counted_cycles() {
	const char *source = "package { import godot.*;\n"
						 "public class Paired extends RefCounted { public var label:String = \"kept\"; public var partner:Partner;\n"
						 "	public function pair():void { partner = new Partner(); partner.back = this; }\n"
						 "	public function word():String { return partner && partner.back == this ? label : \"emptied\"; } }\n"
						 "}\n"
						 "class Partner { public var back:Paired; }\n";
	ir::Module module;
	compile_cycles(&module, source, "Paired.as");
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		engine.running = &interpreter;
		CHECK(interpreter.start());
		const size_t before = vm::live_objects();
		vm::Object *paired = interpreter.construct(class_named(module, "Paired"), engine.box());
		vm::Slot ignored;
		CHECK(interpreter.call_method(paired, "pair", nullptr, 0, &ignored));
		engine.node_boxes[engine.next - 1] = 9;
		engine.nodes[9].exact = true;
		engine.nodes[9].joined.push_back(paired);
		auto word = [&]() {
			vm::Slot result;
			CHECK(interpreter.call_method(paired, "word", nullptr, 0, &result));
			std::string text(static_cast<vm::Str *>(result.p)->view());
			interpreter.discard(ir::Kind::Str, result);
			return text;
		};
		// The host holds the script object; the engine holds nothing more than its handle.
		engine.nodes[9].references = 1;
		interpreter.collect();
		CHECK_EQ(vm::live_objects(), before + 2);
		CHECK_EQ(word(), std::string("kept"));
		// The engine holds the Godot object (as GDScript would), and nothing else holds either half.
		engine.nodes[9].references = 2;
		engine.keeps = true;
		interpreter.release_object(paired);
		interpreter.collect();
		CHECK_EQ(vm::live_objects(), before + 2);
		vm::retain(paired);
		CHECK_EQ(word(), std::string("kept"));
		interpreter.release_object(paired);
		// The engine lets go too: the loop goes, and its handle with it.
		engine.nodes[9].references = 1;
		engine.keeps = false;
		engine.log.clear();
		interpreter.collect();
		CHECK_EQ(engine.log, std::string("keep Paired\n"));
		CHECK_EQ(vm::live_objects(), before);
		CHECK_EQ(engine.boxes, 0);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// An error in code the engine ran for an instruction (a toString() Godot called for a value's text, a
// listener that ran in an emit) comes out of that instruction, where a catch around it catches it; Error
// #1502 does not, whatever runs it.
static void test_carried_errors() {
	const char *source = "package { import godot.*;\n"
						 "public class Reentry extends Node {\n"
						 "	public function boom():String { throw new Error(\"boom\"); }\n"
						 "	public function spin():void { for (;;) {} }\n"
						 "	public function textOf(u:*):String { try { return \"\" + u; } catch (e:Error) { return \"caught \" + e.message; } return \"none\"; }\n"
						 "	public function joined(u:*):String { try { return [u, u].join(); } catch (e:Error) { return \"caught \" + e.message; } return \"none\"; }\n"
						 "	public function called():String { try { setPhysicsProcess(true); } catch (e:*) { return \"caught\"; } return \"out\"; }\n"
						 "	public function keyed(u:*):String { var o:Object = {}; try { o[u] = 1; } catch (e:Error) { var n:int = 0; for (var k:* in o) { n++; } return \"caught \" + e.message + \" \" + n; } return \"none\"; }\n"
						 "	public static var noted:int;\n"
						 "	public function note():void { noted++; }\n"
						 "	public function notes():int { return noted; }\n"
						 "} }\n";
	ir::Module module;
	compile_cycles(&module, source, "Reentry.as");
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		vm::Object *reentry = interpreter.construct(class_named(module, "Reentry"), engine.box());
		std::vector<const char *> again = { "boom" };
		engine.calls_back = [&]() {
			for (const char *method : again) {
				vm::Slot ignored;
				ignored.bits = 0;
				if (interpreter.call_method(reentry, method, nullptr, 0, &ignored)) {
					interpreter.discard(ir::Kind::Str, ignored);
				}
			}
		};
		auto text_of = [&](const char *p_method) {
			engine.errors.clear();
			engine.next = (engine.next + 15) / 16 * 16; // An untyped value holds a handle 16-aligned.
			vm::Slot argument = vm::any::godot_object(engine.box());
			vm::Slot result;
			result.bits = 0;
			const bool ok = interpreter.call_method(reentry, p_method, &argument, 1, &result);
			interpreter.discard(ir::Kind::Any, argument);
			std::string text = ok && result.p ? std::string(static_cast<vm::Str *>(result.p)->view()) : "failed";
			if (ok) {
				interpreter.discard(ir::Kind::Str, result);
			}
			return text;
		};
		CHECK_EQ(text_of("textOf"), std::string("caught boom"));
		CHECK_EQ(engine.errors, std::string());
		CHECK_EQ(text_of("joined"), std::string("caught boom"));
		// The second toString() is not asked for: the first one's error ends the join.
		CHECK_EQ(engine.errors, std::string());
		// Nor is a key written under Godot's own text for the value.
		CHECK_EQ(text_of("keyed"), std::string("caught boom 0"));
		CHECK_EQ(engine.errors, std::string());
		// What else the engine runs before it answers runs: the first error is the instruction's, a later
		// one is reported where it happened.
		again = { "boom", "note", "boom" };
		engine.errors.clear();
		vm::Slot said;
		said.bits = 0;
		CHECK(interpreter.call_method(reentry, "called", nullptr, 0, &said));
		CHECK_EQ(std::string(static_cast<vm::Str *>(said.p)->view()), std::string("caught"));
		interpreter.discard(ir::Kind::Str, said);
		CHECK(interpreter.call_method(reentry, "notes", nullptr, 0, &said));
		CHECK_EQ(said.i, 1);
		CHECK(engine.errors.find("Error: boom") == 0 && engine.errors.find("Error: boom", 1) == std::string::npos);
		// Flash's timeout in a listener: it comes out of the call that ran it, and no catch stops it.
		again = { "spin" };
		interpreter.set_time_limit(30);
		engine.errors.clear();
		vm::Slot result;
		result.bits = 0;
		CHECK(!interpreter.call_method(reentry, "called", nullptr, 0, &result));
		CHECK_EQ(engine.errors.substr(0, engine.errors.find('\n')),
				std::string("Error: Error #1502: A script has executed for longer than the timeout period of 0.03 seconds."));
		CHECK(engine.errors.find("at Reentry/spin()") != std::string::npos);
		CHECK(engine.errors.find("at Reentry/called()") != std::string::npos);
		interpreter.release_object(reentry);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// An emit with two listeners, the first of which throws. The error carried for the emit is that call's
// alone: the second listener's own engine calls (a Godot value's text, a call whose listener throws in
// turn) are judged by what they did, and an error of the second's is reported where it happened.
static void test_carried_errors_per_call() {
	const char *source = "package { import godot.*;\n"
						 "public class Listeners extends Node {\n"
						 "	public var held:*;\n"
						 "	public var heard:String = \"none\";\n"
						 "	public var keys:int = -1;\n"
						 "	public function keep(u:*):void { held = u; }\n"
						 "	public function boom():void { throw new Error(\"boom\"); }\n"
						 "	public function second():void { throw new Error(\"second\"); }\n"
						 "	public function speak():void { heard = \"t:\" + held; var o:Object = {}; o[held] = 1; var n:int = 0; for (var k:* in o) { n++; } keys = n; }\n"
						 "	public function relay():void { setPhysicsProcess(false); heard = \"relayed\"; }\n"
						 "	public function emit():String { try { setPhysicsProcess(true); } catch (e:Error) { return \"caught \" + e.message; } return \"out\"; }\n"
						 "	public function said():String { return heard + \" \" + keys; }\n"
						 "} }\n";
	ir::Module module;
	compile_cycles(&module, source, "Listeners.as");
	{
		FakeEngine engine;
		engine.module = &module;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		vm::Object *listeners = interpreter.construct(class_named(module, "Listeners"), engine.box());
		engine.next = (engine.next + 15) / 16 * 16; // An untyped value holds a handle 16-aligned.
		vm::Slot node = vm::any::godot_object(engine.box());
		vm::Slot ignored;
		ignored.bits = 0;
		CHECK(interpreter.call_method(listeners, "keep", &node, 1, &ignored));
		interpreter.discard(ir::Kind::Any, node);
		// What the engine runs at each depth of its own calls: the listeners of an emit, and those of an emit
		// one of them makes. Deeper than the plan, it runs nothing (a plain Node's text).
		std::vector<std::vector<const char *>> plan;
		size_t depth = 0;
		engine.calls_back = [&]() {
			if (depth >= plan.size()) {
				return;
			}
			const std::vector<const char *> methods = plan[depth];
			depth++;
			for (const char *method : methods) {
				vm::Slot result;
				result.bits = 0;
				(void)interpreter.call_method(listeners, method, nullptr, 0, &result);
			}
			depth--;
		};
		auto call_text = [&](const char *p_method) {
			vm::Slot result;
			result.bits = 0;
			const bool ok = interpreter.call_method(listeners, p_method, nullptr, 0, &result);
			std::string text = ok && result.p ? std::string(static_cast<vm::Str *>(result.p)->view()) : "failed";
			if (ok) {
				interpreter.discard(ir::Kind::Str, result);
			}
			return text;
		};
		// The second listener puts the node into words, and keys an Object by it: both work.
		plan = { { "boom", "speak" } };
		CHECK_EQ(call_text("emit"), std::string("caught boom"));
		CHECK_EQ(engine.errors, std::string());
		plan.clear();
		CHECK(call_text("said").find("t:<#") == 0);
		CHECK(call_text("said").find(" 1") != std::string::npos);
		// The second listener emits in turn, and that emit's listener throws: the error comes out of the
		// second listener's call, which ends there and is reported; the first is still the outer emit's.
		plan = { { "boom", "relay" }, { "second" } };
		CHECK_EQ(call_text("emit"), std::string("caught boom"));
		CHECK_EQ(engine.errors.substr(0, engine.errors.find('\n')), std::string("Error: second"));
		CHECK(engine.errors.find("boom") == std::string::npos);
		CHECK(engine.errors.find("at Listeners/relay()") != std::string::npos);
		plan.clear();
		CHECK(call_text("said").find("relayed") == std::string::npos);
		interpreter.release_object(listeners);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
	CHECK_EQ(vm::live_strings(), (size_t)0);
}

// A script object of a class that extends Godot's is written by its own public fields, as any other: the
// Godot object it is attached to is not one of them. (One of Godot's values is refused: tests/godot_project.)
static void test_json_script_object() {
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Writer.as", "package { import godot.*; public class Writer extends Node2D {\n"
								 "  public var steps:int = 2;\n"
								 "  private var secret:int = 3;\n"
								 "  public function get twice():int { return steps * 2; }\n"
								 "  public function write():String { return JSON.stringify(this); }\n"
								 "} }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	{
		FakeEngine engine;
		vm::Interpreter interpreter(module, engine);
		CHECK(interpreter.start());
		vm::Object *object = interpreter.construct(class_named(module, "Writer"), engine.box());
		CHECK(object != nullptr);
		engine.log.clear();
		vm::Slot result;
		CHECK(interpreter.call_method(object, "write", nullptr, 0, &result));
		CHECK_EQ(std::string(static_cast<vm::Str *>(result.p)->view()), std::string("{\"steps\":2,\"twice\":4}"));
		CHECK_EQ(engine.log, std::string()); // Nothing is asked of the engine.
		interpreter.discard(ir::Kind::Str, result);
		CHECK_EQ(engine.errors, std::string());
		interpreter.release_object(object);
	}
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

// Memory that runs out while JSON makes a value or its text is AS3's Error #1000, and nothing is left.
static void test_json_out_of_memory() {
	Compilation compilation;
	compilation.add("Texts.as", "package { public class Texts {\n"
								"public function read(t:String):String { try { JSON.parse(t); return 'read'; } catch (e:Error) { return e.message; } return ''; }\n"
								"public function write(o:*):String { try { JSON.stringify(o); return 'written'; } catch (e:Error) { return e.message; } return ''; }\n"
								"public function table():* { return {a: [1, 'b']}; }\n"
								"} }\n");
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(false), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	const size_t strings = vm::live_strings();
	{
		Recorder host;
		vm::Interpreter interpreter(module, host);
		CHECK(interpreter.start());
		vm::Object *object = interpreter.construct(class_named(module, "Texts"));
		CHECK(object != nullptr);
		const auto failing = [&](const char *p_method, vm::Slot p_argument, uint32_t p_allocations) {
			vm::Slot result;
			vm::fail_allocations(p_allocations);
			const bool called = interpreter.call_method(object, p_method, &p_argument, 1, &result);
			vm::fail_allocations(0);
			const std::string said = called && result.p ? std::string(static_cast<vm::Str *>(result.p)->view()) : std::string("failed");
			interpreter.discard(ir::Kind::Str, result);
			return said;
		};
		const auto read = [&](const char *p_text, uint32_t p_allocations) {
			vm::Slot text;
			text.bits = 0;
			text.p = vm::new_string(p_text);
			const std::string said = failing("read", text, p_allocations);
			interpreter.discard(ir::Kind::Str, text);
			return said;
		};
		// The first thing each makes: the Object, the Array, the String, the Int64 the number is held in.
		CHECK_EQ(read("{\"a\":[1]}", 1), std::string(vm::OUT_OF_MEMORY));
		CHECK_EQ(read("[{}]", 1), std::string(vm::OUT_OF_MEMORY));
		CHECK_EQ(read("\"text\"", 1), std::string(vm::OUT_OF_MEMORY));
		CHECK_EQ(read("12345678901", 1), std::string(vm::OUT_OF_MEMORY));
		CHECK_EQ(read("1", 1), std::string("read"));
		CHECK_EQ(read("{\"a\":[1, \"b\"]}", 0), std::string("read"));
		vm::Slot table;
		CHECK(interpreter.call_method(object, "table", nullptr, 0, &table));
		CHECK_EQ(failing("write", table, 1), std::string(vm::OUT_OF_MEMORY)); // The text, at the end.
		CHECK_EQ(failing("write", table, 0), std::string("written"));
		interpreter.discard(ir::Kind::Any, table);
		interpreter.release_object(object);
	}
	CHECK_EQ(vm::live_strings(), strings);
	CHECK_EQ(vm::live_objects(), (size_t)0);
}

int main() {
	test_cycles();
	test_cycles_through_engine();
	test_counted_cycles();
	test_carried_errors();
	test_carried_errors_per_call();
	test_host_construct_order();
	test_interface_table();
	test_kept_traces();
	test_untyped_out_of_memory();
	test_tables_out_of_memory();
	test_out_of_memory();
	test_reload();
	test_reload_vectors();
	test_reload_callbacks();
	test_reload_callback_identity();
	test_entries();
	test_reload_tables();
	test_run_once_changes();
	test_run_once_overrides();
	test_run_once_other_objects();
	test_static_changes();
	test_reload_kept_containers();
	test_reload_alive();
	test_reload_constants("", "Game");
	test_reload_constants("com.example.config", "com.example.config.Game"); // A class in a package: its fields are named with it.
	test_reload_in_place();
	test_reload_in_place_values();
	test_reload_in_place_refused();
	test_reload_in_place_memory();
	test_reload_from_native();
	test_reload_from_native_that_tells();
	test_reload_in_place_kept();
	test_reload_in_place_turns();
	test_reload_in_place_early();
	test_reload_engine_class();
	test_failed_static_initializer();
	test_loop_markers();
	test_engine_calls();
	test_engine_prepared();
	test_vector_of_values();
	test_engine_operators();
	test_kept_by_the_engine();
	test_script_object_arrives();
	test_collector_leaves_vectors_of_nodes();
	test_collector_leaves_vectors_of_plain_objects();
	test_engine_result_is_the_registers_own();
	test_no_engine();
	test_engine_listeners();
	test_engine_statics();
	test_entry_points();
	test_time_limit();
	test_statement_hook();
	test_refused_program();
	test_host_clock();
	test_json_script_object();
	test_json_out_of_memory();
	return check::finish();
}
