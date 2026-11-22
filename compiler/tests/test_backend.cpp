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

// Holder.sum(), with its classes: i = 0; v = this.items; n = v.length; <p_first>; while (i < n) { v = this.items;
// p = v[i]; <p_between>; p.x = p.x; i++ }. The loop is instructions 4 to 16; r11 is an object's register.
ir::Module module_of_sum(ir::Instruction p_first, ir::Instruction p_between) {
	using ir::Instruction;
	using ir::Kind;
	using ir::Op;
	ir::Module module = module_of(
			{ Kind::Obj, Kind::I32, Kind::Obj, Kind::Obj, Kind::F64, Kind::F64, Kind::F64, Kind::Bool, Kind::I32, Kind::F64, Kind::F64, Kind::Obj },
			{ Instruction{ Op::ConstI32, 1, 0, 0, 0 }, Instruction{ Op::GetField, 2, 0, 0, 0 }, Instruction{ Op::VecLength, 8, 2, 0, 0 }, p_first,
					Instruction{ Op::I32ToF64, 5, 1, 0, 0 }, Instruction{ Op::I32ToF64, 6, 8, 0, 0 }, Instruction{ Op::LtF64, 7, 5, 6, 0 },
					Instruction{ Op::JumpIfNot, 7, 17, 0, 0 }, Instruction{ Op::GetField, 2, 0, 0, 0 }, Instruction{ Op::VecGet, 3, 2, 1, 0 },
					p_between, Instruction{ Op::GetField, 4, 3, 0, 0 }, Instruction{ Op::SetField, 3, 0, 4, 0 },
					Instruction{ Op::ConstF64, 9, 0, 0, 0 }, Instruction{ Op::AddF64, 10, 5, 9, 0 }, Instruction{ Op::F64ToI32, 1, 10, 0, 0 },
					Instruction{ Op::Jump, 4, 0, 0, 0 } });
	module.numbers.push_back(1.0);
	add_points(module);
	ir::Class points;
	points.name = "Vector.<Point>";
	points.element = Kind::Obj;
	points.element_check = 0;
	module.classes.push_back(std::move(points));
	ir::Class holder;
	holder.name = "Holder";
	holder.fields.push_back(ir::Field{ "items", Kind::Obj, 2 });
	module.classes.push_back(std::move(holder));
	ir::Class link; // An object of it holds another: letting go of one may be the end of more.
	link.name = "Link";
	link.fields.push_back(ir::Field{ "next", Kind::Obj, 4 });
	module.classes.push_back(std::move(link));
	ir::Function &sum = module.functions[0];
	sum.name = "Holder/sum";
	sum.is_static = false;
	sum.owner = 3;
	sum.parameters = 1;
	sum.required = 1;
	return module;
}

// A loop that reads the elements of a vector of objects, in a method that lets go of nothing, is written
// twice like a loop over numbers: the first copy has the vector's length and elements in locals. A field
// written in the loop is of an element's class, which `this` is not: its number may be the number of the
// field the vector is read from, and the loop is still written twice.
void a_loop_over_a_vector_of_objects_is_written_twice() {
	const ir::Instruction nothing{ ir::Op::Stmt, 0, 0, 0, 0 };
	const backend_cpp::Generated made = backend_cpp::generate(module_of_sum(nothing, nothing));
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "Holder.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	const auto has = [&](std::string_view p_part) { return text->find(p_part) != std::string::npos; };
	CHECK(has("d2 = slots_in(static_cast<C3_Holder *>(r0)->f0_items);"));
	CHECK(has("r3 = static_cast<Object *>(d2[static_cast<uint32_t>(r1)].p);"));
	CHECK(has("if (Slot *const e = element(r2, r1)) {")); // The loop as it would be alone, which an index out of range leaves for.
	CHECK(has("static_cast<C0_Point *>(r3)->f0_x = r4;"));
}

// In a method that does let go of things (it makes an object into a register that held one), an element
// read into a variable still counts no reference where every use of the variable follows the read at once,
// with nothing between that could let go of anything: the vector holds the object until then. With
// something between whose end may be the end of other objects (a Link made into a register that held
// one), the variable counts one, as ever. A Point, which holds only numbers, ends alone: one let go of
// between the read and the use takes nothing out of any vector, and the variable still counts nothing.
void an_element_used_at_once_counts_no_reference() {
	const ir::Instruction nothing{ ir::Op::Stmt, 0, 0, 0, 0 };
	const ir::Instruction makes{ ir::Op::New, 11, 0, 0, 0 };
	const ir::Instruction links{ ir::Op::New, 11, 4, 0, 0 };
	const backend_cpp::Generated at_once = backend_cpp::generate(module_of_sum(links, nothing));
	const backend_cpp::Generated later = backend_cpp::generate(module_of_sum(nothing, links));
	const backend_cpp::Generated alone = backend_cpp::generate(module_of_sum(nothing, makes));
	CHECK_EQ(at_once.unsupported, std::string());
	CHECK_EQ(later.unsupported, std::string());
	CHECK_EQ(alone.unsupported, std::string());
	const std::string *text = file_named(at_once, "Holder.cpp");
	const std::string *counted = file_named(later, "Holder.cpp");
	const std::string *between = file_named(alone, "Holder.cpp");
	CHECK(text != nullptr && counted != nullptr && between != nullptr);
	if (!text || !counted || !between) {
		return;
	}
	CHECK(text->find("put(c, r11, e);") != std::string::npos);
	CHECK(text->find("r3 = static_cast<Object *>(d2[static_cast<uint32_t>(r1)].p);") != std::string::npos);
	CHECK(text->find("store(c, r3, ") == std::string::npos);
	// What the library hands back for an index out of range is counted once for the register: given back at once.
	CHECK(text->find("r3 = static_cast<Object *>(o.p);\n\trelease(c, r3);") != std::string::npos);
	CHECK(text->find("put(c, r3, ") == std::string::npos);
	CHECK(counted->find("store(c, r3, static_cast<Object *>((*e).p));") != std::string::npos);
	CHECK(counted->find("put(c, r3, static_cast<Object *>(o.p));") != std::string::npos);
	CHECK(between->find("put(c, r11, e);") != std::string::npos);
	CHECK(between->find("r3 = static_cast<Object *>(d2[static_cast<uint32_t>(r1)].p);") != std::string::npos);
	CHECK(between->find("store(c, r3, ") == std::string::npos);
}

// An object made and then stored in a vector's place as the last thing done with it is handed over: the
// place takes the register's reference, where it would count one more for the register to give back
// later. Stored and then read again, the register keeps its own.
void an_object_stored_in_a_place_last_is_handed_over() {
	using ir::Instruction;
	using ir::Kind;
	using ir::Op;
	const auto text_of = [](bool p_read_again) {
		// one(v:Vector.<Point>): i = 0; made = new Point; v[i] = made; [n = made.x]
		std::vector<Instruction> code = { Instruction{ Op::ConstI32, 1, 0, 0, 0 }, Instruction{ Op::New, 2, 0, 0, 0 },
			Instruction{ Op::VecSet, 0, 1, 2, 0 } };
		if (p_read_again) {
			code.push_back(Instruction{ Op::GetField, 3, 2, 0, 0 });
		}
		ir::Module module = module_of({ Kind::Obj, Kind::I32, Kind::Obj, Kind::F64 }, std::move(code));
		add_points(module);
		ir::Class points;
		points.name = "Vector.<Point>";
		points.element = Kind::Obj;
		points.element_check = 0;
		module.classes.push_back(std::move(points));
		module.functions[0].parameters = 1;
		module.functions[0].required = 1;
		module.functions[0].parameter_classes = { 2 };
		const backend_cpp::Generated made = backend_cpp::generate(module);
		CHECK_EQ(made.unsupported, std::string());
		const std::string *text = file_named(made, "program.cpp");
		CHECK(text != nullptr);
		return text ? *text : std::string();
	};
	const std::string handed = text_of(false);
	CHECK(handed.find("{ const Slot old = *e; *e = slot(r2); release(c, static_cast<Object *>(old.p)); }\n\tr2 = nullptr;") != std::string::npos);
	CHECK(handed.find("store_slot(c, *e, r2);") == std::string::npos);
	const std::string kept = text_of(true);
	CHECK(kept.find("store_slot(c, *e, r2);") != std::string::npos);
}

// `this.items[i] = new Point()`: the vector is read from its field before the object is made, and used
// after. The register it is read into borrows the field (it counts nothing) where nothing between the
// two, nor the store itself, can run code: a Point is made out of memory alone, and the Point the place
// held ends alone. A place that held a Link may end other objects, whose end may run code: the register
// counts a reference, as ever.
void a_vector_read_to_store_an_object_that_ends_alone_is_borrowed() {
	using ir::Instruction;
	using ir::Kind;
	using ir::Op;
	const auto text_of = [](uint32_t p_element) {
		// fill(): v = this.items; made = new <element>; i = 0; v[i] = made
		ir::Module module = module_of({ Kind::Obj, Kind::Obj, Kind::Obj, Kind::I32 },
				{ Instruction{ Op::GetField, 1, 0, 0, 0 }, Instruction{ Op::New, 2, p_element, 0, 0 }, Instruction{ Op::ConstI32, 3, 0, 0, 0 },
						Instruction{ Op::VecSet, 1, 3, 2, 0 } });
		add_points(module);
		ir::Class link;
		link.name = "Link";
		link.fields.push_back(ir::Field{ "next", Kind::Obj, 2 });
		module.classes.push_back(std::move(link));
		ir::Class items;
		items.name = "Vector.<T>";
		items.element = Kind::Obj;
		items.element_check = p_element;
		module.classes.push_back(std::move(items));
		ir::Class holder;
		holder.name = "Holder";
		holder.fields.push_back(ir::Field{ "items", Kind::Obj, 3 });
		module.classes.push_back(std::move(holder));
		ir::Function &fill = module.functions[0];
		fill.name = "Holder/fill";
		fill.is_static = false;
		fill.owner = 4;
		fill.parameters = 1;
		fill.required = 1;
		const backend_cpp::Generated made = backend_cpp::generate(module);
		CHECK_EQ(made.unsupported, std::string());
		const std::string *text = file_named(made, "Holder.cpp");
		CHECK(text != nullptr);
		return text ? *text : std::string();
	};
	const std::string points = text_of(0);
	CHECK(points.find("\tr1 = static_cast<C4_Holder *>(r0)->f0_items;") != std::string::npos);
	CHECK(points.find("store(c, r1, ") == std::string::npos);
	const std::string links = text_of(2);
	CHECK(links.find("store(c, r1, static_cast<C4_Holder *>(r0)->f0_items);") != std::string::npos);
}

// Holder.sum() (module_of_sum) with `p.grow()` between the element's read and its use: a call through
// Point's method table, slot 0. Point's grow() is sums on the object's own fields; Deep's is the code given,
// or Point's own where none is. p_known: the call says which class declares the method, as the lowerer
// does for a receiver other than `this`. A function of no class makes one object of each class.
ir::Module module_of_grow(const std::vector<ir::Instruction> *p_override, bool p_known) {
	using ir::Instruction;
	using ir::Kind;
	using ir::Op;
	const Instruction nothing{ Op::Stmt, 0, 0, 0, 0 };
	ir::Module module = module_of_sum(nothing, Instruction{ Op::CallVirtual, ir::NONE, 0, 0, 0 });
	module.functions[0].argument_lists.push_back({ 3 });
	if (p_known) {
		module.functions[0].virtual_receivers.emplace_back(10, 0);
	}
	const auto method = [&](std::string_view p_name, uint32_t p_owner, std::vector<Kind> p_registers, std::vector<Instruction> p_code) {
		ir::Function function;
		function.name = std::string(p_name);
		function.owner = p_owner;
		function.parameters = 1;
		function.required = 1;
		function.registers = std::move(p_registers);
		function.code = std::move(p_code);
		function.code.push_back(Instruction{ Op::ReturnVoid, 0, 0, 0, 0 });
		module.functions.push_back(std::move(function));
		return static_cast<uint32_t>(module.functions.size() - 1);
	};
	const std::vector<Instruction> sums = { Instruction{ Op::GetField, 1, 0, 0, 0 }, Instruction{ Op::SetField, 0, 1, 1, 0 } };
	const uint32_t grow = method("Point/grow", 0, { Kind::Obj, Kind::F64 }, sums);
	module.classes[0].methods = { grow };
	module.classes[1].methods = { p_override ? method("Deep/grow", 1, { Kind::Obj, Kind::F64, Kind::Obj }, *p_override) : grow };
	if (!p_known) {
		module.classes[3].methods = { grow }; // What the call's arguments are written by, where no class is named.
	}
	ir::Function makes;
	makes.name = "makes";
	makes.is_static = true;
	makes.registers = { Kind::Obj };
	makes.code = { Instruction{ Op::New, 0, 0, 0, 0 }, Instruction{ Op::New, 0, 1, 0, 0 }, Instruction{ Op::ReturnVoid, 0, 0, 0, 0 } };
	module.functions.push_back(std::move(makes));
	return module;
}

// A call through a method table is to one of the functions the classes that may be the receiver's have
// there, and the program is whole: each is known as it is written. Where the program makes objects of a few
// such classes, the object's class is compared and that class's function called by its name, which the C++
// compiler can put in the loop; the table is for any other class. The call does what those functions do
// taken together: where each is sums on the object's own fields, nothing is let go of and nothing can fail,
// so the element still counts no reference, the loop is still written twice and no error is looked for.
void a_call_through_a_table_is_what_every_override_is() {
	// Deep's grow() writes its own object's `x`: slot 0, which in a Holder is the vector. A Holder is no
	// Point, so the loop still has the vector's length and elements in locals.
	const std::vector<ir::Instruction> sums = { ir::Instruction{ ir::Op::GetField, 1, 0, 2, 0 }, ir::Instruction{ ir::Op::SetField, 0, 0, 1, 0 } };
	const backend_cpp::Generated made = backend_cpp::generate(module_of_grow(&sums, true));
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "Holder.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	const auto has = [&](std::string_view p_part) { return text->find(p_part) != std::string::npos; };
	CHECK(has("r3 = static_cast<Object *>(d2[static_cast<uint32_t>(r1)].p);"));
	CHECK(!has("store(c, r3, "));
	CHECK(has("if (k == 0) f1_Point_grow(c, r3);"));
	CHECK(has("else if (k == 1) f2_Deep_grow(c, r3);"));
	CHECK(has("else reinterpret_cast<void (*)(Context *, Object *)>(c->classes[k].code[0])(c, r3);"));
	CHECK(has("if (KAS3_UNLIKELY(deep)) { stack_overflow(c);")); // Neither function counts the depth itself.
	CHECK(has("(c, r3);\n\t}\n")); // No error is looked for after the call.
}

// One override that may do more is what the call may do: Deep's grow() makes a Link into a register, and
// letting go of a Link may be the end of other objects. The element counts a reference again, the loop is
// written once, and an error is looked for after the call. The classes are compared all the same.
void an_override_that_may_run_code_makes_the_call_one_that_may() {
	const std::vector<ir::Instruction> links = { ir::Instruction{ ir::Op::New, 2, 4, 0, 0 } };
	const backend_cpp::Generated made = backend_cpp::generate(module_of_grow(&links, true));
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "Holder.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	const auto has = [&](std::string_view p_part) { return text->find(p_part) != std::string::npos; };
	CHECK(has("store(c, r3, static_cast<Object *>((*e).p));"));
	CHECK(!has("d2["));
	CHECK(has("else if (k == 1) f2_Deep_grow(c, r3);"));
	CHECK(has("(c, r3);\n\tif (KAS3_UNLIKELY(c->failing))"));
}

// A field written through an object that is not the method's own may be a field of any object: Deep's
// grow() hands its object to a function of no class, which writes slot 0 of what it is given, and slot 0 of
// a Holder is the vector the loop reads. Nothing is let go of, so the element still counts no reference,
// but the vector's length and elements are read again each time round.
void a_field_written_through_any_object_may_be_the_callers() {
	using ir::Instruction;
	using ir::Kind;
	using ir::Op;
	const std::vector<Instruction> hands = { Instruction{ Op::Call, ir::NONE, 4, 0, 0 } };
	ir::Module module = module_of_grow(&hands, true);
	module.functions[2].argument_lists.push_back({ 0 });
	ir::Function pokes; // Function 4, after `makes`.
	pokes.name = "pokes";
	pokes.is_static = true;
	pokes.parameters = 1;
	pokes.required = 1;
	pokes.registers = { Kind::Obj, Kind::F64 };
	pokes.code = { Instruction{ Op::GetField, 1, 0, 1, 0 }, Instruction{ Op::SetField, 0, 0, 1, 0 }, Instruction{ Op::ReturnVoid, 0, 0, 0, 0 } };
	module.functions.push_back(std::move(pokes));
	const backend_cpp::Generated made = backend_cpp::generate(module);
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "Holder.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	const auto has = [&](std::string_view p_part) { return text->find(p_part) != std::string::npos; };
	CHECK(!has("d2["));
	CHECK(has("r3 = static_cast<Object *>((*e).p);"));
	CHECK(!has("store(c, r3, "));
}

// One function for every class that may be the receiver's is called by its name, with no class compared:
// no override of it is anywhere in the program.
void a_method_nothing_overrides_is_called_by_name() {
	const backend_cpp::Generated made = backend_cpp::generate(module_of_grow(nullptr, true));
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "Holder.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	const auto has = [&](std::string_view p_part) { return text->find(p_part) != std::string::npos; };
	CHECK(has("\tf1_Point_grow(c, r3);"));
	CHECK(!has("->klass"));
	CHECK(!has("store(c, r3, "));
	CHECK(has("r3 = static_cast<Object *>(d2[static_cast<uint32_t>(r1)].p);"));
}

// A receiver whose class nothing says (the call names no class, and the receiver is not the method's own
// object) may be of any class: the call goes through the table and may do anything.
void a_receiver_of_no_known_class_is_called_through_the_table() {
	const std::vector<ir::Instruction> sums = { ir::Instruction{ ir::Op::GetField, 1, 0, 2, 0 }, ir::Instruction{ ir::Op::SetField, 0, 2, 1, 0 } };
	const backend_cpp::Generated made = backend_cpp::generate(module_of_grow(&sums, false));
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "Holder.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	const auto has = [&](std::string_view p_part) { return text->find(p_part) != std::string::npos; };
	CHECK(has("(c->classes[r3->klass].code[0])(c, r3);"));
	CHECK(!has("f1_Point_grow(c, r3)"));
	CHECK(has("store(c, r3, static_cast<Object *>((*e).p));"));
	CHECK(has("(c, r3);\n\tif (KAS3_UNLIKELY(c->failing))"));
}

// A vector of ints or of uints holds each number in four bytes: an element read into a register of that
// kind, or stored from one, is read and written as the number where it is, and a loop over such a vector
// has those numbers in its local. A Number's place is a slot, as every other is.
void an_int_in_a_vector_is_four_bytes() {
	using ir::Instruction;
	using ir::Kind;
	using ir::Op;
	// one(v, w): i = 0; x = v[i]; v[i] = x; u = w[i]; w[i] = u; n = w[i]... and a loop: while (i < 3) { v[i] = v[i]; i = i + ... }
	ir::Module module = module_of({ Kind::Obj, Kind::Obj, Kind::I32, Kind::I32, Kind::U32, Kind::F64, Kind::Bool, Kind::Obj },
			{ Instruction{ Op::ConstI32, 2, 0, 0, 0 }, Instruction{ Op::VecGet, 3, 0, 2, 0 }, Instruction{ Op::VecSet, 0, 2, 3, 0 },
					Instruction{ Op::VecGet, 4, 1, 2, 0 }, Instruction{ Op::VecSet, 1, 2, 4, 0 }, Instruction{ Op::VecGet, 5, 7, 2, 0 },
					Instruction{ Op::VecSet, 7, 2, 5, 0 }, Instruction{ Op::VecCall, ir::NONE, static_cast<uint32_t>(ir::VectorMethod::Push), 0, 0 },
					Instruction{ Op::EqRef, 6, 0, 1, 0 }, Instruction{ Op::JumpIfNot, 6, 14, 0, 0 }, Instruction{ Op::VecGet, 3, 0, 2, 0 },
					Instruction{ Op::VecSet, 0, 2, 3, 0 }, Instruction{ Op::Jump, 8, 0, 0, 0 }, Instruction{ Op::Stmt, 0, 0, 0, 0 } });
	module.functions[0].argument_lists.push_back({ 0, 3 });
	module.functions[0].parameters = 2;
	module.functions[0].required = 2;
	const backend_cpp::Generated made = backend_cpp::generate(module);
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "program.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	const auto has = [&](std::string_view p_part) { return text->find(p_part) != std::string::npos; };
	CHECK(has("if (const int32_t *const e = number(r0, r2)) {\n\t\tr3 = static_cast<int32_t>(*e);"));
	CHECK(has("if (int32_t *const e = number_to_write(c, r0, r2)) {\n\t\t*e = static_cast<int32_t>(r3);"));
	CHECK(has("r4 = static_cast<uint32_t>(*e);"));
	CHECK(has("if (int32_t *const e = number_to_write(c, r1, r2)) {\n\t\t*e = static_cast<int32_t>(r4);"));
	CHECK(has("if (Slot *const e = element(r7, r2)) {")); // A Number's place is a slot.
	CHECK(has("if (int32_t *const e = number_room(r0)) {\n\t\t*e = static_cast<int32_t>(r3);"));
	// The loop: the numbers in a local, found only in a vector whose places are numbers.
	CHECK(has("int32_t *d0 = nullptr;"));
	CHECK(has("d0 = numbers_in(r0);"));
	CHECK(has("r3 = static_cast<int32_t>(d0[static_cast<uint32_t>(r2)]);"));
	CHECK(has("d0[static_cast<uint32_t>(r2)] = static_cast<int32_t>(r3);"));
}

void byte_loop_reads_and_writes_packed_places() {
	using ir::Kind;
	using ir::Op;
	ir::Module module = module_of({ Kind::Obj, Kind::I32, Kind::U8, Kind::Bool },
			{ { Op::JumpIfNot, 3, 4 }, { Op::VecGet, 2, 0, 1 }, { Op::VecSet, 0, 1, 2 }, { Op::Jump, 0 }, { Op::ReturnVoid } });
	const backend_cpp::Generated made = backend_cpp::generate(module);
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "program.cpp");
	CHECK(text && text->find("d0 = bytes_in(r0);") != std::string::npos);
}

void float_arithmetic_keeps_packed_places_in_a_loop() {
	using ir::Kind;
	using ir::Op;
	ir::Module module = module_of({ Kind::Obj, Kind::I32, Kind::F32, Kind::Bool },
			{ { Op::JumpIfNot, 3, 5 }, { Op::VecGet, 2, 0, 1 }, { Op::MulF32, 2, 2, 2 }, { Op::VecSet, 0, 1, 2 }, { Op::Jump, 0 }, { Op::ReturnVoid } });
	const backend_cpp::Generated made = backend_cpp::generate(module);
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "program.cpp");
	CHECK(text && text->find("d0 = floats_in(r0);") != std::string::npos);
}

// `y[i] = y[i] + 1`, where y is a field, reads the field into two registers: one vector, and so one set of
// places in the loop's locals, the second register's said to be the first's. Read apart, the C++ compiler
// took them for two memories that might overlap, found that they did, and ran the loop one element at a time.
void one_vector_read_twice_has_its_places_once() {
	using ir::Instruction;
	using ir::Kind;
	using ir::Op;
	ir::Module module = module_of(
			{ Kind::Obj, Kind::I32, Kind::Obj, Kind::Obj, Kind::F64, Kind::F64, Kind::F64, Kind::Bool, Kind::I32, Kind::F64, Kind::F64 },
			{ Instruction{ Op::ConstI32, 1, 0, 0, 0 }, Instruction{ Op::I32ToF64, 5, 1, 0, 0 }, Instruction{ Op::I32ToF64, 6, 8, 0, 0 },
					Instruction{ Op::LtF64, 7, 5, 6, 0 }, Instruction{ Op::JumpIfNot, 7, 13, 0, 0 }, Instruction{ Op::GetField, 2, 0, 0, 0 },
					Instruction{ Op::VecGet, 4, 2, 1, 0 }, Instruction{ Op::GetField, 3, 0, 0, 0 }, Instruction{ Op::VecSet, 3, 1, 4, 0 },
					Instruction{ Op::ConstF64, 9, 0, 0, 0 }, Instruction{ Op::AddF64, 10, 5, 9, 0 }, Instruction{ Op::F64ToI32, 1, 10, 0, 0 },
					Instruction{ Op::Jump, 1, 0, 0, 0 } });
	module.numbers.push_back(1.0);
	ir::Class numbers;
	numbers.name = "Vector.<Number>";
	numbers.element = Kind::F64;
	module.classes.push_back(std::move(numbers));
	ir::Class holder;
	holder.name = "Holder";
	holder.fields.push_back(ir::Field{ "items", Kind::Obj, 0 });
	module.classes.push_back(std::move(holder));
	ir::Function &adds = module.functions[0];
	adds.name = "Holder/adds";
	adds.is_static = false;
	adds.owner = 1;
	adds.parameters = 1;
	adds.required = 1;
	const backend_cpp::Generated made = backend_cpp::generate(module);
	CHECK_EQ(made.unsupported, std::string());
	const std::string *text = file_named(made, "Holder.cpp");
	CHECK(text != nullptr);
	if (!text) {
		return;
	}
	const auto has = [&](std::string_view p_part) { return text->find(p_part) != std::string::npos; };
	CHECK(has("d2 = slots_in(static_cast<C1_Holder *>(r0)->f0_items);"));
	CHECK(has("\td3 = d2;\n"));
	CHECK(has("\tn3 = n2;\n"));
	CHECK(has("if (KAS3_UNLIKELY(!d2)) goto L1;"));
}

void a_typed_callback_uses_the_typed_entry() {
	using ir::Kind;
	using ir::Op;
	ir::Module module = module_of({ Kind::Obj, Kind::I32, Kind::F64, Kind::Str },
			{ ir::Instruction{ Op::CallTypedFunction, 3, 0, 0, 0 },
					ir::Instruction{ Op::CallTypedFunction, ir::NONE, 0, 1, 0 } });
	module.functions[0].parameters = 3;
	module.functions[0].required = 3;
	module.functions[0].argument_lists = { { 1, 2 }, {} };
	const backend_cpp::Generated made = backend_cpp::generate(module);
	CHECK(made.unsupported.empty());
	const std::string *text = file_named(made, "program.cpp");
	CHECK(text && text->find("Slot a[] = { empty(), slot(r1), slot(r2) };") != std::string::npos);
	CHECK(text && text->find("call_typed_value(c, r0, a, 2, kinds,") != std::string::npos);
	CHECK(text && text->find("call_typed_value(c, r0, a, 0, nullptr,") != std::string::npos);
	CHECK(text && text->find("call_value(c,") == std::string::npos);
}

int main() {
	a_typed_callback_uses_the_typed_entry();
	a_program_it_takes_is_written_whole();
	what_it_cannot_say_exactly_is_refused_by_name();
	what_is_left_to_the_library_is_one_statement();
	a_field_of_a_known_class_is_written_through_its_struct();
	a_register_of_two_classes_holds_what_they_share();
	a_loop_over_a_vector_of_objects_is_written_twice();
	an_element_used_at_once_counts_no_reference();
	an_object_stored_in_a_place_last_is_handed_over();
	a_vector_read_to_store_an_object_that_ends_alone_is_borrowed();
	a_call_through_a_table_is_what_every_override_is();
	an_override_that_may_run_code_makes_the_call_one_that_may();
	a_field_written_through_any_object_may_be_the_callers();
	a_method_nothing_overrides_is_called_by_name();
	a_receiver_of_no_known_class_is_called_through_the_table();
	an_int_in_a_vector_is_four_bytes();
	byte_loop_reads_and_writes_packed_places();
	float_arithmetic_keeps_packed_places_in_a_loop();
	one_vector_read_twice_has_its_places_once();
	return check::finish();
}
