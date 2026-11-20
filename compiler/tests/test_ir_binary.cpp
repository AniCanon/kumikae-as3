#include "check.h"
#include "builtins.h"
#include "interpreter.h"
#include "ir.h"
#include "ir_binary.h"
#include "lower.h"
#include "names.h"

#include <string>
#include <vector>

using namespace kas3;

namespace {

bool lower_files(const std::vector<std::pair<const char *, const char *>> &p_files, bool p_godot, ir::Module *r_module) {
	Compilation compilation;
	if (p_godot) {
		compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	}
	for (const auto &[path, source] : p_files) {
		compilation.add(path, source);
	}
	compilation.check();
	if (compilation.has_errors()) {
		return false;
	}
	const ir::LowerMode mode = p_godot ? ir::LowerMode::Library : ir::LowerMode::Program;
	return ir::lower(compilation, r_module, mode) && !compilation.has_errors();
}

std::string run_program(const ir::Module &p_module) {
	struct Capture : vm::Output {
		std::string lines;
		void print(std::string_view p_line) override {
			lines.append(p_line);
			lines.push_back('\n');
		}
		void error(std::string_view p_line) override {
			lines.append("ERR:");
			lines.append(p_line);
			lines.push_back('\n');
		}
	} output;
	vm::SystemClock clock(0.0);
	vm::Interpreter vm(p_module, vm::Host(output, vm::NoEngine::instance(), vm::NoEngine::instance(), clock));
	if (!vm.run_program()) {
		return "RUN_FAILED:" + output.lines;
	}
	return output.lines;
}

} // namespace

static void test_round_trip_program_and_execution() {
	const char *greeter = R"(
		package {
			public interface Greeter {
				function greet(who:String):String;
			}
		}
	)";
	const char *base = R"(
		package {
			public class Base implements Greeter {
				public static const PREFIX:String = "hi ";
				protected var count:int = 2;
				public function greet(who:String):String {
					return PREFIX + who + ":" + count;
				}
			}
		}
	)";
	const char *main_src = R"(
		package {
			public class Main extends Base {
				public function Main() {
					var nums:Vector.<int> = new <int>[10, 20, 30];
					var sum:int = 0;
					var add:Function = function(v:int):void { sum += v; };
					for each (var n:int in nums) {
						add(n);
					}
					try {
						throw new Error("boom");
					} catch (e:Error) {
						trace(greet("as3") + " " + sum + " " + e.message);
					} finally {
						trace("done");
					}
				}
			}
		}
	)";
	ir::Module original;
	CHECK(lower_files({ { "Main.as", main_src }, { "Base.as", base }, { "Greeter.as", greeter } }, false, &original));

	const std::vector<uint8_t> bytes = ir::write_binary(original);
	CHECK(!bytes.empty());

	ir::Module decoded;
	const std::optional<std::string> error = ir::read_binary(bytes.data(), bytes.size(), &decoded);
	CHECK(!error.has_value());
	CHECK_EQ(ir::print(decoded), ir::print(original));
	CHECK_EQ(ir::fingerprint(decoded), ir::fingerprint(original));
	CHECK_EQ(run_program(decoded), run_program(original));
	CHECK_EQ(run_program(decoded), std::string("hi as3:2 60 boom\ndone\n"));
}

static void test_round_trip_godot_script_metadata() {
	const char *source = R"(
		package demo {
			import godot.*;
			public class Courier extends CharacterBody2D {
				[Export] public var speed:Number = 330;
				[Export] public var origin:Vector2 = new Vector2(12, 34);
				[Export] public var label:String = "meridian";
				[Signal] public var dashed:Signal;
				override public function _physicsProcess(delta:Number):void {
					velocity = new Vector2(speed * delta, 0);
					moveAndSlide();
				}
			}
		}
	)";
	ir::Module original;
	CHECK(lower_files({ { "demo/Courier.as", source } }, true, &original));

	const std::vector<uint8_t> bytes = ir::write_binary(original);
	ir::Module decoded;
	const std::optional<std::string> error = ir::read_binary(bytes.data(), bytes.size(), &decoded);
	CHECK(!error.has_value());
	CHECK_EQ(ir::print(decoded), ir::print(original));
	CHECK_EQ(ir::fingerprint(decoded), ir::fingerprint(original));
	CHECK_EQ(decoded.engine_names.size(), original.engine_names.size());
	CHECK_EQ(decoded.classes.size(), original.classes.size());
	bool found_courier = false;
	for (const ir::Class &c : decoded.classes) {
		if (c.name == "demo.Courier") {
			found_courier = true;
			CHECK_EQ(c.native_base, std::string("CharacterBody2D"));
			CHECK_EQ(c.signals.size(), size_t(1));
			CHECK_EQ(c.signals[0], std::string("dashed"));
			CHECK(!c.virtuals.empty());
		}
	}
	CHECK(found_courier);
}

static void test_rejects_corrupt_or_truncated_modules() {
	ir::Module original;
	CHECK(lower_files({ { "A.as", "package { public class A { public function A() { trace(42); } } }" } }, false, &original));
	const std::vector<uint8_t> valid = ir::write_binary(original);

	ir::Module out;
	CHECK(ir::read_binary(nullptr, 0, &out).has_value());
	const uint8_t bad_magic[8] = { 'N', 'O', 'P', 'E', 1, 0, 0, 0 };
	CHECK(ir::read_binary(bad_magic, sizeof(bad_magic), &out).has_value());

	std::vector<uint8_t> bad_version = valid;
	bad_version[4] = 99;
	CHECK(ir::read_binary(bad_version.data(), bad_version.size(), &out).has_value());

	std::vector<uint8_t> trailing = valid;
	trailing.push_back(0);
	CHECK(ir::read_binary(trailing.data(), trailing.size(), &out).has_value());

	for (size_t cut = 0; cut < valid.size(); cut++) {
		CHECK(ir::read_binary(valid.data(), cut, &out).has_value());
	}
}

int main() {
	test_round_trip_program_and_execution();
	test_round_trip_godot_script_metadata();
	test_rejects_corrupt_or_truncated_modules();
	return check::finish();
}
