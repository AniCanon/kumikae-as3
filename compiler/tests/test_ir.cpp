#include "check.h"

#include "ir.h"

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
		CHECK(std::strchr("RrINSLAFCDGV-", *c) != nullptr); \
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

int main() {
	test_ops();
	test_print();
	return check::finish();
}
