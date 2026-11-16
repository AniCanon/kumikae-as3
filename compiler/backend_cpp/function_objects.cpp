// Fields, static fields, classes, and the ways out of a function: jumps, returns and throws.
#include "function_writer.h"

#include "names.h"

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// A static field or package variable, as the kind's C++ type: a place for one that holds no reference.
std::string global(uint32_t p_global, Kind p_kind) {
	return from_slot(p_kind, "c->globals[" + std::to_string(p_global) + "]");
}

} // namespace

bool FunctionWriter::object(const Instruction &p_in) {
	// A field of the method's own object is read through the class's struct; any other object's class is
	// not known here, and its field is reached by its number.
	const auto field = [&](uint32_t p_object, uint32_t p_field, Kind p_kind) {
		if (is_this(p_object) && p_field < module_.classes[function_.owner].fields.size()) {
			const ir::Class &klass = module_.classes[function_.owner];
			return "static_cast<" + class_name(module_, function_.owner) + " *>(r0)->" + field_name(klass, p_field);
		}
		return "field<" + std::string(type_of(p_kind)) + ">(" + reg(p_object) + ", " + std::to_string(p_field) + ")";
	};
	switch (p_in.op) {
		case Op::Move:
			if (p_in.a != p_in.b) { // A register moved to itself is nothing, and C++ warns of it.
				share(p_in.a, reg(p_in.b));
			}
			return true;
		case Op::GetField:
			null_check(p_in.b);
			share(p_in.a, field(p_in.b, p_in.c, kind(p_in.a)));
			return true;
		case Op::SetField:
			null_check(p_in.a);
			if (ir::is_held(kind(p_in.c))) {
				line("store(c, " + field(p_in.a, p_in.b, kind(p_in.c)) + ", " + reg(p_in.c) + ");");
			} else {
				line(field(p_in.a, p_in.b, kind(p_in.c)) + " = " + reg(p_in.c) + ";");
			}
			return true;
		case Op::GetGlobal:
			share(p_in.a, global(p_in.b, kind(p_in.a)));
			return true;
		case Op::SetGlobal:
			if (ir::is_held(kind(p_in.b))) {
				line("store_slot(c, c->globals[" + std::to_string(p_in.a) + "], " + reg(p_in.b) + ");");
			} else {
				line(global(p_in.a, kind(p_in.b)) + " = " + reg(p_in.b) + ";");
			}
			return true;
		case Op::InitClass:
			mark();
			line("if (KAS3_UNLIKELY(!c->class_used[" + std::to_string(p_in.a) + "])) {");
			line("\tuse_class(c, " + std::to_string(p_in.a) + ");");
			line("\tif (KAS3_UNLIKELY(c->failing)) { " + fail() + " }");
			line("}");
			return true;
		case Op::NullCheck:
			null_check(p_in.a);
			return true;
		case Op::IsClass:
			line(reg(p_in.a) + " = is_class(c, " + reg(p_in.b) + ", " + std::to_string(p_in.c) + ");");
			return true;
		case Op::AsClass:
			share(p_in.a, "(is_class(c, " + reg(p_in.b) + ", " + std::to_string(p_in.c) + ") ? " + reg(p_in.b) + " : nullptr)");
			return true;
		case Op::StrToStr:
			share(p_in.a, "(" + reg(p_in.b) + " ? " + reg(p_in.b) + " : c->null_text)");
			return true;
		case Op::ErrorStack:
			// Only a program that reads it is given it: the others keep no frames to make it from.
			put(p_in.a, keeps_stack_ ? "error_stack(c, " + reg(p_in.b) + ")" : std::string("nullptr"));
			return true;
		case Op::ArgMissing:
			line(reg(p_in.a) + " = " + (takes_count(function_) ? std::to_string(p_in.b) + " >= n" : std::string("false")) + ";");
			return true;
		case Op::EqRef: {
			// The same object, or two function values that are one function: only those go to the library.
			const std::string same = "equal(" + reg(p_in.b) + ", " + reg(p_in.c) + ")";
			if (module_.function_class == NONE) {
				line(reg(p_in.a) + " = " + same + ";");
				return true;
			}
			const std::string function = std::to_string(module_.function_class);
			line("if (" + same + " || !" + reg(p_in.b) + " || !" + reg(p_in.c) + " || " + reg(p_in.b) + "->klass != " + function + " || " +
					reg(p_in.c) + "->klass != " + function + ") {");
			line("\t" + reg(p_in.a) + " = " + same + ";");
			line("} else");
			by_thunk(p_in);
			return true;
		}
		case Op::Trace: {
			const std::vector<uint32_t> &list = function_.argument_lists[p_in.a];
			std::string texts;
			for (uint32_t each : list) {
				texts += (texts.empty() ? "" : ", ") + reg(each);
			}
			line(list.empty() ? "trace(c, nullptr, 0);" : "{ Str *const t[] = { " + texts + " }; trace(c, t, " + std::to_string(list.size()) + "); }");
			return true;
		}
		default:
			return false;
	}
}

bool FunctionWriter::flow(const Instruction &p_in) {
	switch (p_in.op) {
		case Op::Stmt: {
			const std::string file = function_.file < module_.files.size() ? module_.files[function_.file] : std::string("?");
			body_ += "#line " + std::to_string(line_of(module_, function_.file, p_in.position)) + " " + string_literal(file) + "\n";
			return true;
		}
		case Op::Jump:
			line("goto L" + std::to_string(p_in.a) + ";");
			return true;
		case Op::JumpIf:
			line("if (" + reg(p_in.a) + ") goto L" + std::to_string(p_in.b) + ";");
			return true;
		case Op::JumpIfNot:
			line("if (!" + reg(p_in.a) + ") goto L" + std::to_string(p_in.b) + ";");
			return true;
		case Op::Return:
			// The result is the caller's: a register that owns its reference hands it over.
			line("result = " + reg(p_in.a) + ";");
			if (owns(p_in.a)) {
				line(reg(p_in.a) + " = " + std::string(zero_of(kind(p_in.a))) + ";");
			} else if (ir::is_held(kind(p_in.a))) {
				line("retain(c, result);");
			}
			line("goto done;");
			return true;
		case Op::ReturnVoid:
			line("goto done;");
			return true;
		case Op::Throw:
			line("throw_value(c, " + reg(p_in.a) + ");");
			line(fail());
			return true;
		case Op::Rethrow:
			line("rethrow(c, " + reg(p_in.a) + ", " + std::to_string(p_in.a) + ", &traced);");
			line(fail());
			return true;
		default:
			return false;
	}
}

} // namespace backend_cpp
} // namespace kas3
