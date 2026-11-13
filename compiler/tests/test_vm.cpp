// The interpreter as a library: what a debugger will use, and what the host is told.
#include "check.h"

#include "builtins.h"
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
struct FakeEngine : vm::Host {
	std::string log, errors;
	int boxes = 0; // Handed out and not yet dropped.
	int next = 1;
	bool fail_calls = false;
	bool fail_sets = false;
	std::string name = "Player";

	static std::string show(const vm::Host::Argument &p_argument) {
		switch (p_argument.kind) {
			case ir::Kind::F64:
				return vm::number_to_string(p_argument.slot.d);
			case ir::Kind::I32:
				return std::to_string(p_argument.slot.i);
			case ir::Kind::Bool:
				return p_argument.slot.b ? "true" : "false";
			case ir::Kind::Str:
				return p_argument.slot.p ? "\"" + std::string(static_cast<vm::Str *>(p_argument.slot.p)->view()) + "\"" : "null";
			default:
				return "#" + std::to_string((intptr_t)p_argument.slot.p);
		}
	}
	static std::string list(const Argument *p_arguments, size_t p_count) {
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
	void *construct(std::string_view p_type, const Argument *p_arguments, size_t p_count, std::string *) override {
		void *made = box();
		log += "construct " + std::string(p_type) + "(" + list(p_arguments, p_count) + ") -> #" + std::to_string((intptr_t)made) + "\n";
		return made;
	}
	bool call(void *p_receiver, std::string_view p_method, const Argument *p_arguments, size_t p_count, const Wanted &p_wanted,
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
	bool get(void *p_receiver, std::string_view p_property, const Wanted &p_wanted, vm::Slot *r_result, std::string *) override {
		log += "get #" + std::to_string((intptr_t)p_receiver) + "." + std::string(p_property) + "\n";
		give(p_wanted.kind, r_result, 7.5, name);
		return true;
	}
	bool index_get(void *p_receiver, const Argument &p_key, const Wanted &p_wanted, vm::Slot *r_result, std::string *) override {
		log += "get #" + std::to_string((intptr_t)p_receiver) + "[" + show(p_key) + "]\n";
		give(p_wanted.kind, r_result, 2.5, "text");
		return true;
	}
	bool index_set(void *p_receiver, const Argument &p_key, const Argument &p_value, std::string *) override {
		log += "set #" + std::to_string((intptr_t)p_receiver) + "[" + show(p_key) + "] = " + show(p_value) + "\n";
		return true;
	}
	const ir::Module *module = nullptr;
	void *bind(vm::Object *p_object, uint32_t p_function, void *p_owner, std::string *) override {
		void *made = box();
		log += "bind " + (module ? module->functions[p_function].name : std::to_string(p_function)) + " of " +
				(module ? module->classes[p_object->klass].name : std::string("?")) + " owner #" + std::to_string((intptr_t)p_owner) + " -> #" +
				std::to_string((intptr_t)made) + "\n";
		return made;
	}
	bool member(void *p_receiver, std::string_view p_name, const Wanted &p_wanted, vm::Slot *r_result, std::string *) override {
		log += "member #" + std::to_string((intptr_t)p_receiver) + "[" + std::string(p_name) + "] as " + std::string(p_wanted.type) + "\n";
		give(p_wanted.kind, r_result, 0, "");
		return true;
	}
	vm::Str *text(void *p_value) override {
		log += "text #" + std::to_string((intptr_t)p_value) + "\n";
		return vm::new_string("<#" + std::to_string((intptr_t)p_value) + ">");
	}
	bool operate(std::string_view p_operator, const Argument *p_arguments, size_t p_count, const Wanted &p_wanted, vm::Slot *r_result,
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
	bool set(void *p_receiver, std::string_view p_property, const Argument &p_value, std::string *r_error) override {
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
	void *copy(void *p_value) override {
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
};

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
	CHECK_EQ(compilation.format_diagnostics(), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(), std::string());
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

// Operators on Godot's values are the engine's to carry out.
static void test_engine_operators() {
	const char *source = "package { import godot.*; public class Mover extends Node2D {\n"
						 "  public function step(by:Vector2, n:int):Boolean {\n"
						 "    var v:Vector2 = position + by * n;\n"
						 "    v -= by;\n"
						 "    return -v == by;\n"
						 "  }\n"
						 "  public function nobody():Vector2 { var v:Vector2; return v + v; }\n"
						 "  public function isNobody(n:Node):Boolean { return n == this.getParent() || n != null; }\n"
						 "  public function say(v:Vector2, n:Node):String { trace(v); return \"v=\" + v + n.toString() + String(n); }\n"
						 "  public function sayNothing():String { var n:Node; var v:Vector2; trace(n); return v + \"\"; }\n"
						 "  public function sayNobody():String { var n:Node; return n.toString(); }\n"
						 "  public function nudge(n:Number):void { position.x = n; }\n"
						 "  public function nudgeMore(n:Number):void { transform.origin.x += n; }\n"
						 "  public function element(i:int, n:Number):Number { position[i] += n; return position[i + 1]; }\n"
						 "} }\n";
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	compilation.add("Mover.as", source);
	compilation.check();
	CHECK_EQ(compilation.format_diagnostics(), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(), std::string());
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
	CHECK_EQ(compilation.format_diagnostics(), std::string());
	ir::Module module;
	CHECK(ir::lower(compilation, &module, ir::LowerMode::Library));
	CHECK_EQ(compilation.format_diagnostics(), std::string());
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
		CHECK_EQ(program.format_diagnostics(), std::string());
		ir::Module out;
		CHECK(ir::lower(program, &out, ir::LowerMode::Library));
		CHECK_EQ(program.format_diagnostics(), std::string());
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
		std::string checked = program.format_diagnostics();
		ir::Module out;
		bool lowered = ir::lower(program, &out, ir::LowerMode::Library);
		std::string text = program.format_diagnostics();
		return std::string(checked.empty() ? "" : "CHECK ") + (lowered ? "LOWERED " : "") + text;
	};
	const char *bodies[] = {
		"function f():void { if (Input.isActionPressed(\"jump\")) {} }",
		"function f():void { treeEntered.add(function():void {}); }",
		"public var mine:int; function f():void { this[\"mine\"].add(f); }",
		"function f(n:Node):void { var x:int = n[\"name\"].length; }",
		"function f():void { print(\"hello\"); }",
		"function f():void { position.normalized().x = 5; }",
		"function f():void { position.normalized()[0] = 5; }",
		"function f(a:GodotArray):void { var x:int = a[0]; }",
		"function f():void { addChild(this); }",
		"function f():void { var other:Player = new Player(); }",
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
	CHECK_EQ(compilation.format_diagnostics(), std::string());
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

int main() {
	test_failed_static_initializer();
	test_loop_markers();
	test_engine_calls();
	test_engine_operators();
	test_engine_listeners();
	test_entry_points();
	test_statement_hook();
	test_refused_program();
	return check::finish();
}
