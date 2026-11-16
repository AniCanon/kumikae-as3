// The C++ backend on modules made by hand: what it refuses, it refuses by name and writes nothing; what it
// takes, it writes whole. (Whole programs are held to the interpreter's output by tests/run_native.py.)
#include "check.h"
#include "generate.h"

using namespace kas3;

namespace {

// A module of one function that takes nothing and gives nothing, with the registers and code given.
ir::Module module_of(std::vector<ir::Kind> p_registers, std::vector<ir::Instruction> p_code) {
	ir::Module module;
	module.files.push_back("one.as");
	module.lines.emplace_back();
	ir::Function function;
	function.name = "one";
	function.is_static = true;
	function.registers = std::move(p_registers);
	function.code = std::move(p_code);
	function.code.push_back(ir::Instruction{ ir::Op::ReturnVoid, 0, 0, 0, 0 });
	module.functions.push_back(std::move(function));
	return module;
}

bool has_file(const backend_cpp::Generated &p_generated, std::string_view p_name) {
	for (const backend_cpp::File &file : p_generated.files) {
		if (file.name == p_name) {
			return true;
		}
	}
	return false;
}

void a_program_it_takes_is_written_whole() {
	const backend_cpp::Generated made = backend_cpp::generate(module_of({ ir::Kind::I32 }, { ir::Instruction{ ir::Op::ConstI32, 0, 7, 0, 0 } }));
	CHECK(made.unsupported.empty());
	CHECK(has_file(made, "program.h"));
	CHECK(has_file(made, "program.cpp"));
}

void what_it_cannot_say_exactly_is_refused_by_name() {
	// A Number moved into an int with no conversion: no lowering writes it, and C++ would convert.
	const backend_cpp::Generated moved = backend_cpp::generate(
			module_of({ ir::Kind::I32, ir::Kind::F64 }, { ir::Instruction{ ir::Op::Move, 0, 1, 0, 0 } }));
	CHECK_EQ(moved.unsupported, std::string("a move between registers of two kinds"));
	CHECK(moved.files.empty());
	// Arithmetic of Numbers on a String's register.
	const backend_cpp::Generated added = backend_cpp::generate(
			module_of({ ir::Kind::F64, ir::Kind::Str, ir::Kind::F64 }, { ir::Instruction{ ir::Op::AddF64, 0, 1, 2, 0 } }));
	CHECK_EQ(added.unsupported, std::string("AddF64 on a register of another kind"));
	CHECK(added.files.empty());
}

// A program that reads an Error's stack keeps a line for each function in progress, set before whatever
// may raise. Where an operation is done here when it can be and left to the library when it cannot, the
// line is set inside what is left to the library: set before it, the `else` would end at the line, and the
// library would do the operation again after it was done here.
void what_is_left_to_the_library_is_one_statement() {
	ir::Module module = module_of({ ir::Kind::Str, ir::Kind::I32 }, { ir::Instruction{ ir::Op::I32ToStr, 0, 1, 0, 0 } });
	module.strings.push_back("getStackTrace");
	const backend_cpp::Generated made = backend_cpp::generate(module);
	CHECK(made.unsupported.empty());
	bool lines = false;
	for (const backend_cpp::File &file : made.files) {
		lines = lines || file.text.find("frame.line = ") != std::string::npos;
		CHECK(file.text.find("else\n\tframe.line") == std::string::npos);
		CHECK(file.text.find(")\n\tframe.line") == std::string::npos);
	}
	CHECK(lines);
}

} // namespace

int main() {
	a_program_it_takes_is_written_whole();
	what_it_cannot_say_exactly_is_refused_by_name();
	what_is_left_to_the_library_is_one_statement();
	return check::finish();
}
