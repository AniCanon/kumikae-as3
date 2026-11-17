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

const std::string *file_named(const backend_cpp::Generated &p_generated, std::string_view p_name) {
	for (const backend_cpp::File &file : p_generated.files) {
		if (file.name == p_name) {
			return &file.text;
		}
	}
	return nullptr;
}

// A class of two Numbers, `x` and `y`, and one that extends it with a third, `z`.
void add_points(ir::Module &r_module) {
	ir::Class point;
	point.name = "Point";
	point.fields.push_back(ir::Field{ "x", ir::Kind::F64 });
	point.fields.push_back(ir::Field{ "y", ir::Kind::F64 });
	ir::Class deep = point;
	deep.name = "Deep";
	deep.base = static_cast<uint32_t>(r_module.classes.size());
	deep.fields.push_back(ir::Field{ "z", ir::Kind::F64 });
	r_module.classes.push_back(std::move(point));
	r_module.classes.push_back(std::move(deep));
}

// A field of an object whose class is known where it is read (a parameter declared of the class, a new
// object, a copy of either) is written through the class's struct, which tells the C++ compiler that a
// write of one field leaves another as it was. An object of no known class has its field by its number,
// which says nothing: a write through it may be of any field.
void a_field_of_a_known_class_is_written_through_its_struct() {
	// one(p:Point, q:Deep, o): p.y = q.x; made = new Point; copy = made; copy.x = q.z; o.x = p.x
	ir::Module module = module_of({ ir::Kind::Obj, ir::Kind::Obj, ir::Kind::Obj, ir::Kind::F64, ir::Kind::Obj, ir::Kind::Obj },
			{ ir::Instruction{ ir::Op::GetField, 3, 1, 0, 0 }, ir::Instruction{ ir::Op::SetField, 0, 1, 3, 0 },
					ir::Instruction{ ir::Op::New, 4, 0, 0, 0 }, ir::Instruction{ ir::Op::Move, 5, 4, 0, 0 },
					ir::Instruction{ ir::Op::GetField, 3, 1, 2, 0 }, ir::Instruction{ ir::Op::SetField, 5, 0, 3, 0 },
					ir::Instruction{ ir::Op::GetField, 3, 0, 0, 0 }, ir::Instruction{ ir::Op::SetField, 2, 0, 3, 0 } });
	add_points(module);
	module.functions[0].parameters = 3;
	module.functions[0].required = 3;
	module.functions[0].parameter_classes = { 0, 1, ir::NONE };
	const backend_cpp::Generated made = backend_cpp::generate(module);
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "program.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	const auto has = [&](std::string_view p_part) { return text->find(p_part) != std::string::npos; };
	CHECK(has("static_cast<C0_Point *>(r0)->f1_y = r3;"));
	CHECK(has("r3 = static_cast<C1_Deep *>(r1)->f0_x;")); // A base's field, through the class the register holds.
	CHECK(has("r3 = static_cast<C1_Deep *>(r1)->f2_z;"));
	CHECK(has("static_cast<C0_Point *>(r5)->f0_x = r3;")); // A copy of a new object.
	CHECK(has("field<double>(r2, 0) = r3;")); // No class is known for the third parameter.
}

// A register given objects of two classes holds their nearest common base, and none where they have none:
// a field is then read by its number.
void a_register_of_two_classes_holds_what_they_share() {
	// one(): a = new Point; a = new Deep; n = a.x; b = new Point; b = null; b = a as Other; n = b.x
	ir::Module module = module_of({ ir::Kind::Obj, ir::Kind::F64, ir::Kind::Obj, ir::Kind::I32, ir::Kind::Bool },
			{ ir::Instruction{ ir::Op::New, 0, 0, 0, 0 }, ir::Instruction{ ir::Op::New, 0, 1, 0, 0 }, ir::Instruction{ ir::Op::GetField, 1, 0, 0, 0 },
					ir::Instruction{ ir::Op::New, 2, 0, 0, 0 }, ir::Instruction{ ir::Op::ConstNull, 2, 0, 0, 0 },
					ir::Instruction{ ir::Op::AsClass, 2, 0, 2, 0 }, ir::Instruction{ ir::Op::GetField, 1, 2, 0, 0 } });
	add_points(module);
	ir::Class other;
	other.name = "Other";
	other.fields.push_back(ir::Field{ "x", ir::Kind::F64 });
	module.classes.push_back(std::move(other));
	const backend_cpp::Generated made = backend_cpp::generate(module);
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "program.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	CHECK(text->find("r1 = static_cast<C0_Point *>(r0)->f0_x;") != std::string::npos);
	CHECK(text->find("r1 = field<double>(r2, 0);") != std::string::npos);
}

int main() {
	a_program_it_takes_is_written_whole();
	what_it_cannot_say_exactly_is_refused_by_name();
	what_is_left_to_the_library_is_one_statement();
	a_field_of_a_known_class_is_written_through_its_struct();
	a_register_of_two_classes_holds_what_they_share();
	return check::finish();
}
