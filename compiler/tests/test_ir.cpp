#include "check.h"

#include "ir.h"
#include "last_use.h"
#include "liveness.h"
#include "temporary_releases.h"

#include <cstring>
#include <string>

using namespace kas3;
using namespace kas3::ir;

static void test_ops() {
	// Every operation has a name and a three-letter shape made of known letters.
	int count = 0;
#define X(name, shape) \
	CHECK_EQ(std::string(op_name(Op::name)), std::string(#name)); \
	CHECK_EQ(std::strlen(op_shape(Op::name)), (size_t)3); \
	for (const char *c = op_shape(Op::name); *c; c++) { \
		CHECK(std::strchr("RrTINWSLAFCDGVME-", *c) != nullptr); \
	} \
	count++;
	KAS3_IR_OPS(X)
#undef X
	CHECK(count > 50);
	CHECK(is_reference(Kind::Str) && is_reference(Kind::Obj) && !is_reference(Kind::F64));
}

static void test_print() {
	Module module;
	module.files.push_back("Main.as");
	module.strings.push_back("hi \"there\"\n");
	module.numbers.push_back(0.5);
	Class base;
	base.name = "Base";
	base.fields.push_back({ "n", Kind::I32 });
	module.classes.push_back(base);
	Class main;
	main.name = "game.Main";
	main.base = 0;
	main.fields.push_back({ "n", Kind::I32 });
	main.fields.push_back({ "label", Kind::Str });
	main.methods.push_back(0);
	main.constructor = 0;
	module.classes.push_back(main);
	module.globals.push_back({ "game.Main.count", Kind::F64 });
	module.entry_class = 1;
	module.initializers.push_back(0);

	Function function;
	function.name = "game.Main/step";
	function.owner = 1;
	function.parameters = 2;
	function.result = Kind::F64;
	function.registers = { Kind::Obj, Kind::F64, Kind::F64, Kind::Str, Kind::Bool };
	function.argument_lists.push_back({ 0, 1 });
	auto add = [&](Op p_op, uint32_t p_a = 0, uint32_t p_b = 0, uint32_t p_c = 0) {
		Instruction instruction;
		instruction.op = p_op;
		instruction.a = p_a;
		instruction.b = p_b;
		instruction.c = p_c;
		function.code.push_back(instruction);
	};
	add(Op::Stmt);
	add(Op::ConstF64, 2, 0);
	add(Op::AddF64, 2, 1, 2);
	add(Op::ConstStr, 3, 0);
	add(Op::GetField, 3, 0, 1);
	add(Op::SetGlobal, 0, 2);
	add(Op::LtF64, 4, 1, 2);
	add(Op::JumpIfNot, 4, 9);
	add(Op::Call, NONE, 0, 0);
	add(Op::CallVirtual, 2, 0, 0);
	add(Op::New, 0, 1);
	add(Op::ConstI32, 1, (uint32_t)-7);
	add(Op::Return, 2);
	module.functions.push_back(function);

	CHECK_EQ(print(module),
			std::string("class Base\n"
						"  .0 n:int\n"
						"class game.Main extends Base\n"
						"  .0 n:int\n"
						"  .1 label:String\n"
						"  #0 game.Main/step\n"
						"  new game.Main/step\n"
						"global game.Main.count:Number\n"
						"initializer game.Main/step\n"
						"entry game.Main\n"
						"function game.Main/step(r0:Object, r1:Number):Number\n"
						"  @1 ConstF64 r2 0.5\n"
						"  @2 AddF64 r2 r1 r2\n"
						"  @3 ConstStr r3 \"hi \\\"there\\\"\\n\"\n"
						"  @4 GetField r3 r0 .1\n"
						"  @5 SetGlobal game.Main.count r2\n"
						"  @6 LtF64 r4 r1 r2\n"
						"  @7 JumpIfNot r4 @9\n"
						"  @8 Call _ game.Main/step (r0, r1)\n"
						"  @9 CallVirtual r2 #0 (r0, r1)\n"
						"  @10 New r0 game.Main\n"
						"  @11 ConstI32 r1 -7\n"
						"  @12 Return r2\n"));
	// Indices that point nowhere print as such instead of crashing.
	Module broken;
	Function bad;
	bad.name = "bad";
	bad.is_static = true;
	Instruction instruction;
	instruction.op = Op::Call;
	instruction.a = NONE;
	instruction.b = 99;
	instruction.c = 99;
	bad.code.push_back(instruction);
	instruction.op = Op::ConstStr;
	instruction.b = 5;
	bad.code.push_back(instruction);
	broken.functions.push_back(bad);
	CHECK_EQ(print(broken), std::string("static function bad():void\n  @0 Call _ ?function (?)\n  @1 ConstStr r4294967295 ?string\n"));
}

// A value carried into one branch or an argument abandoned by a throw loses its hold
// before the first statement on the other path.
static void test_release_edges() {
	Function fn;
	fn.registers = { Kind::Any, Kind::Bool, Kind::Any, Kind::Any };
	fn.parameters = 2;
	fn.argument_lists = { {}, { 2 } };
	fn.code = { { Op::Call, 2, 0, 0 }, { Op::JumpIfNot, 1, 4 }, { Op::Call, NONE, 0, 1 },
		{ Op::ReturnVoid }, { Op::Stmt }, { Op::ReturnVoid }, { Op::Stmt }, { Op::ReturnVoid } };
	fn.handlers.push_back({ 2, 3, 6, 3 });
	TemporaryReleases().insert(fn);
	const auto &branch = fn.code[1];
	CHECK(branch.op == Op::JumpIfNot);
	CHECK(fn.code[branch.b].op == Op::Release && fn.code[branch.b].a == 2);
	const auto &caught = fn.code[fn.handlers[0].target];
	CHECK(caught.op == Op::Release && caught.a == 2);
}

// By instruction: the tracked registers it uses for the last time, in order.
static std::vector<std::vector<uint32_t>> endings(Liveness &r_live, const Function &p_fn) {
	std::vector<std::vector<uint32_t>> ending(p_fn.code.size());
	r_live.each_instruction([&](uint32_t p_pc, const std::vector<uint32_t> &p_ending) {
		ending[p_pc] = p_ending;
		std::sort(ending[p_pc].begin(), ending[p_pc].end());
	});
	return ending;
}

// A register read at the head of a loop stays live around the jump back, and ends only on the way out.
// One written and read inside a block ends at its read, and takes no part in what blocks hand each other.
static void test_liveness_around_a_loop() {
	Function fn;
	fn.registers = { Kind::Bool, Kind::Any, Kind::Any };
	fn.argument_lists = { {}, { 1 }, { 2 } };
	fn.code = { { Op::Call, 1, 0, 0 }, { Op::Call, NONE, 0, 1 }, { Op::Call, 2, 0, 0 }, { Op::Call, NONE, 0, 2 },
		{ Op::JumpIf, 0, 1 }, { Op::ReturnVoid } };
	Liveness live;
	live.solve(fn, { false, true, true });
	const auto ending = endings(live, fn);
	for (size_t pc = 0; pc < fn.code.size(); ++pc) {
		CHECK(ending[pc] == (pc == 3 ? std::vector<uint32_t>{ 2 } : std::vector<uint32_t>{}));
	}
	std::vector<uint32_t> leaving, repeating;
	live.each_instruction([&](uint32_t p_pc, const std::vector<uint32_t> &) {
		if (p_pc == 4) {
			live.dying_into(5, leaving);
			live.dying_into(1, repeating);
		}
	});
	CHECK(leaving == std::vector<uint32_t>{ 1 });
	CHECK(repeating.empty());
	CHECK(!live.entered_once(1)); // From above and from the branch.
	CHECK(live.entered_once(2));
	CHECK(live.entered_once(5));
	CHECK(!live.entered_once(0));
}

// An instruction in a try range may throw before it writes, so a register the handler reads stays live
// through a write in the range. What the range holds and the handler does not read is left to it.
static void test_liveness_into_a_handler() {
	Function fn;
	fn.registers = { Kind::Any, Kind::Any, Kind::Any };
	fn.argument_lists = { {}, { 1 }, { 2 } };
	fn.code = { { Op::Call, 2, 0, 0 }, { Op::Stmt }, { Op::Call, 2, 0, 0 }, { Op::Call, 1, 0, 0 }, { Op::ReturnVoid },
		{ Op::Call, NONE, 0, 2 }, { Op::ReturnVoid } };
	Liveness live;
	live.solve(fn, { false, true, true });
	CHECK(endings(live, fn)[0] == std::vector<uint32_t>{ 2 }); // The write at 2 replaces it unread.
	fn.handlers.push_back({ 2, 4, 5, 0 });
	live.solve(fn, { false, true, true });
	const auto ending = endings(live, fn);
	CHECK(ending[0].empty() && ending[2].empty());
	CHECK(ending[3] == std::vector<uint32_t>{ 1 });
	CHECK(ending[5] == std::vector<uint32_t>{ 2 });
	std::vector<uint32_t> past_the_range;
	live.each_instruction([&](uint32_t p_pc, const std::vector<uint32_t> &) {
		if (p_pc == 3) {
			live.dying_into(4, past_the_range);
		}
	});
	CHECK(past_the_range == std::vector<uint32_t>{ 2 });
	CHECK(!live.entered_once(5));
	std::vector<std::vector<uint32_t>> abandoned;
	live.abandoned_to_handlers(abandoned);
	CHECK(abandoned.size() == 1 && abandoned[0] == std::vector<uint32_t>{ 1 });
	// The same storage serves a function with nothing to track.
	live.solve(fn, { false, false, false });
	CHECK(endings(live, fn)[0].empty());
	live.abandoned_to_handlers(abandoned);
	CHECK(abandoned.size() == 1 && abandoned[0].empty());
}

// A register an instruction both reads and writes ends there only if the new value is never read.
static void test_liveness_of_a_register_read_and_written() {
	Function fn;
	fn.registers = { Kind::Str, Kind::Str };
	fn.parameters = 1;
	fn.code = { { Op::Concat, 1, 0, 0 }, { Op::Concat, 1, 1, 0 }, { Op::Concat, 1, 1, 0 }, { Op::ReturnVoid } };
	Liveness live;
	live.solve(fn, { false, true });
	const auto ending = endings(live, fn);
	CHECK(ending[0].empty() && ending[1].empty());
	CHECK(ending[2] == (std::vector<uint32_t>{ 1, 1 }));
}

// Handlers one inside another: each is left what its own range holds, the inner range's among it.
static void test_liveness_nested_handlers() {
	Function fn;
	fn.registers = { Kind::Any, Kind::Any, Kind::Any, Kind::Any };
	fn.argument_lists = { {}, { 3 } };
	fn.code = { { Op::Call, 1, 0, 0 }, { Op::Call, 2, 0, 0 }, { Op::Call, 3, 0, 0 }, { Op::ReturnVoid },
		{ Op::ReturnVoid }, { Op::Call, NONE, 0, 1 }, { Op::ReturnVoid } };
	fn.handlers.push_back({ 1, 2, 4, 0 });
	fn.handlers.push_back({ 0, 3, 5, 0 });
	Liveness live;
	live.solve(fn, { false, true, true, true });
	std::vector<std::vector<uint32_t>> abandoned;
	live.abandoned_to_handlers(abandoned);
	// r3 is live on entry to the inner range because the outer handler reads it.
	CHECK(abandoned[0] == (std::vector<uint32_t>{ 2, 3 }));
	CHECK(abandoned[1] == (std::vector<uint32_t>{ 1, 2 }));
}

// A string returned on one path is still held where a jump goes around the return. Its release must
// not be put after a Move that only the jump reaches.
static void test_release_stays_on_its_path() {
	Function fn;
	fn.registers = { Kind::Bool, Kind::Str, Kind::Str, Kind::Str };
	fn.parameters = 2;
	fn.code = { { Op::Concat, 2, 1, 1 }, { Op::JumpIf, 0, 3 }, { Op::Return, 2 }, { Op::Move, 3, 1 }, { Op::Return, 2 } };
	TemporaryReleases().insert(fn);
	for (const auto &in : fn.code) {
		CHECK(!(in.op == Op::Release && in.a == 2));
	}
}

static void test_store_live_on_another_path() {
	Function fn;
	fn.registers = { Kind::Obj, Kind::Obj, Kind::Bool, Kind::Any };
	fn.code = { { Op::SetField, 0, 0, 1 }, { Op::JumpIf, 2, 0 }, { Op::ReturnVoid },
		{ Op::NullCheck, 1 }, { Op::ReturnVoid } };
	std::vector<bool> fields(fn.code.size());
	fields[0] = true;
	CHECK(!last_stores(fn, fields)[0]); // The next iteration reads the source again.
	fn.code[1] = { Op::Stmt };
	CHECK(last_stores(fn, fields)[0]);
	fn.handlers.push_back({ 0, 1, 3, 3 });
	CHECK(!last_stores(fn, fields)[0]); // The catch block still sees the local.
}

int main() {
	test_liveness_around_a_loop();
	test_liveness_into_a_handler();
	test_liveness_of_a_register_read_and_written();
	test_liveness_nested_handlers();
	test_store_live_on_another_path();
	test_release_edges();
	test_release_stays_on_its_path();
	test_ops();
	test_print();
	return check::finish();
}
