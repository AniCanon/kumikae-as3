#include "function_writer.h"

#include "library_entries.h"
#include "names.h"

#include <algorithm>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

bool takes_count(const ir::Function &p_function) {
	const uint32_t fixed = p_function.rest ? p_function.parameters - 1 : p_function.parameters;
	return fixed > p_function.required;
}

Signature signature_of(const ir::Function &p_function) {
	Signature signature;
	signature.result = type_of(p_function.result);
	signature.parameters.push_back("Context *");
	for (uint32_t i = 0; i < p_function.parameters; i++) {
		const std::string_view inline_type = parts_of(p_function, i);
		signature.parameters.push_back(std::string(inline_type.empty() ? type_of(p_function.registers[i]) : inline_type));
	}
	signature.counted = takes_count(p_function);
	if (signature.counted) {
		signature.parameters.push_back("uint32_t");
	}
	return signature;
}

FunctionWriter::FunctionWriter(const ir::Module &p_module, uint32_t p_function, ThunkTable &r_thunks, bool p_keeps_stack, const Facts *p_facts,
		bool p_remembers) :
		module_(p_module), index_(p_function), function_(p_module.functions[p_function]), thunks_(r_thunks), keeps_stack_(p_keeps_stack),
		remembers_(p_remembers), facts_(p_facts) {
	analyse();
}

// The constructor of a class whose chain can be run again says what its object is constructed with, as it
// begins and before a default is given: the arguments as they came, and how many. Which classes' objects
// are remembered, and that a base's constructor run by `super(...)` is not the object's own, is the
// host's to say (vm/constructions.h). An argument held inline is no plain value: the object has no record,
// and the host is told only that one of its class was made.
std::string FunctionWriter::told() const {
	const uint32_t owner = function_.owner;
	if (!remembers_ || owner == NONE || module_.classes[owner].constructor != index_ || function_.parameters == 0 || ir::constructor_refusal(module_, owner)) {
		return std::string();
	}
	std::string values;
	bool plain = true;
	for (uint32_t i = 0; i < function_.parameters; i++) {
		plain = plain && parts(i).empty();
		values += (i ? ", " : "") + ("slot(" + register_name(i) + ")");
	}
	const std::string count = takes_count(function_) ? "n" : std::to_string(function_.parameters);
	// An object the host keeps only the arguments of is remembered here, where it is made (remembered_here).
	const std::string call = plain ? "const Slot t[] = { " + values + " }; if (!remembered_here(c, " + register_name(0) + ", " + std::to_string(owner) + ", t, " +
											 count + ")) { constructing(c, " + std::to_string(index_) + ", t, " + count + "); }"
								   : "constructing(c, " + std::to_string(index_) + ", nullptr, 0);";
	return "\tif (KAS3_UNLIKELY(c->remembers)) { " + call + " }\n";
}

void FunctionWriter::analyse() {
	targets_.assign(function_.code.size() + 1, false);
	rounds_.assign(function_.code.size() + 1, false);
	written_.assign(function_.registers.size(), false);
	for (uint32_t pc = 0; pc < function_.code.size(); pc++) {
		const Instruction &in = function_.code[pc];
		if (const uint32_t target = jump_target(in); target != NONE) {
			targets_[target] = true;
			rounds_[target] = rounds_[target] || target <= pc;
		}
		const char first = ir::op_shape(in.op)[0];
		if ((first == 'R' || first == 'r') && in.a != NONE && !first_is_read(in.op)) {
			written_[in.a] = true;
		}
		if (const uint32_t changed = changed_in_place(function_, in); changed != NONE) {
			written_[changed] = true;
		}
	}
	for (const ir::Handler &handler : function_.handlers) {
		written_[handler.reg] = true;
	}
	shadow_.assign(function_.registers.size(), std::string());
	borrowed_.assign(function_.registers.size(), NONE);
	lent_.assign(function_.registers.size(), false);
	find_classes();
	find_inline();
	singleton_use_.assign(function_.registers.size(), NONE);
	find_aliases();
	find_lent_signals();
	find_borrowed();
	find_still();
	find_kept();
	find_last_uses();
	find_twice();
	find_counted();
}

std::string_view FunctionWriter::parts(uint32_t p_register) const {
	return inline_[p_register] ? parts_of(function_, p_register) : std::string_view();
}

std::string FunctionWriter::reg(uint32_t p_register) const {
	return shadow_[p_register].empty() ? register_name(p_register) : shadow_[p_register];
}

bool FunctionWriter::owns(uint32_t p_register) const {
	return ir::is_held(kind(p_register)) && parts(p_register).empty() && alias_[p_register] == Alias::None && borrowed_[p_register] == NONE &&
			!lent_[p_register] &&
			(p_register >= function_.parameters || written_[p_register]);
}

bool FunctionWriter::is_this(uint32_t p_register) const {
	return p_register == 0 && function_.owner != NONE && !function_.is_static && function_.parameters > 0 && !written_[0];
}

void FunctionWriter::line(const std::string &p_text) {
	body_ += "\t" + p_text + "\n";
}

void FunctionWriter::put(uint32_t p_register, const std::string &p_owned) {
	if (lent_[p_register]) {
		// What is handed over is counted once for this register, which counts nothing: the vector it was
		// read from holds it all the while.
		line(reg(p_register) + " = " + p_owned + ";");
		line("release(c, " + reg(p_register) + ");");
	} else if (ir::is_held(kind(p_register))) {
		line("put(c, " + reg(p_register) + ", " + p_owned + ");");
	} else {
		line(reg(p_register) + " = " + p_owned + ";");
	}
}

void FunctionWriter::share(uint32_t p_register, const std::string &p_borrowed) {
	if (kind(p_register) == Kind::Val && parts(p_register).empty() && has_parts(value_type(p_register))) {
		line("copy_parts<" + std::string(value_type(p_register)) + ">(c, " + reg(p_register) + ", " + p_borrowed + ");");
		return;
	}
	if (ir::is_held(kind(p_register)) && !lent_[p_register]) {
		line("store(c, " + reg(p_register) + ", " + p_borrowed + ");");
	} else {
		line(reg(p_register) + " = " + p_borrowed + ";");
	}
}

uint32_t FunctionWriter::chain_of(uint32_t p_pc) {
	std::vector<uint32_t> chain;
	for (size_t i = 0; i < function_.handlers.size(); i++) {
		if (p_pc >= function_.handlers[i].start && p_pc < function_.handlers[i].end) {
			chain.push_back(static_cast<uint32_t>(i));
		}
	}
	if (chain.empty()) {
		return NONE;
	}
	const auto found = std::find(chains_.begin(), chains_.end(), chain);
	if (found != chains_.end()) {
		return static_cast<uint32_t>(found - chains_.begin());
	}
	chains_.push_back(std::move(chain));
	return static_cast<uint32_t>(chains_.size() - 1);
}

std::string FunctionWriter::fail() {
	fails_ = true;
	const uint32_t chain = chain_of(pc_);
	const uint32_t at = line_of(module_, function_.file, function_.code[pc_].position);
	// A kept field the instruction may have changed is read again first: the local is what is written back.
	const std::string again = kept_read_again(pc_);
	return again + (again.empty() ? "" : " ") + "line = " + std::to_string(at) + "; goto " + (chain == NONE ? std::string("failed") : "H" + std::to_string(chain)) + ";";
}

void FunctionWriter::check() {
	line("if (KAS3_UNLIKELY(c->failing)) { " + fail() + " }");
}

void FunctionWriter::mark() {
	if (keeps_stack_) {
		line("frame.line = " + std::to_string(line_of(module_, function_.file, function_.code[pc_].position)) + ";");
	}
}

void FunctionWriter::null_check(uint32_t p_register) {
	if (is_this(p_register)) {
		return;
	}
	// One of Godot's is missing when there is no handle on it: a freed object is the engine's to find.
	const std::string held = ir::is_engine(kind(p_register)) ? reg(p_register) + ".handle" : reg(p_register);
	line("if (KAS3_UNLIKELY(!" + held + ")) { raise_null(c); " + fail() + " }");
}

void FunctionWriter::by_thunk(const Instruction &p_in) {
	if (p_in.op == Op::CallFunction && !ir::library_entry(function_, p_in).name.empty()) {
		// The library's call is of a function value. A class called as a function converts what it is
		// given, which is the interpreter's own still, and so are the words for anything else.
		line("if (KAS3_LIKELY(holds_function(c, " + reg(p_in.b) + "))) {");
		by_library(p_in);
		line("} else {");
		raw_thunk(p_in);
		line("}");
		return;
	}
	if (by_library(p_in)) {
		return; // The library's function of plain values does it whole: no thunk.
	}
	raw_thunk(p_in);
}

void FunctionWriter::raw_thunk(const Instruction &p_in) {
	const ThunkUse use = thunks_.use(function_, p_in);
	// One statement, the line it is at set inside it: it may be what follows an `else`.
	line("{");
	mark();
	std::string given = "nullptr";
	if (!use.given.empty()) {
		std::string slots;
		for (uint32_t each : use.given) {
			slots += (slots.empty() ? "" : ", ") + ("slot(" + reg(each) + ")");
		}
		line("const Slot t[] = { " + slots + " };");
		given = "t";
	}
	if (use.updated != NONE) {
		line("Slot u = empty();");
	}
	line(std::string(use.result != NONE ? "const Slot o = " : "") + "thunk(c, " + std::to_string(use.thunk) + ", " + given +
			(use.updated != NONE ? ", &u" : ", nullptr") + ");");
	check();
	if (use.updated != NONE) {
		put(use.updated, from_slot(kind(use.updated), "u"));
	}
	if (use.result != NONE) {
		put(use.result, from_slot(kind(use.result), "o"));
	}
	line("}");
}

void FunctionWriter::dispatch(const Instruction &p_in) {
	const bool written = counted(p_in) || whole_conversion(p_in) || whole_comparison(p_in) || constant(p_in) || simple(p_in) || object(p_in) || flow(p_in) || call(p_in) || fast_object(p_in) ||
			fast_text(p_in) || fast_named(p_in) || fast_untyped(p_in) || fast_call(p_in);
	if (!written) {
		by_thunk(p_in);
	}
}

// An instruction that is not Godot's own done inline. What it reads or writes of values held inline it is
// given as handles: each is boxed first, and one it writes is copied back out. A call hands a value
// straight to a parameter that holds the same type inline. A register that stands for a field's handle
// (value_from_) is given a handle of its own: what the library runs may write the field meanwhile.
void FunctionWriter::generic(const Instruction &p_in) {
	const char *shape = ir::op_shape(p_in.op);
	const uint32_t operands[3] = { p_in.a, p_in.b, p_in.c };
	const ir::Function *callee = callee_of(p_in);
	std::vector<uint32_t> boxed;
	std::vector<uint32_t> held;
	const auto box = [&](uint32_t p_register) {
		if (!parts(p_register).empty() && std::find(boxed.begin(), boxed.end(), p_register) == boxed.end()) {
			boxed.push_back(p_register);
		}
		// (Its one write is the field's read, which gives it nothing.)
		if (value_from_[p_register] != NONE && p_in.op != Op::GetField && shadow_[p_register].empty() &&
				std::find(held.begin(), held.end(), p_register) == held.end()) {
			held.push_back(p_register);
		}
	};
	for (int n = 0; n < 3; n++) {
		if ((shape[n] == 'R' || shape[n] == 'r') && operands[n] != NONE) {
			box(operands[n]);
		} else if (shape[n] == 'A') {
			const std::vector<uint32_t> &list = function_.argument_lists[operands[n]];
			for (size_t i = 0; i < list.size(); i++) {
				const bool straight = callee && i < callee->parameters && !parts(list[i]).empty() && parts_of(*callee, static_cast<uint32_t>(i)) == parts(list[i]);
				if (!straight) {
					box(list[i]);
				}
			}
		}
	}
	if (boxed.empty() && held.empty()) {
		dispatch(p_in);
		return;
	}
	line("{");
	for (uint32_t each : held) {
		line("Held h" + std::to_string(each) + "(c, " + own_field(value_from_[each]) + ");");
		shadow_[each] = "h" + std::to_string(each) + ".v";
	}
	for (uint32_t each : boxed) {
		line("Boxed<" + std::string(parts(each)) + "> x" + std::to_string(each) + "(c, " + register_name(each) + ");");
		shadow_[each] = "x" + std::to_string(each) + ".v";
	}
	dispatch(p_in);
	const bool writes = (shape[0] == 'R' || shape[0] == 'r') && p_in.a != NONE && !first_is_read(p_in.op);
	const uint32_t changed = changed_in_place(function_, p_in);
	for (uint32_t each : boxed) {
		if ((writes && each == p_in.a) || each == changed) {
			line(register_name(each) + " = unbox<" + std::string(parts(each)) + ">(c, " + shadow_[each] + ".handle);");
		}
		shadow_[each].clear();
	}
	for (uint32_t each : held) {
		shadow_[each].clear();
	}
	line("}");
}

void FunctionWriter::instruction(const Instruction &p_in) {
	// A counted loop's first stretch begins as the loop is entered; a round goes to the label after.
	if (const std::optional<Counts> how = counts(pc_)) {
		body_ += how->start + ":;\n";
		line(how->begin);
		body_ += how->label + ":;\n";
	}
	// A direct binding can bypass the host's interpreter, including when it invokes a callback.
	if (p_in.op >= Op::HostNew && p_in.op <= Op::HostIndexSet) {
		line("if (c->script_declarations) c->script_declarations->store(false, std::memory_order_relaxed);");
	}
	// Around what may see or change a field kept in a local, memory and the local are made to agree.
	const std::string before = kept_written_back(pc_);
	const std::string after = kept_read_again(pc_);
	if (!before.empty()) {
		line(before);
	}
	if (pc_ == joined_) {
		joined_ = NONE; // Written with the instruction before it.
	} else if (!engine(p_in) && !fast_place(p_in)) {
		generic(p_in);
	}
	if (!after.empty()) {
		line(after);
	}

	// What was known of a register is forgotten when it is written, and with it what was made from it.
	const uint32_t moved_text = p_in.op == Op::Move && kind(p_in.a) == Kind::Str ? text_[p_in.b] : NONE;
	const bool moved_exact = p_in.op == Op::Move && kind(p_in.a) == Kind::Str ? text_exact_[p_in.b] : false;
	const char first = ir::op_shape(p_in.op)[0];
	if ((first == 'R' || first == 'r') && p_in.a != NONE && !first_is_read(p_in.op)) {
		forget_whole(p_in.a);
	}
	if (const uint32_t changed = changed_in_place(function_, p_in); changed != NONE) {
		forget_whole(changed);
	}
	remember_whole(p_in);
	if (p_in.op == Op::ConstStr) {
		text_[p_in.a] = p_in.b;
		text_exact_[p_in.a] = true;
	} else if (moved_text != NONE) {
		text_[p_in.a] = moved_text;
		text_exact_[p_in.a] = moved_exact;
	} else if (p_in.op == Op::GetGlobal && facts_ && p_in.b < facts_->const_strings.size() && facts_->const_strings[p_in.b] != NONE) {
		text_[p_in.a] = facts_->const_strings[p_in.b];
		text_exact_[p_in.a] = false;
	}
}

std::string FunctionWriter::prologue() const {
	std::string out;
	const auto add = [&](const std::string &p_text) { out += "\t" + p_text + "\n"; };
	for (const uint32_t use : loop_binds_) {
		const std::string index = std::to_string(use);
		add("void *const bind" + index + " = c->resolved ? c->resolved[" + index + "].bind : nullptr;");
	}
	std::string unused = "(void)c;";
	for (uint32_t i = 0; i < function_.registers.size(); i++) {
		if (i >= function_.parameters) {
			add(parts(i).empty() ? declared(kind(i), reg(i)) + " = " + std::string(zero_of(kind(i))) + ";"
								 : std::string(parts(i)) + " " + reg(i) + "{};");
		}
		unused += " (void)" + reg(i) + ";";
	}
	if (takes_count(function_)) {
		unused += " (void)n;";
	}
	if (fails_) {
		add("uint32_t line = 0;");
		add("bool traced = false;");
		unused += " (void)traced; (void)line;";
	}
	if (std::find(rounds_.begin(), rounds_.end(), true) != rounds_.end()) {
		add("uint32_t rounds = ROUNDS;"); // Until the next look at the host's alarm (jump()).
		unused += " (void)rounds;";
	}
	add(unused);
	const bool gives = function_.result != Kind::Void;
	if (gives) {
		add(declared(function_.result, "result") + " = " + std::string(zero_of(function_.result)) + ";");
	}
	// A function in which nothing can fail calls nothing of the program's. It is still one call too deep
	// at the limit, which whoever calls it sees to before the call (`deep`, below; its entry, for a call
	// from outside): it keeps no count itself, sets no error, and its one frame fits in what the stack's
	// floor leaves free.
	if (fails_) {
		// The host's watch asks for the clock by the floor: then the function goes on, unless its time is up.
		add("if (KAS3_UNLIKELY(c->depth >= MAX_DEPTH || stack_low(c)) && stack_overflow(c)) { return" + std::string(gives ? " result" : "") + "; }");
		add("c->depth++;");
		// Whether a call from here is one too deep: asked once, of a count nothing moves while this runs.
		add("const bool deep = c->depth >= MAX_DEPTH;");
		add("(void)deep;");
	}
	if (keeps_stack_) {
		add("StackFrame frame = { " + std::to_string(index_) + ", 0, c->frames };");
		add("c->frames = &frame;");
	}
	for (uint32_t i = 0; i < function_.parameters; i++) {
		if (owns(i)) {
			add(reg(i) + " = shared(c, " + reg(i) + ");");
		}
	}
	out += told() + still_declared() + kept_declared() + twice_declared() + counted_declared();
	if (has_owner_alias_) {
		// The script's own Godot object: found once, and again only after the engine has freed a script's.
		add("void *self = object_of(c, field<Ref>(r0, 0));");
		add("uint32_t self_seen = c->freed.load(std::memory_order_relaxed);");
		add("(void)self; (void)self_seen;");
	}
	return out;
}

std::string FunctionWriter::handlers() const {
	std::string out;
	for (size_t i = 0; i < chains_.size(); i++) {
		out += "H" + std::to_string(i) + ":\n";
		// Taking the error lets go of what the handler's register held, which may run code (facts.h).
		const std::string back = kept_written_back(NONE);
		const std::string again = kept_read_again(NONE);
		if (!back.empty()) {
			out += "\t" + back + "\n";
		}
		for (uint32_t each : chains_[i]) {
			const ir::Handler &handler = function_.handlers[each];
			const Kind held = kind(handler.reg);
			const std::string error = "caught(c, " + std::to_string(index_) + ", line, " + std::to_string(handler.reg) + ", " +
					(handler.rethrows ? "true" : "false") + ", &traced)";
			const std::string target = again + (again.empty() ? "" : " ") + "goto L" + std::to_string(handler.target) + ";";
			out += "\tif (catches(c, " + number(handler.type) + ")) {";
			if (held == Kind::Obj || held == Kind::Any) {
				out += " put(c, " + reg(handler.reg) + ", " + (held == Kind::Obj ? "object_of(c, " + error + ")" : error) + "); " + target + " }\n";
				continue;
			}
			// A catch of a String or a number: the value thrown as that kind, as a store makes it one.
			out += "\n\t\tconst Slot e = " + error + ";\n\t\tconst Slot o = thunk(c, " + std::to_string(thunks_.from_any(held)) + ", &e, nullptr);\n";
			out += "\t\trelease(c, e);\n\t\tif (KAS3_UNLIKELY(c->failing)) goto failed;\n";
			out += "\t\t" + (ir::is_held(held) ? "put(c, " + reg(handler.reg) + ", " + from_slot(held, "o") + ");" : reg(handler.reg) + " = " + from_slot(held, "o") + ";") +
					"\n\t\t" + target + "\n\t}\n";
		}
		out += "\tgoto failed;\n";
	}
	return out;
}

std::string FunctionWriter::epilogue() const {
	std::string out = "\tgoto done;\ndone:\n";
	if (const std::string back = kept_written_back(NONE); !back.empty()) {
		out += "\t" + back + "\n";
	}
	for (uint32_t i = 0; i < function_.registers.size(); i++) {
		if (owns(i)) {
			out += "\trelease(c, " + reg(i) + ");\n";
		}
	}
	if (keeps_stack_) {
		out += "\tc->frames = frame.below;\n";
	}
	if (fails_) {
		out += "\tc->depth--;\n";
	}
	const bool keeps = std::any_of(function_.handlers.begin(), function_.handlers.end(), [](const ir::Handler &p_each) { return p_each.rethrows; });
	if (keeps) {
		out += "\tforget_kept(c);\n";
	}
	out += function_.result != Kind::Void ? "\treturn result;\n" : "\treturn;\n";
	out += handlers();
	if (fails_) {
		// The function behind the library's methods read by name is no place in the program: no line of it.
		const bool hidden = index_ == module_.library_function;
		out += "failed:\n" + (hidden ? std::string() : "\tleave_failed(c, " + std::to_string(index_) + ", line, traced);\n") + "\tgoto done;\n";
	}
	return out;
}

std::string FunctionWriter::write() {
	std::vector<std::string> pieces;
	std::vector<bool> landed = targets_; // Where code is reached from elsewhere, nothing is known.
	for (const ir::Handler &handler : function_.handlers) {
		landed[handler.target] = true;
	}
	whole_.assign(function_.registers.size(), Whole());
	text_.assign(function_.registers.size(), NONE);
	text_exact_.assign(function_.registers.size(), false);
	std::vector<std::string> copies(function_.code.size() + 1); // Before an instruction: a loop's copy with locals.
	for (pc_ = 0; pc_ < function_.code.size(); pc_++) {
		for (const Twice &loop : twice_) {
			if (loop.from == pc_) {
				copies[pc_] = fast_copy(loop, landed);
			}
		}
		body_.clear();
		if (landed[pc_]) {
			whole_.assign(function_.registers.size(), Whole());
			text_.assign(function_.registers.size(), NONE);
			text_exact_.assign(function_.registers.size(), false);
		}
		instruction(function_.code[pc_]);
		pieces.push_back(body_);
	}
	// A label is written only where something goes to it: a handler none of whose instructions can fail
	// is never reached.
	std::vector<bool> labelled = targets_;
	for (const std::vector<uint32_t> &chain : chains_) {
		for (uint32_t each : chain) {
			labelled[function_.handlers[each].target] = true;
		}
	}
	body_.clear();
	for (size_t pc = 0; pc <= function_.code.size(); pc++) {
		body_ += copies[pc];
		if (labelled[pc]) {
			// A counted loop's own jump back goes past its start: the label is for whatever else comes here.
			body_ += (rounds_counted(static_cast<uint32_t>(pc)) ? "RL" + std::to_string(pc) + ":;\n" : std::string()) +
					(counts(static_cast<uint32_t>(pc)) ? "[[maybe_unused]] " : "") + "L" + std::to_string(pc) + ":;\n";
		}
		if (retried_[pc]) {
			body_ += "S" + std::to_string(pc) + ":;\n";
		}
		if (pc < pieces.size()) {
			body_ += pieces[pc];
		}
	}
	const Signature signature = signature_of(function_);
	std::string head = declared(function_.result, function_name(module_, index_)) + "(Context *c";
	std::string passed = "c";
	for (uint32_t i = 0; i < function_.parameters; i++) {
		const std::string given = "a[" + std::to_string(i) + "]";
		head += ", " + (parts(i).empty() ? declared(kind(i), reg(i)) : std::string(parts(i)) + " " + reg(i));
		passed += ", " + (parts(i).empty() ? from_slot(kind(i), given) : "unbox<" + std::string(parts(i)) + ">(c, " + given + ".p)");
	}
	if (signature.counted) {
		head += ", uint32_t n";
		passed += ", n";
	}
	const std::string file = function_.file < module_.files.size() ? module_.files[function_.file] : std::string("?");
	std::string out = "#line " + std::to_string(std::max<uint32_t>(1, line_of(module_, function_.file, function_.position))) + " " +
			string_literal(file) + "\n";
	out += head + ") {\n" + prologue() + body_ + epilogue() + "}\n\n";
	// The same function for a caller that knows it only by its number: its arguments as slots.
	const std::string call = function_name(module_, index_) + "(" + passed + ")";
	out += "void " + entry_name(index_) + "(Context *c, const Slot *a, uint32_t n, Slot *r) {\n\t(void)a; (void)n; (void)r;\n";
	if (!fails_) {
		out += "\tif (KAS3_UNLIKELY(c->depth >= MAX_DEPTH)) { stack_overflow(c); return; }\n";
	}
	out += function_.result == Kind::Void ? "\t" + call + ";\n" : "\t*r = slot(" + call + ");\n";
	return out + "}\n\n";
}

} // namespace backend_cpp
} // namespace kas3
