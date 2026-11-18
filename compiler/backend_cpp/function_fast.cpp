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

// The vector an element of that register's kind is read from or written to where it is: one whose places
// hold handles, for one of Godot's values (vector_of(), runtime/fast.h); any at all, for the rest.
std::string FunctionWriter::vector_for(uint32_t p_vector, uint32_t p_element) const {
	return kind(p_element) == Kind::Val ? "vector_of(" + reg(p_vector) + ", 0)" : reg(p_vector);
}

bool FunctionWriter::fast_place(const Instruction &p_in) {
	const auto whole = [&](uint32_t p_register) { return kind(p_register) == Kind::I32 || kind(p_register) == Kind::U32; };
	// The register that holds the element inline, and the vector, where the instruction is one of these.
	uint32_t value = NONE, vector = NONE;
	if (p_in.op == Op::VecGet && whole(p_in.c)) {
		value = p_in.a;
		vector = p_in.b;
	} else if (p_in.op == Op::VecSet && whole(p_in.b)) {
		value = p_in.c;
		vector = p_in.a;
	} else if (p_in.op == Op::VecCall && static_cast<ir::VectorMethod>(p_in.b) == ir::VectorMethod::Push && function_.argument_lists[p_in.c].size() == 2) {
		value = function_.argument_lists[p_in.c][1];
		vector = function_.argument_lists[p_in.c][0];
	}
	const ir::Bare bare = value == NONE ? ir::Bare::None : ir::bare_named(parts(value));
	if (bare == ir::Bare::None) {
		return false;
	}
	// Only a vector that holds values of this very type whole: any other, none at all, an index outside it
	// and a vector that must grow are the same instruction with the value in a handle, the library's.
	const std::string of = "vector_of(" + reg(vector) + ", " + std::to_string(static_cast<unsigned>(bare)) + ")";
	const std::string type(parts(value));
	if (p_in.op == Op::VecGet) {
		line("if (const Slot *const e = element(" + of + ", " + reg(p_in.c) + ")) {");
		line("\t" + reg(value) + " = place_value<" + type + ">(*e);");
	} else if (p_in.op == Op::VecSet) {
		line("if (Slot *const e = element_to_write(c, " + of + ", " + reg(p_in.b) + ")) {");
		line("\t*e = value_place(" + reg(value) + ");");
	} else {
		const std::string length = "static_cast<Vector *>(" + reg(vector) + ")->length";
		line("if (Slot *const e = push_room(" + of + ")) {");
		line("\t*e = value_place(" + reg(value) + ");");
		line(p_in.a == NONE ? "\t" + length + "++;" : "\t" + reg(p_in.a) + " = ++" + length + ";");
	}
	line("} else");
	generic(p_in);
	return true;
}

bool FunctionWriter::fast_object(const Instruction &p_in) {
	const auto whole = [&](uint32_t p_register) { return kind(p_register) == Kind::I32 || kind(p_register) == Kind::U32; };
	if (fast_ && fast_vector(p_in)) {
		return true;
	}
	switch (p_in.op) {
		case Op::New:
			line("if (Object *const e = made_object(c, " + std::to_string(p_in.b) + ")) {");
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
			if (whole(p_in.a)) {
				// A vector of ints or of uints holds each in four bytes (runtime/fast.h, number()).
				line("if (const int32_t *const e = number(" + reg(p_in.b) + ", " + reg(p_in.c) + ")) {");
				line("\t" + reg(p_in.a) + " = static_cast<" + std::string(type_of(kind(p_in.a))) + ">(*e);");
				break;
			}
			line("if (Slot *const e = element(" + vector_for(p_in.b, p_in.a) + ", " + reg(p_in.c) + ")) {");
			// An Array's hole is read as undefined (vm/any.h).
			share(p_in.a, from_slot(kind(p_in.a), kind(p_in.a) == Kind::Any ? "kas3::vm::any::filled(*e)" : "(*e)"));
			break;
		case Op::VecSet:
			// An element there is, or one added at the end: a gap, a fixed vector and the rest are the library's.
			if (!whole(p_in.b)) {
				return false;
			}
			if (whole(p_in.c)) {
				line("if (int32_t *const e = number_to_write(c, " + reg(p_in.a) + ", " + reg(p_in.b) + ")) {");
				line("\t*e = static_cast<int32_t>(" + reg(p_in.c) + ");");
				break;
			}
			line("if (Slot *const e = element_to_write(c, " + vector_for(p_in.a, p_in.c) + ", " + reg(p_in.b) + ")) {");
			if (ir::is_held(kind(p_in.c))) {
				keep("*e", p_in.c, true); // The library, where the place is left to it, counts one of its own.
			} else {
				line("\t*e = slot(" + reg(p_in.c) + ");");
			}
			break;
		case Op::VecCall: {
			// push(x) where the vector has room for it: one that must grow, a fixed one and none at all are
			// the library's. The value is counted before the vector is any longer.
			const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
			if (static_cast<ir::VectorMethod>(p_in.b) != ir::VectorMethod::Push || list.size() != 2) {
				return false;
			}
			const std::string length = "static_cast<Vector *>(" + reg(list[0]) + ")->length";
			if (whole(list[1])) {
				line("if (int32_t *const e = number_room(" + reg(list[0]) + ")) {");
				line("\t*e = static_cast<int32_t>(" + reg(list[1]) + ");");
			} else {
				line("if (Slot *const e = push_room(" + vector_for(list[0], list[1]) + ")) {");
				line("\t*e = slot(" + (ir::is_held(kind(list[1])) ? "shared(c, " + reg(list[1]) + ")" : reg(list[1])) + ");");
			}
			line(p_in.a == NONE ? "\t" + length + "++;" : "\t" + reg(p_in.a) + " = ++" + length + ";");
			break;
		}
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
		case Op::StrCall: {
			// A method that only reads text is the library's function of plain values, called as it is, with
			// no thunk behind it: all that can go wrong is running out of memory.
			if (!ir::reads_text_only(static_cast<ir::StringFunction>(p_in.b))) {
				return false;
			}
			std::string given;
			for (const uint32_t each : function_.argument_lists[p_in.c]) {
				given += ", slot(" + reg(each) + ")";
			}
			line("if (Slot w = empty(); KAS3_LIKELY(text_method_of(c, " + std::to_string(p_in.b) + ", &w" + given + "))) {");
			put(p_in.a, from_slot(kind(p_in.a), "w"));
			line("} else {");
			mark();
			line("out_of_memory(c); " + fail());
			line("}");
			return true;
		}
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
			// A sum of whole numbers is read as the integer it is (function_whole.cpp): an int, unless it is
			// past an int's range. Nothing: a sum of nothing may be -0, which is the Number's to say.
			if (const std::string &sum = whole_[p_in.b].exact; !sum.empty()) {
				line("if (const int64_t n = " + sum + "; KAS3_LIKELY(n != 0 && n >= INT32_MIN && n <= INT32_MAX)) {");
				put(p_in.a, "to_any(c, static_cast<int32_t>(n))");
				line("} else");
			}
			line("if (Slot w = empty(); number_any(c, " + reg(p_in.b) + ", &w)) {");
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
		case Op::AnySub:
		case Op::AnyMul:
		case Op::AnyDiv:
		case Op::AnyMod:
		case Op::AnyNeg: {
			// Two numbers, here: AS3's arithmetic. Anything else (one of Godot's values, text, an object) is
			// the library's.
			const std::string number_kind = std::to_string(static_cast<unsigned>(Kind::F64));
			const std::string second = p_in.op == Op::AnyNeg ? "" : " && from_any(c, " + number_kind + ", " + number(NONE) + ", " + reg(p_in.c) + ", &y)";
			const std::string result = p_in.op == Op::AnySub ? "x.d - y.d" : p_in.op == Op::AnyMul ? "x.d * y.d" : p_in.op == Op::AnyDiv ? "x.d / y.d"
					: p_in.op == Op::AnyMod ? "std::fmod(x.d, y.d)" : "-x.d";
			line("if (Slot x = empty(), y = empty(), w = empty(); from_any(c, " + number_kind + ", " + number(NONE) + ", " + reg(p_in.b) + ", &x)" + second +
					" && number_any(c, " + result + ", &w)) {");
			line("(void)y;");
			put(p_in.a, "w");
			break;
		}
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
