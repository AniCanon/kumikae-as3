// Operations the library owns, written with a fast path for the usual case (runtime/fast.h). Whatever the
// fast path does not take is the same instruction by thunk: the unusual cases and the errors stay the
// library's. The names declared here (e, s, w, g) are not the thunk's (t, u, o), which follows as `else`.
#include "function_writer.h"

#include "names.h"

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

bool FunctionWriter::fast_object(const Instruction &p_in) {
	const auto whole = [&](uint32_t p_register) { return kind(p_register) == Kind::I32 || kind(p_register) == Kind::U32; };
	switch (p_in.op) {
		case Op::New:
			line("if (Object *const e = new_object(c, " + std::to_string(p_in.b) + ")) {");
			put(p_in.a, "e");
			break;
		case Op::VecLength:
			line("if (KAS3_LIKELY(" + reg(p_in.b) + ")) {");
			line("\t" + reg(p_in.a) + " = static_cast<" + std::string(type_of(kind(p_in.a))) + ">(static_cast<Vector *>(" + reg(p_in.b) + ")->length);");
			break;
		case Op::VecGet:
			if (!whole(p_in.c)) {
				return false;
			}
			line("if (Slot *const e = element(" + reg(p_in.b) + ", " + reg(p_in.c) + ")) {");
			share(p_in.a, from_slot(kind(p_in.a), "(*e)"));
			break;
		case Op::VecSet:
			// An element there is, or one added at the end: a gap, a fixed vector and the rest are the library's.
			if (!whole(p_in.b)) {
				return false;
			}
			line("if (Slot *const e = element_to_write(c, " + reg(p_in.a) + ", " + reg(p_in.b) + ")) {");
			line(ir::is_held(kind(p_in.c)) ? "\tstore_slot(c, *e, " + reg(p_in.c) + ");" : "\t*e = slot(" + reg(p_in.c) + ");");
			break;
		default:
			return false;
	}
	line("} else");
	by_thunk(p_in);
	return true;
}

bool FunctionWriter::fast_text(const Instruction &p_in) {
	switch (p_in.op) {
		case Op::Concat:
			line("if (Str *const s = join(c, " + reg(p_in.b) + ", " + reg(p_in.c) + ")) {");
			put(p_in.a, "s");
			break;
		case Op::I32ToStr:
		case Op::U32ToStr:
		case Op::F64ToStr:
		case Op::BoolToStr:
		case Op::I64ToStr:
			line("if (Str *const s = text_of(c, " + reg(p_in.b) + ")) {");
			put(p_in.a, "s");
			break;
		case Op::LtStr:
		case Op::LeStr:
			// With a missing String the two are compared as numbers: the library's.
			line("if (" + reg(p_in.b) + " && " + reg(p_in.c) + ") {");
			line("\t" + reg(p_in.a) + " = compare_text(c, " + reg(p_in.b) + ", " + reg(p_in.c) + ") " +
					(p_in.op == Op::LtStr ? "< 0;" : "<= 0;"));
			break;
		case Op::ObjToStr:
		case Op::AnyText:
			mark();
			line(std::string("if (Str *s = nullptr; ") + (p_in.op == Op::ObjToStr ? "object_text" : "any_text") + "(c, " + reg(p_in.b) + ", &s)) {");
			check();
			put(p_in.a, "s");
			break;
		default:
			return false;
	}
	line("} else");
	by_thunk(p_in);
	return true;
}

bool FunctionWriter::fast_untyped(const Instruction &p_in) {
	switch (p_in.op) {
		case Op::ToAny:
			if (kind(p_in.b) == Kind::Any) {
				share(p_in.a, reg(p_in.b));
				return true;
			}
			if (kind(p_in.b) == Kind::I64 || ir::is_engine(kind(p_in.b))) {
				return false; // An Int64 is held in a box, which the library makes; one of Godot's is the host's.
			}
			if (kind(p_in.b) != Kind::F64) {
				put(p_in.a, "to_any(c, " + reg(p_in.b) + ")");
				return true;
			}
			line("if (Slot w = empty(); number_to_any(c, " + reg(p_in.b) + ", &w)) {");
			put(p_in.a, "w");
			break;
		case Op::FromAny:
			if (kind(p_in.a) == Kind::I64) {
				return false;
			}
			line("if (Slot w = empty(); from_any(c, " + std::to_string(static_cast<unsigned>(kind(p_in.a))) + ", " + number(p_in.c) + ", " + reg(p_in.b) +
					", &w)) {");
			share(p_in.a, from_slot(kind(p_in.a), "w"));
			break;
		case Op::DynGet:
		case Op::DynSet: {
			// A property of an Object or a Dictionary under a String key, where the table keeps it.
			const uint32_t holder = p_in.op == Op::DynGet ? p_in.b : p_in.a;
			const uint32_t key = p_in.op == Op::DynGet ? p_in.c : p_in.b;
			const bool held = (kind(holder) == Kind::Obj || kind(holder) == Kind::Any) && (kind(key) == Kind::Str || kind(key) == Kind::Any);
			if (!held || (p_in.op == Op::DynSet && kind(p_in.c) != Kind::Any)) {
				return false;
			}
			const std::string place = "c, holder_of(c, " + reg(holder) + "), key_of(c, " + reg(key) + ")";
			if (p_in.op == Op::DynSet) {
				line("if (KAS3_UNLIKELY(!set_property(" + place + ", " + reg(p_in.c) + ")))");
				by_thunk(p_in);
				return true;
			}
			line("if (Slot *const e = property(" + place + ")) {");
			share(p_in.a, "*e");
			break;
		}
		case Op::AnyNullCheck:
			line("if (KAS3_UNLIKELY(!has_members(c, " + reg(p_in.a) + ")))");
			by_thunk(p_in);
			return true;
		default:
			return false;
	}
	line("} else");
	by_thunk(p_in);
	return true;
}

// f(a, b): the function called from here when it is a function value given what it takes; the library
// makes any other call, or says why it can't be made.
bool FunctionWriter::fast_call(const Instruction &p_in) {
	if (p_in.op != Op::CallFunction) {
		return false;
	}
	const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
	if (p_in.a == p_in.b) {
		return false; // The result's register is emptied first: it must not be what holds the function.
	}
	std::string given;
	for (uint32_t each : list) {
		if (kind(each) != Kind::Any || each == p_in.a) {
			return false;
		}
		given += (given.empty() ? "" : ", ") + reg(each);
	}
	if (p_in.a != NONE) {
		put(p_in.a, "empty()"); // A function that gives nothing gives undefined, and so does one that fails.
	}
	mark();
	line("{");
	if (!list.empty()) {
		line("const Slot g[] = { " + given + " };");
	}
	line("Slot w = empty();");
	line("if (call_value(c, function_of(c, " + reg(p_in.b) + "), " + (list.empty() ? "nullptr" : "g") + ", " + std::to_string(list.size()) + ", &w)) {");
	check();
	if (p_in.a != NONE) {
		put(p_in.a, "w");
	} else {
		line("release(c, w);");
	}
	line("} else");
	by_thunk(p_in);
	line("}");
	return true;
}

} // namespace backend_cpp
} // namespace kas3
