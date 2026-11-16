#include "function_writer.h"

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
		signature.parameters.push_back(std::string(type_of(p_function.registers[i])));
	}
	signature.counted = takes_count(p_function);
	if (signature.counted) {
		signature.parameters.push_back("uint32_t");
	}
	return signature;
}

FunctionWriter::FunctionWriter(const ir::Module &p_module, uint32_t p_function, ThunkTable &r_thunks, bool p_keeps_stack) :
		module_(p_module), index_(p_function), function_(p_module.functions[p_function]), thunks_(r_thunks), keeps_stack_(p_keeps_stack) {
	analyse();
}

void FunctionWriter::analyse() {
	targets_.assign(function_.code.size() + 1, false);
	written_.assign(function_.registers.size(), false);
	for (const Instruction &in : function_.code) {
		if (in.op == Op::Jump) {
			targets_[in.a] = true;
		} else if (in.op == Op::JumpIf || in.op == Op::JumpIfNot) {
			targets_[in.b] = true;
		}
		const char first = ir::op_shape(in.op)[0];
		if ((first == 'R' || first == 'r') && in.a != NONE && !first_is_read(in.op)) {
			written_[in.a] = true;
		}
		if (in.op == Op::DynNext) {
			written_[in.b] = true;
		}
	}
	for (const ir::Handler &handler : function_.handlers) {
		written_[handler.reg] = true;
	}
}

std::string FunctionWriter::reg(uint32_t p_register) const {
	return register_name(p_register);
}

bool FunctionWriter::owns(uint32_t p_register) const {
	return ir::is_held(kind(p_register)) && (p_register >= function_.parameters || written_[p_register]);
}

bool FunctionWriter::is_this(uint32_t p_register) const {
	return p_register == 0 && function_.owner != NONE && !function_.is_static && function_.parameters > 0 && !written_[0];
}

void FunctionWriter::line(const std::string &p_text) {
	body_ += "\t" + p_text + "\n";
}

void FunctionWriter::put(uint32_t p_register, const std::string &p_owned) {
	if (ir::is_held(kind(p_register))) {
		line("put(c, " + reg(p_register) + ", " + p_owned + ");");
	} else {
		line(reg(p_register) + " = " + p_owned + ";");
	}
}

void FunctionWriter::share(uint32_t p_register, const std::string &p_borrowed) {
	if (ir::is_held(kind(p_register))) {
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
	return "line = " + std::to_string(at) + "; goto " + (chain == NONE ? std::string("failed") : "H" + std::to_string(chain)) + ";";
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
	line("if (KAS3_UNLIKELY(!" + reg(p_register) + ")) { raise_null(c); " + fail() + " }");
}

void FunctionWriter::by_thunk(const Instruction &p_in) {
	const ThunkUse use = thunks_.use(function_, p_in);
	mark();
	line("{");
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
			(use.updated != NONE ? ", &u" : "") + ");");
	check();
	if (use.updated != NONE) {
		put(use.updated, from_slot(kind(use.updated), "u"));
	}
	if (use.result != NONE) {
		put(use.result, from_slot(kind(use.result), "o"));
	}
	line("}");
}

void FunctionWriter::instruction(const Instruction &p_in) {
	const bool written = whole_conversion(p_in) || constant(p_in) || simple(p_in) || object(p_in) || flow(p_in) || call(p_in) || fast_vector(p_in) ||
			fast_text(p_in) || fast_untyped(p_in) || fast_call(p_in);
	if (!written) {
		by_thunk(p_in);
	}
	// What was known of a register is forgotten when it is written, and with it what was made from it.
	const char first = ir::op_shape(p_in.op)[0];
	if ((first == 'R' || first == 'r') && p_in.a != NONE && !first_is_read(p_in.op)) {
		forget_whole(p_in.a);
	}
	if (p_in.op == Op::DynNext) {
		forget_whole(p_in.b);
	}
	remember_whole(p_in);
}

std::string FunctionWriter::prologue() const {
	std::string out;
	const auto add = [&](const std::string &p_text) { out += "\t" + p_text + "\n"; };
	std::string unused = "(void)c;";
	for (uint32_t i = 0; i < function_.registers.size(); i++) {
		if (i >= function_.parameters) {
			add(declared(kind(i), reg(i)) + " = " + std::string(zero_of(kind(i))) + ";");
		}
		unused += " (void)" + reg(i) + ";";
	}
	if (takes_count(function_)) {
		unused += " (void)n;";
	}
	if (fails_) {
		add("uint32_t line = 0;");
		add("bool traced = false;");
		unused += " (void)traced;";
	}
	add(unused);
	const bool gives = function_.result != Kind::Void;
	if (gives) {
		add(declared(function_.result, "result") + " = " + std::string(zero_of(function_.result)) + ";");
	}
	add(std::string("if (KAS3_UNLIKELY(c->depth >= MAX_DEPTH)) { stack_overflow(c); return") + (gives ? " result" : "") + "; }");
	add("c->depth++;");
	if (keeps_stack_) {
		add("StackFrame frame = { " + std::to_string(index_) + ", 0, c->frames };");
		add("c->frames = &frame;");
	}
	for (uint32_t i = 0; i < function_.parameters; i++) {
		if (owns(i)) {
			add("retain(c, " + reg(i) + ");");
		}
	}
	return out;
}

std::string FunctionWriter::handlers() const {
	std::string out;
	for (size_t i = 0; i < chains_.size(); i++) {
		out += "H" + std::to_string(i) + ":\n";
		for (uint32_t each : chains_[i]) {
			const ir::Handler &handler = function_.handlers[each];
			const Kind held = kind(handler.reg);
			const std::string error = "caught(c, " + std::to_string(index_) + ", line, " + std::to_string(handler.reg) + ", " +
					(handler.rethrows ? "true" : "false") + ", &traced)";
			const std::string target = "goto L" + std::to_string(handler.target) + ";";
			out += "\tif (catches(c, " + number(handler.type) + ")) {";
			if (held == Kind::Obj || held == Kind::Any) {
				out += " put(c, " + reg(handler.reg) + ", " + (held == Kind::Obj ? "object_of(" + error + ")" : error) + "); " + target + " }\n";
				continue;
			}
			// A catch of a String or a number: the value thrown as that kind, as a store makes it one.
			out += "\n\t\tconst Slot e = " + error + ";\n\t\tconst Slot o = thunk(c, " + std::to_string(thunks_.from_any(held)) + ", &e);\n";
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
	for (uint32_t i = 0; i < function_.registers.size(); i++) {
		if (owns(i)) {
			out += "\trelease(c, " + reg(i) + ");\n";
		}
	}
	if (keeps_stack_) {
		out += "\tc->frames = frame.below;\n";
	}
	out += "\tc->depth--;\n";
	const bool keeps = std::any_of(function_.handlers.begin(), function_.handlers.end(), [](const ir::Handler &p_each) { return p_each.rethrows; });
	if (keeps) {
		out += "\tforget_kept(c);\n";
	}
	out += function_.result != Kind::Void ? "\treturn result;\n" : "\treturn;\n";
	out += handlers();
	if (fails_) {
		out += "failed:\n\tleave_failed(c, " + std::to_string(index_) + ", line, traced);\n\tgoto done;\n";
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
	for (pc_ = 0; pc_ < function_.code.size(); pc_++) {
		body_.clear();
		if (landed[pc_]) {
			whole_.assign(function_.registers.size(), Whole());
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
		if (labelled[pc]) {
			body_ += "L" + std::to_string(pc) + ":;\n";
		}
		if (pc < pieces.size()) {
			body_ += pieces[pc];
		}
	}
	const Signature signature = signature_of(function_);
	std::string head = signature.result + " " + function_name(module_, index_) + "(Context *c";
	std::string passed = "c";
	for (uint32_t i = 0; i < function_.parameters; i++) {
		head += ", " + declared(kind(i), reg(i));
		passed += ", " + from_slot(kind(i), "a[" + std::to_string(i) + "]");
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
	out += function_.result == Kind::Void ? "\t" + call + ";\n" : "\t*r = slot(" + call + ");\n";
	return out + "}\n\n";
}

} // namespace backend_cpp
} // namespace kas3
