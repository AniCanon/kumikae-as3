// Godot's value types that have parts, where generated code asks Godot's own code for the type with
// nothing between: an operator's evaluator, a method of the type, a part's getter or setter, each found
// once by the host (c->resolved) and called on the bare values, and a constant kept by the host. A value
// held inline is passed by its address; one held in a handle is copied out of it first. Where the host
// found nothing, or a value is missing, the same instruction is the library's by thunk.
//
// Making a value from its parts and reading or writing a part that is a member of the struct are written
// as they are (runtime/godot_values.h has the same layout as Godot).
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

// How a type's value is written from numbers: the numbers it takes, whether they are whole, and the
// braces ($0, $1: each as the member's own type).
struct FromNumbers {
	std::string_view type;
	size_t count;
	bool whole;
	std::string_view text;
};
constexpr FromNumbers FROM_NUMBERS[] = {
	{ "Vector2", 2, false, "{ $0, $1 }" },
	{ "Vector2i", 2, true, "{ $0, $1 }" },
	{ "Vector3", 3, false, "{ $0, $1, $2 }" },
	{ "Vector3i", 3, true, "{ $0, $1, $2 }" },
	{ "Vector4", 4, false, "{ $0, $1, $2, $3 }" },
	{ "Vector4i", 4, true, "{ $0, $1, $2, $3 }" },
	{ "Quaternion", 4, false, "{ $0, $1, $2, $3 }" },
	{ "Color", 4, false, "{ $0, $1, $2, $3 }" },
	{ "Color", 3, false, "{ $0, $1, $2, 1.0f }" },
	{ "Rect2", 4, false, "{ { $0, $1 }, { $2, $3 } }" },
	{ "Rect2i", 4, true, "{ { $0, $1 }, { $2, $3 } }" },
	{ "Plane", 4, false, "{ { $0, $1, $2 }, $3 }" },
	// With nothing given: zero, or what Godot starts the type as.
	{ "Vector2", 0, false, "{}" },
	{ "Vector2i", 0, true, "{}" },
	{ "Vector3", 0, false, "{}" },
	{ "Vector3i", 0, true, "{}" },
	{ "Vector4", 0, false, "{}" },
	{ "Vector4i", 0, true, "{}" },
	{ "Rect2", 0, false, "{}" },
	{ "Rect2i", 0, true, "{}" },
	{ "AABB", 0, false, "{}" },
	{ "Plane", 0, false, "{}" },
	{ "Color", 0, false, "{ 0, 0, 0, 1 }" },
	{ "Quaternion", 0, false, "{ 0, 0, 0, 1 }" },
	{ "Transform2D", 0, false, "{ { { 1, 0 }, { 0, 1 }, { 0, 0 } } }" },
	{ "Basis", 0, false, "{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } }" },
	{ "Transform3D", 0, false, "{ { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } }, { 0, 0, 0 } }" },
	{ "Projection", 0, false, "{ { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } }" },
};

// A value made of two others, member by member.
struct FromValues {
	std::string_view type;
	std::string_view parts[2];
};
constexpr FromValues FROM_VALUES[] = {
	{ "Rect2", { "Vector2", "Vector2" } },
	{ "Rect2i", { "Vector2i", "Vector2i" } },
	{ "AABB", { "Vector3", "Vector3" } },
	{ "Transform3D", { "Basis", "Vector3" } },
};

// A part that is a member of the struct under Godot's own name for it: the types, the members, and what a
// member is ("float", "int", or a type with parts).
struct Members {
	std::string_view types;
	std::string_view names;
	std::string_view is;
};
constexpr Members MEMBERS[] = {
	{ "Vector2 Vector3 Vector4 Quaternion", "x y z w", "float" },
	{ "Vector2i Vector3i Vector4i", "x y z w", "int" },
	{ "Color", "r g b a", "float" },
	{ "Rect2", "position size", "Vector2" },
	{ "Rect2i", "position size", "Vector2i" },
	{ "AABB", "position size", "Vector3" },
	{ "Plane", "normal", "Vector3" },
	{ "Plane", "d", "float" },
	{ "Transform3D", "basis", "Basis" },
	{ "Transform3D", "origin", "Vector3" },
};

bool listed(std::string_view p_words, std::string_view p_word) {
	const std::vector<std::string_view> words = words_of(p_words);
	return std::find(words.begin(), words.end(), p_word) != words.end();
}

// How many of x, y, z, w a type has: its later members are not every type's.
size_t members_of(std::string_view p_type) {
	return p_type.find('2') != std::string_view::npos ? 2 : p_type.find('3') != std::string_view::npos ? 3 : 4;
}

// What a part of a type is, if it is a member of the struct.
std::string_view member_is(std::string_view p_type, std::string_view p_member) {
	for (const Members &each : MEMBERS) {
		const std::vector<std::string_view> names = words_of(each.names);
		const auto found = std::find(names.begin(), names.end(), p_member);
		if (!listed(each.types, p_type) || found == names.end()) {
			continue;
		}
		const bool lettered = each.names == std::string_view("x y z w");
		if (!lettered || static_cast<size_t>(found - names.begin()) < members_of(p_type)) {
			return each.is;
		}
	}
	return {};
}

bool is_whole(Kind p_kind) {
	return p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::I64;
}

std::string replaced(std::string p_text, const std::vector<std::string> &p_values) {
	for (size_t i = 0; i < p_values.size(); i++) {
		const std::string name = "$" + std::to_string(i);
		for (size_t at = p_text.find(name); at != std::string::npos; at = p_text.find(name, at + p_values[i].size())) {
			p_text.replace(at, name.size(), p_values[i]);
		}
	}
	return p_text;
}

} // namespace

// Godot's name for the type of the value with parts a register holds, inline or in a handle; or nothing.
std::string_view FunctionWriter::value_type(uint32_t p_register) const {
	if (kind(p_register) != Kind::Val || p_register >= function_.engine_types.size()) {
		return {};
	}
	const std::string_view type = function_.engine_types[p_register];
	return has_parts(type) ? type : std::string_view();
}

// A register as Godot's code takes it bare, by its address: the register's own where it holds that very
// thing, a temporary for a whole number or a Boolean, and for a value in a handle its copy, made once the
// handle is known to be there (r_guard, r_inside). Nothing for a register that holds none of these.
std::optional<std::string> FunctionWriter::bare_operand(uint32_t p_register, size_t p_at, std::string *r_guard, std::string *r_inside) {
	const Kind held = kind(p_register);
	const std::string temporary = "a" + std::to_string(p_at);
	if (held == Kind::F64) {
		return "&" + register_name(p_register);
	}
	if (is_whole(held)) {
		line("const int64_t " + temporary + " = static_cast<int64_t>(" + register_name(p_register) + ");");
		return "&" + temporary;
	}
	if (held == Kind::Bool) {
		line("const uint8_t " + temporary + " = " + register_name(p_register) + ";");
		return "&" + temporary;
	}
	const std::string_view type = value_type(p_register);
	if (type.empty()) {
		return std::nullopt;
	}
	if (!parts(p_register).empty()) {
		return "&" + register_name(p_register);
	}
	// The register's handle, or the handle of the field the register stands for (value_from_).
	const std::string handle = (value_from_[p_register] != NONE ? own_field(value_from_[p_register]) : register_name(p_register)) + ".handle";
	*r_guard += " && " + handle;
	*r_inside += "\t" + std::string(type) + " " + temporary + " = unbox<" + std::string(type) + ">(c, " + handle + ");\n";
	return "&" + temporary;
}

// The call itself, between the test of what the host found and the thunk: `p_call` is written with $r for
// where the result goes.
bool FunctionWriter::value_call(const Instruction &p_in, uint32_t p_out, std::string_view p_result, const std::string &p_guard,
		const std::string &p_inside, const std::string &p_call) {
	// What comes back: a Number or an Int64 into the register itself, a Boolean through Godot's byte, a
	// value with parts into a register that holds one inline. Anything else is not written here.
	const Kind held = p_out == NONE ? Kind::Void : kind(p_out);
	const bool boolean = p_result == "bool";
	const bool fits = p_result == "void" || (p_out == NONE && (p_result == "float" || p_result == "int" || boolean || has_parts(p_result))) ||
			(p_result == "float" && held == Kind::F64) || (p_result == "int" && held == Kind::I64) || (boolean && held == Kind::Bool) ||
			(p_out != NONE && has_parts(p_result) && parts(p_out) == p_result);
	if (!fits) {
		return false;
	}
	const std::string type = p_result == "float" ? "double" : p_result == "int" ? "int64_t" : boolean ? "uint8_t" : std::string(p_result);
	line("if (KAS3_LIKELY(" + p_guard + ")) {");
	body_ += p_inside;
	if (p_result != "void") {
		// Through a local, so that a result is never written over an operand as it is being read.
		line(type + " v{};");
	}
	line(p_call + ";");
	if (p_out != NONE && p_result != "void") {
		line(register_name(p_out) + " = " + (boolean ? "v != 0" : "v") + ";");
	}
	line("} else");
	generic(p_in);
	line("}");
	return true;
}

bool FunctionWriter::value_made(const Instruction &p_in) {
	const std::string_view type = p_in.op == Op::HostNew ? parts(p_in.a) : std::string_view();
	if (type.empty() || type != module_.engine_names[p_in.b].key) {
		return false;
	}
	const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
	for (const FromNumbers &each : FROM_NUMBERS) {
		const bool numbers = std::all_of(list.begin(), list.end(), [&](uint32_t p_register) {
			return is_whole(kind(p_register)) || (!each.whole && kind(p_register) == Kind::F64);
		});
		if (each.type != type || each.count != list.size() || !numbers) {
			continue;
		}
		std::vector<std::string> values;
		for (uint32_t given : list) {
			values.push_back("static_cast<" + std::string(each.whole ? "int32_t" : "float") + ">(" + register_name(given) + ")");
		}
		line(register_name(p_in.a) + " = " + std::string(type) + replaced(std::string(each.text), values) + ";");
		return true;
	}
	for (const FromValues &each : FROM_VALUES) {
		if (each.type == type && list.size() == 2 && parts(list[0]) == each.parts[0] && parts(list[1]) == each.parts[1]) {
			line(register_name(p_in.a) + " = " + std::string(type) + "{ " + register_name(list[0]) + ", " + register_name(list[1]) + " };");
			return true;
		}
	}
	return false;
}

bool FunctionWriter::value_part(const Instruction &p_in) {
	if (p_in.op != Op::HostGet && p_in.op != Op::HostSet) {
		return false;
	}
	const bool reads = p_in.op == Op::HostGet;
	const uint32_t of = reads ? p_in.b : p_in.a;
	const ir::EngineName &entry = module_.engine_names[reads ? p_in.c : p_in.b];
	const std::vector<std::string_view> words = words_of(entry.value);
	const std::string_view type = value_type(of);
	if (type.empty() || type != entry.owner || words.size() != 3 || words[1] != "field") {
		return false;
	}
	const std::string_view is = words[2];
	const uint32_t value = reads ? p_in.a : p_in.c;
	const bool held_inline = !parts(of).empty();
	// A member of the struct, read or written where it is.
	if (held_inline && member_is(type, entry.name) == is) {
		const std::string member = register_name(of) + "." + entry.name;
		if (is == "float" && kind(value) == Kind::F64) {
			line(reads ? register_name(value) + " = static_cast<double>(" + member + ");" : member + " = static_cast<float>(" + register_name(value) + ");");
			return true;
		}
		// A whole part is an int in the struct: it is an Int64 to Godot, and fits an int register as it is.
		if (is == "int" && (reads ? kind(value) == Kind::I64 || kind(value) == Kind::I32 : is_whole(kind(value)))) {
			line(reads ? register_name(value) + " = " + member + ";" : member + " = static_cast<int32_t>(" + register_name(value) + ");");
			return true;
		}
		if (has_parts(is) && parts(value) == is) {
			line(reads ? register_name(value) + " = " + member + ";" : member + " = " + register_name(value) + ";");
			return true;
		}
	}
	if (!reads && !held_inline) {
		return false; // A value in a handle is changed where the handle is: the library's.
	}
	const std::string before = body_;
	line("{");
	line("void *const bind = c->resolved[$U].bind;");
	std::string guard = "bind";
	std::string inside;
	const std::optional<std::string> on = bare_operand(of, 0, &guard, &inside);
	const std::optional<std::string> given = reads ? std::optional<std::string>("&v") : bare_operand(value, 1, &guard, &inside);
	// A part that is a Number takes one: a whole number given for it is not what the setter reads.
	const bool right = reads || (is == "float" ? kind(value) == Kind::F64 : is == "int" ? is_whole(kind(value)) : has_parts(is) && value_type(value) == is);
	if (!on || !given || !right ||
			!value_call(p_in, reads ? value : NONE, reads ? is : std::string_view("void"), guard, inside,
					reads ? "get_part(bind, " + *on + ", &v)" : "set_part(bind, " + *on + ", " + *given + ")")) {
		body_ = before;
		return false;
	}
	settle_use(p_in, before.size());
	return true;
}

bool FunctionWriter::value_operation(const Instruction &p_in) {
	if (p_in.op != Op::HostOperate && p_in.op != Op::HostCall) {
		return false;
	}
	const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
	const std::string before = body_;
	line("{");
	line("void *const bind = c->resolved[$U].bind;");
	std::string guard = "bind";
	std::string inside;
	std::vector<std::string> operands;
	bool fits = !list.empty();
	for (size_t i = 0; fits && i < list.size(); i++) {
		const std::optional<std::string> operand = bare_operand(list[i], i, &guard, &inside);
		fits = operand.has_value();
		operands.push_back(operand.value_or(std::string()));
	}
	std::string_view result;
	std::string call;
	if (fits && p_in.op == Op::HostOperate) {
		// The evaluator is Godot's for these very types, which the host reads from the same registers: a
		// result that is a whole number is an Int64's.
		const bool any = std::any_of(list.begin(), list.end(), [&](uint32_t p_register) { return !value_type(p_register).empty(); });
		const Kind out = kind(p_in.a);
		result = out == Kind::Bool ? "bool" : out == Kind::F64 ? "float" : out == Kind::I64 ? "int" : parts(p_in.a);
		fits = any && list.size() <= 2 && !result.empty();
		call = "operate(bind, " + operands[0] + ", " + (list.size() == 2 ? operands[1] : std::string("nullptr")) + ", &v)";
	} else if (fits) {
		const std::vector<std::string_view> words = words_of(module_.engine_names[p_in.b].value);
		fits = words.size() == 5 && words[1] == "method" && value_type(list[0]) == module_.engine_names[p_in.b].owner;
		std::vector<std::string_view> parameters;
		for (size_t at = 0; fits && words[4] != "-" && at <= words[4].size();) {
			const size_t comma = std::min(words[4].find(',', at), words[4].size());
			parameters.push_back(words[4].substr(at, comma - at));
			at = comma + 1;
		}
		// Each argument must be what the method takes, as it is: Godot's code reads it by that type.
		fits = fits && parameters.size() == list.size() - 1;
		for (size_t i = 0; fits && i < parameters.size(); i++) {
			const Kind given = kind(list[i + 1]);
			fits = parameters[i] == "float" ? given == Kind::F64 : parameters[i] == "int" ? is_whole(given) : parameters[i] == "bool" ? given == Kind::Bool :
					has_parts(parameters[i]) && value_type(list[i + 1]) == parameters[i];
		}
		if (fits) {
			result = words[3];
			std::string given;
			for (size_t i = 1; i < operands.size(); i++) {
				given += (i > 1 ? ", " : "") + operands[i];
			}
			if (operands.size() > 1) {
				inside += "\tconst void *const p[] = { " + given + " };\n";
			}
			// A value in a handle is called on its copy: a method that changes its value is the library's.
			fits = !parts(list[0]).empty() || changed_in_place(function_, p_in) == NONE;
			call = "call_part(bind, " + operands[0] + ", " + (operands.size() > 1 ? "p" : "nullptr") + ", " + (result == "void" ? "nullptr" : "&v") + ", " + std::to_string(operands.size() - 1) + ")";
		}
	}
	if (!fits || !value_call(p_in, p_in.a, result, guard, inside, call)) {
		body_ = before;
		return false;
	}
	settle_use(p_in, before.size());
	return true;
}

// A method of one of Godot's values that a handle keeps (a Dictionary's `is_empty`), on the register's own
// handle: Godot's code for the type is called where the handle keeps the value, which is what the library
// calls it on (a method that changes the value changes it there, in the handle the register has). Its
// arguments are numbers, Booleans and values with parts, as the method takes them; what it gives is one of
// those. No handle, or one on a value of another type, is the instruction by its thunk.
bool FunctionWriter::kept_method(const Instruction &p_in) {
	if (p_in.op != Op::HostCall || function_.argument_lists[p_in.c].empty()) {
		return false;
	}
	const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
	const ir::EngineName &entry = module_.engine_names[p_in.b];
	const std::vector<std::string_view> words = words_of(entry.value);
	if (words.size() != 5 || words[1] != "method" || !in_handle(entry.owner) || kind(list[0]) != Kind::Val || !holds_handle(list[0], entry.owner)) {
		return false;
	}
	std::vector<std::string_view> parameters;
	for (size_t at = 0; words[4] != "-" && at <= words[4].size();) {
		const size_t comma = std::min(words[4].find(',', at), words[4].size());
		parameters.push_back(words[4].substr(at, comma - at));
		at = comma + 1;
	}
	const std::string before = body_;
	line("{");
	line("void *const bind = c->resolved[$U].bind;");
	line("void *const on = value_in(c->resolved[$U], " + register_name(list[0]) + ".handle);");
	std::string guard = "bind && on";
	std::string inside, given;
	bool fits = parameters.size() == list.size() - 1;
	for (size_t i = 0; fits && i < parameters.size(); i++) {
		const Kind held = kind(list[i + 1]);
		fits = parameters[i] == "float" ? held == Kind::F64 : parameters[i] == "int" ? is_whole(held) : parameters[i] == "bool" ? held == Kind::Bool :
				has_parts(parameters[i]) && value_type(list[i + 1]) == parameters[i];
		const std::optional<std::string> operand = fits ? bare_operand(list[i + 1], i + 1, &guard, &inside) : std::nullopt;
		fits = operand.has_value();
		given += (i ? ", " : "") + operand.value_or(std::string());
	}
	if (!parameters.empty()) {
		inside += "\tconst void *const p[] = { " + given + " };\n";
	}
	const std::string call = "call_part(bind, on, " + std::string(parameters.empty() ? "nullptr" : "p") + ", " + (words[3] == "void" ? "nullptr" : "&v") + ", " +
			std::to_string(parameters.size()) + ")";
	if (!fits || !value_call(p_in, p_in.a, words[3], guard, inside, call)) {
		body_ = before;
		return false;
	}
	settle_use(p_in, before.size());
	return true;
}

// `var at:Vector2 = found["position"]`, where found is one of Godot's Dictionaries, reads the element as an
// untyped value and then converts it: two instructions, a key made of the text, and two handles for a
// value that is wanted as one. Where the key is text the program wrote, the untyped register is read by
// that conversion and by nothing else, and the conversion is to a value with parts, the host is asked for
// the element as that type at once (EngineApi::keyed) and gives it in the handle the typed register takes.
// Whatever else it finds (no Dictionary, no such key, a value of another type) is the two instructions as
// they would be alone, which say what that is.
bool FunctionWriter::value_keyed(const Instruction &p_in) {
	if (p_in.op != Op::HostIndexGet || pc_ + 1 >= function_.code.size() || targets_[pc_ + 1]) {
		return false;
	}
	const Instruction &next = function_.code[pc_ + 1];
	const uint32_t untyped = p_in.a;
	const std::string_view type = next.op == Op::FromAny && next.a < function_.engine_types.size() ? std::string_view(function_.engine_types[next.a]) : "";
	if (next.op != Op::FromAny || next.b != untyped || kind(untyped) != Kind::Any || kind(next.a) != Kind::Val || !has_parts(type) || !parts(next.a).empty() ||
			kind(p_in.b) != Kind::Val || !holds_handle(p_in.b, "Dictionary") || kind(p_in.c) != Kind::Str || text_[p_in.c] == NONE) {
		return false;
	}
	uint32_t reads = 0;
	for (const Instruction &in : function_.code) {
		const char *shape = ir::op_shape(in.op);
		const uint32_t operands[3] = { in.a, in.b, in.c };
		const bool writes_first = (shape[0] == 'R' || shape[0] == 'r') && in.a != NONE && !first_is_read(in.op);
		for (int n = 0; n < 3; n++) {
			if (shape[n] == 'A') {
				const std::vector<uint32_t> &list = function_.argument_lists[operands[n]];
				reads += static_cast<uint32_t>(std::count(list.begin(), list.end(), untyped));
			} else if ((shape[n] == 'R' || shape[n] == 'r') && operands[n] == untyped && !(n == 0 && writes_first)) {
				reads++;
			}
		}
	}
	const bool caught = std::any_of(function_.handlers.begin(), function_.handlers.end(), [&](const ir::Handler &p_each) { return p_each.reg == untyped; });
	if (reads != 1 || caught) {
		return false;
	}
	const std::string name = "c->names[" + std::to_string(thunks_.name_index(text_[p_in.c])) + "]";
	line("{");
	line("void *const made = c->engine && " + name + " ? c->engine->keyed(" + register_name(p_in.b) + ".handle, " + name + ", PartsOf<" + std::string(type) +
			">::code) : nullptr;");
	line("if (KAS3_LIKELY(made)) {");
	put(untyped, std::string(zero_of(Kind::Any))); // What it held is let go of, as the element read into it would have.
	put(next.a, "Val{ made }");
	line("} else {");
	generic(p_in);
	pc_++;
	if (const std::string before = kept_written_back(pc_); !before.empty()) {
		line(before);
	}
	generic(next);
	if (const std::string after = kept_read_again(pc_); !after.empty()) {
		line(after);
	}
	pc_--;
	line("}");
	line("}");
	joined_ = pc_ + 1;
	return true;
}

// The place the host resolves is numbered once the instruction is known to be written this way: one that
// turned out not to be leaves no place the host would look into for nothing.
void FunctionWriter::settle_use(const Instruction &p_in, size_t p_from) {
	const std::string number = std::to_string(thunks_.use_index(thunks_.use(function_, p_in).thunk));
	for (size_t at = body_.find("$U", p_from); at != std::string::npos; at = body_.find("$U", at)) {
		body_.replace(at, 2, number);
	}
}

bool FunctionWriter::value_constant(const Instruction &p_in) {
	const std::string_view type = p_in.op == Op::HostConstant ? parts(p_in.a) : std::string_view();
	if (type.empty() || type != module_.engine_names[p_in.b].owner) {
		return false;
	}
	const std::string resolved = "c->resolved[" + std::to_string(thunks_.use_index(thunks_.use(function_, p_in).thunk)) + "]";
	line("if (KAS3_LIKELY(" + resolved + ".handle)) {");
	line("\t" + register_name(p_in.a) + " = unbox<" + std::string(type) + ">(c, " + resolved + ".handle);");
	line("} else");
	generic(p_in);
	return true;
}

bool FunctionWriter::engine_value(const Instruction &p_in) {
	return value_made(p_in) || value_part(p_in) || value_operation(p_in) || kept_method(p_in) || value_keyed(p_in) || value_constant(p_in);
}

} // namespace backend_cpp
} // namespace kas3
