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

void FunctionWriter::find_still() {
	std::vector<bool> counts_nothing = lent_;
	for (uint32_t i = 0; i < counts_nothing.size(); i++) {
		counts_nothing[i] = counts_nothing[i] || borrowed_[i] != NONE;
	}
	std::vector<uint32_t> read, written;
	bool runs = false; // Something in it may run code, and code may write any static field that is no constant.
	for (const Instruction &in : function_.code) {
		runs = runs || is_call(in.op) || may_run_code(module_, function_, in, counts_nothing);
		if (in.op == Op::GetGlobal && !ir::is_held(kind(in.a))) {
			read.push_back(in.b);
		} else if (in.op == Op::SetGlobal) {
			written.push_back(in.a);
		}
	}
	std::sort(read.begin(), read.end());
	read.erase(std::unique(read.begin(), read.end()), read.end());
	for (const uint32_t each : read) {
		if (std::find(written.begin(), written.end(), each) == written.end() && (!runs || own_constant(each))) {
			still_.push_back(each);
		}
	}
}

// Whether a static field is a constant of the function's own class or of one that class extends. Such a
// class's static fields are being given their values, or have been, by the time any of its functions runs
// (a class is set up before it is used, its bases first), and what gives them is not running beside the
// function but waiting beneath it or done: the constant is what it is for as long as the function runs.
// Not in a function that itself sets the constant's class up (InitClass): there the lowerer did not take
// the class for set up, and the constant is read where the function reads it.
bool FunctionWriter::own_constant(uint32_t p_global) const {
	const ir::Global &global = module_.globals[p_global];
	const size_t dot = global.name.rfind('.');
	if (!global.is_const || dot == std::string::npos) {
		return false;
	}
	const std::string_view owner = std::string_view(global.name).substr(0, dot);
	for (uint32_t at = function_.owner; at != NONE && at < module_.classes.size(); at = module_.classes[at].base) {
		if (module_.classes[at].name == owner) {
			return std::none_of(function_.code.begin(), function_.code.end(), [&](const Instruction &p_in) { return p_in.op == Op::InitClass && p_in.a == at; });
		}
	}
	return false;
}

void FunctionWriter::read_global(const Instruction &p_in) {
	if (std::binary_search(still_.begin(), still_.end(), p_in.b)) {
		line(reg(p_in.a) + " = g" + std::to_string(p_in.b) + ";");
	} else {
		share(p_in.a, global(p_in.b, kind(p_in.a)));
	}
}

std::string FunctionWriter::still_declared() const {
	std::string out;
	for (const uint32_t each : still_) {
		const Kind held = module_.globals[each].kind;
		out += "\tconst " + std::string(type_of(held)) + " g" + std::to_string(each) + " = " + global(each, held) + ";\n";
	}
	return out;
}

bool FunctionWriter::object(const Instruction &p_in) {
	const auto field = [&](uint32_t p_object, uint32_t p_field, Kind p_kind) { return field_of(p_object, p_field, p_kind); };
	switch (p_in.op) {
		case Op::Move:
			if (p_in.a == p_in.b) { // A register moved to itself is nothing, and C++ warns of it.
				return true;
			}
			if (hands_over_[pc_]) {
				keep(reg(p_in.a), p_in.b, false);
			} else {
				share(p_in.a, reg(p_in.b));
			}
			return true;
		case Op::GetField:
			null_check(p_in.b);
			read_field(p_in, field(p_in.b, p_in.c, kind(p_in.a)));
			return true;
		case Op::SetField:
			null_check(p_in.a);
			if (const std::optional<std::string> local = kept_local(p_in.a, p_in.b)) {
				line(*local + " = " + reg(p_in.c) + ";");
			} else if (ir::is_held(kind(p_in.c))) {
				keep(field(p_in.a, p_in.b, kind(p_in.c)), p_in.c, false);
			} else {
				line(field(p_in.a, p_in.b, kind(p_in.c)) + " = " + reg(p_in.c) + ";");
			}
			return true;
		case Op::GetGlobal:
			read_global(p_in);
			return true;
		case Op::SetGlobal:
			if (ir::is_held(kind(p_in.b))) {
				keep("c->globals[" + std::to_string(p_in.a) + "]", p_in.b, true);
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
		case Op::Arguments: {
			// What the caller gave, from the parameters as they are before anything writes them.
			const uint32_t fixed = function_.rest ? function_.parameters - 1 : function_.parameters;
			std::string slots;
			for (uint32_t i = 0; i < fixed; i++) {
				slots += (i ? ", slot(" : "slot(") + reg(i) + ")";
			}
			const bool bound = !function_.is_static && fixed > 0;
			mark();
			line("{");
			if (fixed > 0) {
				line("const Slot g[] = { " + slots + " };");
			}
			line("const Slot f = thunk(c, " + std::to_string(thunks_.function_value(index_, bound)) + ", " + (bound ? "g" : "nullptr") + ", nullptr);");
			check();
			line("Object *const e = arguments_of(c, " + std::to_string(index_) + ", " + (fixed > 0 ? "g" : "nullptr") + ", " +
					(takes_count(function_) ? std::string("n") : std::to_string(fixed)) + ", static_cast<Object *>(f.p));");
			check();
			put(p_in.a, "e");
			line("}");
			return true;
		}
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
			line(jump(p_in.a));
			return true;
		case Op::JumpIf:
		case Op::JumpIfNot:
			line("if (" + std::string(p_in.op == Op::JumpIf ? "" : "!") + reg(p_in.a) + ") { " + jump(p_in.b) + " }");
			return true;
		case Op::Return:
			// The result is the caller's: a register that owns its reference hands it over.
			line("result = " + reg(p_in.a) + ";");
			if (owns(p_in.a)) {
				line(reg(p_in.a) + " = " + std::string(zero_of(kind(p_in.a))) + ";");
			} else if (ir::is_held(kind(p_in.a))) {
				line("result = shared(c, result);");
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
