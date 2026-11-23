// A signal's emit(), where generated code makes it itself (the instruction is a call of a method of Godot's
// Signal, which the library otherwise makes by name on boxed values): what it hands over is given to the
// host as it is held, and a signal that is a field of `this` is emitted through the field's own handle.
#include "function_writer.h"

#include "names.h"

#include <algorithm>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

bool whole(Kind p_kind) {
	return p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::I64;
}

} // namespace

// Whether an instruction only works a value out of others, all of them numbers, Booleans or values held
// inline: whichever way it is written, it runs no code of the program's and writes no field.
bool FunctionWriter::only_values(const Instruction &p_in) const {
	const auto value = [&](uint32_t p_register) {
		const Kind held = kind(p_register);
		return held == Kind::F64 || held == Kind::Bool || whole(held) || !parts(p_register).empty();
	};
	if (p_in.a == NONE || !value(p_in.a)) {
		return false;
	}
	// Arithmetic and conversions (an Int64's among them, which the list of what runs no code leaves out
	// because one may raise an error): registers only, every one such a value.
	const std::string_view shape = ir::op_shape(p_in.op);
	if (shape == "RR-" || shape == "RRR") {
		return value(p_in.b) && (shape[2] == '-' || value(p_in.c));
	}
	if (p_in.op == Op::HostGet) {
		return value(p_in.b);
	}
	if (p_in.op != Op::HostNew && p_in.op != Op::HostOperate && p_in.op != Op::HostCall) {
		return false;
	}
	const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
	return std::all_of(list.begin(), list.end(), value);
}

// `hit.emit(a, b)`, where hit is a field of `this`, reads the field into a register only to emit through
// it. A register is a handle of its own: one made and one given back for every emit. The field holds the
// signal for as long as nothing writes the field, so where the one read of such a register is the emit, in
// straight code after the field is read with nothing between that could write the field or run the
// program's code, the emit is made through the field's own handle (engine_emit). A listener may put another
// signal in the field while the emit goes on: the host reads nothing of the handle once Godot has begun.
//
// `p + velocity * delta`, where velocity is a field that holds a value with parts, is the same thing: the
// register is read once, by an operator, a method or a part of the value, and what that needs is the
// value, which is copied out of the field's handle before anything else is done (value_from_).
void FunctionWriter::find_lent_signals() {
	const size_t count = function_.registers.size();
	signal_from_.assign(count, NONE);
	value_from_.assign(count, NONE);
	if (!is_this(0) || module_.classes[function_.owner].native_base.empty()) {
		return;
	}
	const ir::Class &klass = module_.classes[function_.owner];
	std::vector<uint32_t> writes(count, 0), reads(count, 0), read_at(count, NONE), written_at(count, NONE);
	for (uint32_t pc = 0; pc < function_.code.size(); pc++) {
		const Instruction &in = function_.code[pc];
		const char *shape = ir::op_shape(in.op);
		const uint32_t operands[3] = { in.a, in.b, in.c };
		const bool writes_first = (shape[0] == 'R' || shape[0] == 'r') && in.a != NONE && !first_is_read(in.op);
		if (writes_first) {
			writes[in.a]++;
			written_at[in.a] = pc;
		}
		// A method of one of Godot's values may change the value it is called on; emit() leaves the signal
		// as it was.
		const bool emits = in.op == Op::HostCall && module_.engine_names[in.b].key == "Signal.emit";
		if (const uint32_t changed = changed_in_place(function_, in); changed != NONE && !emits) {
			writes[changed] += 2;
		}
		const auto read = [&](uint32_t p_register) {
			reads[p_register]++;
			read_at[p_register] = pc;
		};
		for (int n = 0; n < 3; n++) {
			if (shape[n] == 'A') {
				std::for_each(function_.argument_lists[operands[n]].begin(), function_.argument_lists[operands[n]].end(), read);
			} else if ((shape[n] == 'R' || shape[n] == 'r') && operands[n] != NONE && !(n == 0 && writes_first)) {
				read(operands[n]);
			}
		}
	}
	std::vector<bool> lands = targets_;
	for (const ir::Handler &handler : function_.handlers) {
		writes[handler.reg] += 2;
		lands[handler.target] = true;
	}
	for (uint32_t i = function_.parameters; i < count; i++) {
		if (kind(i) != Kind::Val || writes[i] != 1 || reads[i] != 1 || read_at[i] <= written_at[i]) {
			continue;
		}
		const Instruction &from = function_.code[written_at[i]];
		const Instruction &emit = function_.code[read_at[i]];
		if (from.op != Op::GetField || from.b != 0 || from.c >= klass.fields.size() || klass.fields[from.c].kind != Kind::Val) {
			continue;
		}
		// Only an emit generated code makes itself: one left to the library is handed the register. Or
		// what asks Godot's own code for a value with parts, which is written with the value itself.
		const bool emitted = emits_bare(emit) && function_.argument_lists[emit.c][0] == i;
		const bool asked = !emitted && !value_type(i).empty() && parts(i).empty() &&
				(emit.op == Op::HostOperate || emit.op == Op::HostGet || (emit.op == Op::HostCall && module_.engine_names[emit.b].key != "Signal.emit"));
		if (!emitted && !asked) {
			continue;
		}
		bool still = !lands[read_at[i]];
		for (uint32_t pc = written_at[i] + 1; still && pc < read_at[i]; pc++) {
			const Instruction &in = function_.code[pc];
			// A call is as its callee is: one that may write the field, or run code nothing looks into, is
			// not quiet (while the facts are being found, none is).
			const FunctionFacts *callee = is_call(in.op) ? called(pc) : nullptr;
			const bool writes_field = is_call(in.op) && (!callee || callee->opaque || may_be_field(module_, callee->writes, function_.owner, from.c));
			const bool quiet = !may_run_code(module_, function_, in) && !writes_field && !(in.op == Op::SetField && in.b == from.c) &&
					std::string_view(ir::op_shape(in.op)).find('L') == std::string_view::npos && in.op != Op::Return && in.op != Op::ReturnVoid &&
					in.op != Op::Throw && in.op != Op::Rethrow;
			still = !lands[pc] && (quiet || only_values(in));
		}
		if (still) {
			(emitted ? signal_from_ : value_from_)[i] = from.c;
		}
	}
}

// A signal's emit(), where every value it hands over is one Godot is given as generated code holds it: a
// number, a Boolean, a value with parts by its address, one of Godot's values or objects by its handle.
// The host emits it by what it worked out once for the place; a String, a script object or an untyped
// value among them, a signal that is not there or one whose object is gone, is the instruction by its thunk.
bool FunctionWriter::emits_bare(const Instruction &p_in) const {
	if (p_in.op != Op::HostCall || p_in.a != NONE || module_.engine_names[p_in.b].key != "Signal.emit") {
		return false;
	}
	const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
	if (list.empty() || kind(list[0]) != Kind::Val || !parts(list[0]).empty() || list.size() > 16) {
		return false;
	}
	return std::all_of(list.begin() + 1, list.end(), [&](uint32_t p_given) {
		const Kind held = kind(p_given);
		return whole(held) || held == Kind::F64 || held == Kind::Bool || held == Kind::Val || held == Kind::Ref;
	});
}

// Such an emit, written: each value's place and how it is held, the host's emit, and the instruction by its
// thunk for what the host leaves.
bool FunctionWriter::engine_emit(const Instruction &p_in) {
	if (!emits_bare(p_in)) {
		return false;
	}
	const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
	std::string temporaries, held, values;
	for (size_t i = 1; i < list.size(); i++) {
		const uint32_t given = list[i];
		const std::string temporary = "a" + std::to_string(i);
		const std::string comma = i > 1 ? ", " : "";
		if (whole(kind(given))) {
			temporaries += "\tconst int64_t " + temporary + " = static_cast<int64_t>(" + reg(given) + ");\n";
			held += comma + "GIVEN_WHOLE";
			values += comma + "&" + temporary;
		} else if (kind(given) == Kind::F64) {
			held += comma + "GIVEN_NUMBER";
			values += comma + "&" + reg(given);
		} else if (kind(given) == Kind::Bool) {
			temporaries += "\tconst uint8_t " + temporary + " = " + reg(given) + ";\n";
			held += comma + "GIVEN_FLAG";
			values += comma + "&" + temporary;
		} else if (!parts(given).empty()) {
			held += comma + "given_parts<" + std::string(parts(given)) + ">()";
			values += comma + "&" + reg(given);
		} else {
			held += comma + "GIVEN_HANDLE";
			values += comma + reg(given) + ".handle";
		}
	}
	const uint32_t use = thunks_.use_index(thunks_.use(function_, p_in).thunk);
	const std::string count = std::to_string(list.size() - 1);
	// The signal's handle: the register's own, or the field's where the register only stands for it.
	const uint32_t lent = signal_from_[list[0]];
	const std::string signal = lent == NONE ? reg(list[0]) : own_field(lent);
	line("{");
	line("void *const emitter = c->resolved[" + std::to_string(use) + "].bind;");
	body_ += temporaries;
	if (list.size() > 1) {
		line("static const uint8_t k[] = { " + held + " };");
		line("const void *const p[] = { " + values + " };");
	}
	mark();
	line("if (KAS3_LIKELY(emitter && c->engine->emit(c, emitter, " + signal + ".handle, " + (list.size() > 1 ? "k, p, " : "nullptr, nullptr, ") + count + "))) {");
	// The listeners ran meanwhile, and one of the program's may have raised.
	line("if (KAS3_UNLIKELY(c->failing || (c->listener_failed && call_failed(c)))) { " + fail() + " }");
	line("} else {");
	if (lent != NONE) {
		// The library reads the handle while the listeners run: it is given one of its own.
		const std::string kept = "h" + std::to_string(list[0]);
		line("Held " + kept + "(c, " + signal + ");");
		shadow_[list[0]] = kept + ".v";
	}
	generic(p_in);
	if (lent != NONE) {
		shadow_[list[0]].clear();
	}
	line("}");
	line("}");
	return true;
}

} // namespace backend_cpp
} // namespace kas3
