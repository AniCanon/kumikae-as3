// What asks Godot, where generated code does it itself. Three kinds of thing:
//
//   - A value with parts (a Vector2) is the runtime's struct, inline in a local, and what the runtime has
//     Godot's formula for is that formula (runtime/godot_values.h).
//   - A property read or written and a method called on one of Godot's objects, when every value that
//     crosses is one Godot takes bare (a number, a Boolean, a value with parts, a name the program wrote):
//     the method bind the host found once is called on the values' addresses.
//   - The script's own Godot object and a singleton are not handles made anew for each use: the first is
//     found once as the function begins, the second is the host's.
//
// Whatever does not fit is the instruction the library runs, by its thunk: a value held inline is boxed
// into a handle for as long as that takes (FunctionWriter::generic).
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

// An operation on values with parts that the runtime does inline: the instruction and Godot's name for
// what it asks, what each operand must be (a type with parts, "float" for a Number, "number" for any
// number), what it gives, and the expression ($0, $1: the operands).
struct InlineOperation {
	Op op;
	std::string_view key;
	std::string_view operands[2];
	std::string_view result;
	std::string_view text;
};

constexpr InlineOperation INLINE[] = {
	{ Op::HostNew, "Vector2", { "number", "number" }, "Vector2", "vector2($0, $1)" },
	{ Op::HostOperate, "@operator.OP_ADD", { "Vector2", "Vector2" }, "Vector2", "add($0, $1)" },
	{ Op::HostOperate, "@operator.OP_SUBTRACT", { "Vector2", "Vector2" }, "Vector2", "subtract($0, $1)" },
	{ Op::HostOperate, "@operator.OP_MULTIPLY", { "Vector2", "float" }, "Vector2", "multiply($0, $1)" },
	{ Op::HostOperate, "@operator.OP_DIVIDE", { "Vector2", "float" }, "Vector2", "divide($0, $1)" },
	{ Op::HostCall, "Vector2.normalized", { "Vector2", "" }, "Vector2", "normalized($0)" },
	{ Op::HostCall, "Vector2.length", { "Vector2", "" }, "float", "length($0)" },
	{ Op::HostCall, "Vector2.dot", { "Vector2", "Vector2" }, "float", "dot($0, $1)" },
	{ Op::HostOperate, "@operator.OP_ADD", { "Vector3", "Vector3" }, "Vector3", "add($0, $1)" },
	{ Op::HostOperate, "@operator.OP_SUBTRACT", { "Vector3", "Vector3" }, "Vector3", "subtract($0, $1)" },
	{ Op::HostOperate, "@operator.OP_MULTIPLY", { "Vector3", "float" }, "Vector3", "multiply($0, $1)" },
	{ Op::HostOperate, "@operator.OP_DIVIDE", { "Vector3", "float" }, "Vector3", "divide($0, $1)" },
	{ Op::HostCall, "Vector3.normalized", { "Vector3", "" }, "Vector3", "normalized($0)" },
	{ Op::HostCall, "Vector3.length", { "Vector3", "" }, "float", "length($0)" },
	{ Op::HostCall, "Vector3.dot", { "Vector3", "Vector3" }, "float", "dot($0, $1)" },
	{ Op::HostCall, "Vector3.cross", { "Vector3", "Vector3" }, "Vector3", "cross($0, $1)" },
	{ Op::HostGet, "Vector2.x", { "Vector2", "" }, "float", "static_cast<double>($0.x)" },
	{ Op::HostGet, "Vector2.y", { "Vector2", "" }, "float", "static_cast<double>($0.y)" },
};

// A part of a value held inline, written in place: Godot's name for the part, the type, the member.
struct InlinePart {
	std::string_view key;
	std::string_view type;
	std::string_view member;
};
constexpr InlinePart PARTS_WRITTEN[] = {
	{ "Vector2.x", "Vector2", "x" },
	{ "Vector2.y", "Vector2", "y" },
};

bool whole(Kind p_kind) {
	return p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::I64;
}

// Whether Godot's name for a type is a whole number as its entries take one.
bool whole_type(std::string_view p_type) {
	return p_type == "int" || p_type.rfind("enum::", 0) == 0 || p_type.rfind("bitfield::", 0) == 0;
}

} // namespace

// Whether a register holds what an operand or a result of the type must be, as the register itself.
bool FunctionWriter::holds(uint32_t p_register, std::string_view p_type) const {
	const Kind held = kind(p_register);
	if (p_type == "float") {
		return held == Kind::F64;
	}
	if (p_type == "number") {
		return held == Kind::F64 || whole(held);
	}
	if (p_type == "bool") {
		return held == Kind::Bool;
	}
	if (whole_type(p_type)) {
		return held == Kind::I64;
	}
	return has_parts(p_type) && parts(p_register) == p_type;
}

bool FunctionWriter::holds_handle(uint32_t p_register, std::string_view p_type) const {
	return (kind(p_register) == Kind::Val || kind(p_register) == Kind::Ref) && parts(p_register).empty() && p_register < function_.engine_types.size() &&
			function_.engine_types[p_register] == p_type;
}

void FunctionWriter::find_inline() {
	inline_.assign(function_.registers.size(), false);
	// Every type with parts: what Godot does with one it does on the bare value (function_values.cpp).
	for (uint32_t i = 0; i < function_.registers.size(); i++) {
		inline_[i] = !parts_of(function_, i).empty();
	}
	for (const ir::Handler &handler : function_.handlers) {
		inline_[handler.reg] = false;
	}
	// One that nothing writes is read as it starts, which is null.
	for (uint32_t i = 0; i < function_.registers.size(); i++) {
		inline_[i] = inline_[i] && written_[i];
	}
	// Taken away until nothing changes: a move from a register that is not inline makes its target not.
	for (bool changed = true; changed;) {
		changed = false;
		for (const Instruction &in : function_.code) {
			const char first = ir::op_shape(in.op)[0];
			const bool writes = (first == 'R' || first == 'r') && in.a != NONE && !first_is_read(in.op);
			if (!writes || !inline_[in.a]) {
				continue;
			}
			// A vector of a type a slot holds whole has no null place: an element read is a value.
			const bool element = in.op == Op::VecGet && ir::bare_named(parts_of(function_, in.a)) != ir::Bare::None;
			const bool gives = in.op == Op::HostNew || in.op == Op::HostOperate || in.op == Op::HostGet || in.op == Op::HostCall ||
					in.op == Op::HostConstant || in.op == Op::HostCallStatic || (in.op == Op::Move && inline_[in.b]) || element;
			if (!gives) {
				inline_[in.a] = false;
				changed = true;
			}
		}
	}
}

void FunctionWriter::find_aliases() {
	alias_.assign(function_.registers.size(), Alias::None);
	alias_name_.assign(function_.registers.size(), NONE);
	const bool script = function_.owner != NONE && !function_.is_static && function_.parameters > 0 && !written_[0] &&
			!module_.classes[function_.owner].native_base.empty();
	std::vector<bool> other(function_.registers.size(), false);
	for (const Instruction &in : function_.code) {
		const char first = ir::op_shape(in.op)[0];
		const bool writes = (first == 'R' || first == 'r') && in.a != NONE && !first_is_read(in.op);
		if (!writes || kind(in.a) != Kind::Ref || in.a < function_.parameters) {
			continue;
		}
		Alias found = Alias::None;
		if (in.op == Op::GetField && script && in.b == 0 && in.c == 0) {
			found = Alias::Owner;
		} else if (in.op == Op::HostSingleton && (alias_name_[in.a] == NONE || alias_name_[in.a] == in.b)) {
			found = Alias::Singleton;
			alias_name_[in.a] = in.b;
		}
		// Every instruction that writes the register must write the same borrowed thing.
		other[in.a] = other[in.a] || found == Alias::None || (alias_[in.a] != Alias::None && alias_[in.a] != found);
		alias_[in.a] = found;
	}
	for (const ir::Handler &handler : function_.handlers) {
		other[handler.reg] = true;
	}
	for (uint32_t i = 0; i < alias_.size(); i++) {
		alias_[i] = other[i] ? Alias::None : alias_[i];
		has_owner_alias_ = has_owner_alias_ || alias_[i] == Alias::Owner;
	}
	asked_.assign(function_.registers.size(), false);
	for (const Instruction &in : function_.code) {
		const uint32_t on = in.op == Op::HostGet ? in.b : in.op == Op::HostSet ? in.a :
				in.op == Op::HostCall && !function_.argument_lists[in.c].empty() ? function_.argument_lists[in.c][0] : NONE;
		if (on != NONE && kind(on) == Kind::Ref) {
			asked_[on] = true;
		}
		// And one handed to a method or a property that takes an object, which is given the object itself.
		for (const uint32_t given : in.op == Op::HostCall ? function_.argument_lists[in.c] : std::vector<uint32_t>{ in.op == Op::HostSet ? in.c : NONE }) {
			if (given != NONE && kind(given) == Kind::Ref) {
				asked_[given] = true;
			}
		}
	}
}

// The object a call on bare values is made on: found once for the script's own, the host's for a
// singleton, asked of the host for any other handle (null for none, or for one Godot has freed).
std::string FunctionWriter::object_behind(uint32_t p_register) {
	if (alias_[p_register] == Alias::Owner) {
		return "owner_object(c, r0, self, self_seen)";
	}
	if (alias_[p_register] == Alias::Singleton) {
		return "c->resolved[" + std::to_string(singleton_use_[p_register]) + "].object";
	}
	return "object_of(c, " + reg(p_register) + ")";
}

bool FunctionWriter::engine_alias(const Instruction &p_in) {
	if (p_in.op == Op::GetField && alias_[p_in.a] == Alias::Owner) {
		line(reg(p_in.a) + " = field<Ref>(r0, 0);");
		return true;
	}
	if (p_in.op != Op::HostSingleton || alias_[p_in.a] != Alias::Singleton) {
		return false;
	}
	const ThunkUse use = thunks_.use(function_, p_in);
	const uint32_t thunk = use.thunk;
	singleton_use_[p_in.a] = thunks_.use_index(thunk);
	std::string given;
	for (uint32_t each : use.given) {
		given += (given.empty() ? "" : ", ") + ("slot(" + reg(each) + ")");
	}
	line(reg(p_in.a) + " = Ref{ c->resolved[" + std::to_string(singleton_use_[p_in.a]) + "].handle };");
	// Without one the library is asked, which says what is wrong; what it gives, if anything, is not kept.
	mark();
	line("if (KAS3_UNLIKELY(!" + reg(p_in.a) + ".handle)) {");
	if (!given.empty()) {
		line("\tconst Slot t[] = { " + given + " };");
	}
	line("\tconst Slot o = thunk(c, " + std::to_string(thunk) + ", " + (given.empty() ? "nullptr" : "t") + ", nullptr);");
	line("\tif (KAS3_UNLIKELY(c->failing)) { " + fail() + " }");
	line("\trelease(c, Ref{ o.p });");
	line("}");
	return true;
}

bool FunctionWriter::engine_inline(const Instruction &p_in) {
	if ((p_in.op == Op::HostPresent || p_in.op == Op::ToAny) && !parts(p_in.b).empty()) {
		if (p_in.op == Op::HostPresent) {
			line(reg(p_in.a) + " = c->engine != nullptr;");
		} else {
			line("store_parts_any(c, " + reg(p_in.a) + ", " + reg(p_in.b) + ");");
		}
		return true;
	}
	// A value held inline moved to a register that holds the same: a plain copy, as of a number.
	if (p_in.op == Op::Move && !parts(p_in.a).empty() && parts(p_in.a) == parts(p_in.b)) {
		if (p_in.a != p_in.b) {
			line(reg(p_in.a) + " = " + reg(p_in.b) + ";");
		}
		return true;
	}
	if (p_in.op == Op::HostSet) {
		for (const InlinePart &each : PARTS_WRITTEN) {
			if (each.key == module_.engine_names[p_in.b].key && parts(p_in.a) == each.type && kind(p_in.c) == Kind::F64) {
				line(reg(p_in.a) + "." + std::string(each.member) + " = static_cast<float>(" + reg(p_in.c) + ");");
				return true;
			}
		}
		return false;
	}
	const bool listed = p_in.op == Op::HostNew || p_in.op == Op::HostOperate || p_in.op == Op::HostCall;
	if (!listed && p_in.op != Op::HostGet) {
		return false;
	}
	if (p_in.a == NONE) {
		return false;
	}
	const std::string_view key = module_.engine_names[listed ? p_in.b : p_in.c].key;
	const std::vector<uint32_t> operands = listed ? function_.argument_lists[p_in.c] : std::vector<uint32_t>{ p_in.b };
	for (const InlineOperation &each : INLINE) {
		const size_t wanted = each.operands[1].empty() ? 1 : 2;
		if (each.op != p_in.op || each.key != key || operands.size() != wanted || !holds(p_in.a, each.result)) {
			continue;
		}
		// An operand may be a register that stands for a field's handle (value_from_): the value is copied
		// out of the handle first, and a field that holds none is the library's to say so.
		const auto lent = [&](uint32_t p_register, std::string_view p_type) {
			return has_parts(p_type) && value_type(p_register) == p_type && (value_from_[p_register] != NONE || holds_handle(p_register, p_type));
		};
		const bool fit = std::equal(operands.begin(), operands.end(), each.operands, [&](uint32_t p_register, std::string_view p_type) {
			return holds(p_register, p_type) || lent(p_register, p_type);
		});
		if (!fit) {
			continue;
		}
		std::string text(each.text);
		std::string guard, copies;
		for (size_t i = 0; i < operands.size(); i++) {
			const std::string name = "$" + std::to_string(i);
			std::string value = each.operands[i] == "number" ? "static_cast<double>(" + reg(operands[i]) + ")" : reg(operands[i]);
			if (!holds(operands[i], each.operands[i])) {
				const auto field = value_from_[operands[i]];
				const std::string handle = (field == NONE ? reg(operands[i]) : own_field(field)) + ".handle";
				value = "a" + std::to_string(i);
				guard += (guard.empty() ? "" : " && ") + handle;
				copies += "\tconst " + std::string(each.operands[i]) + " " + value + " = unbox<" + std::string(each.operands[i]) + ">(c, " + handle + ");\n";
			}
			for (size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + value.size())) {
				text.replace(at, name.size(), value);
			}
		}
		if (guard.empty()) {
			line(reg(p_in.a) + " = " + text + ";");
			return true;
		}
		line("if (KAS3_LIKELY(" + guard + ")) {");
		body_ += copies;
		line(reg(p_in.a) + " = " + text + ";");
		line("} else");
		generic(p_in);
		return true;
	}
	return false;
}

// A value as Godot's entry takes it bare, by its address: the register's own where it holds that very
// type, a temporary where a number is widened or a Boolean is Godot's byte or a value with parts is copied
// out of a handle, the name the host built for a String the program wrote. Nothing if the register does not
// hold what the parameter takes.
std::optional<std::string> FunctionWriter::bare_argument(uint32_t p_register, std::string_view p_type, size_t p_at, std::string *r_guard,
		std::string *r_inside) {
	const Kind held = kind(p_register);
	const std::string temporary = "a" + std::to_string(p_at);
	if (p_type == "float" && (held == Kind::F64 || held == Kind::I32 || held == Kind::U32)) {
		line("const double " + temporary + " = static_cast<double>(" + reg(p_register) + ");");
		return "&" + temporary;
	}
	if (whole_type(p_type) && whole(held)) {
		line("const int64_t " + temporary + " = static_cast<int64_t>(" + reg(p_register) + ");");
		return "&" + temporary;
	}
	if (p_type == "bool" && held == Kind::Bool) {
		line("const uint8_t " + temporary + " = " + reg(p_register) + ";");
		return "&" + temporary;
	}
	if (has_parts(p_type) && value_type(p_register) == p_type) {
		if (!parts(p_register).empty()) {
			return "&" + reg(p_register);
		}
		const std::string handle = (value_from_[p_register] != NONE ? own_field(value_from_[p_register]) : reg(p_register)) + ".handle";
		*r_guard += " && " + handle;
		*r_inside += "\tconst " + std::string(p_type) + " " + temporary + " = unbox<" + std::string(p_type) + ">(c, " + handle + ");\n";
		return "&" + temporary;
	}
	// One of Godot's objects, where the parameter is of exactly the class the register is declared to hold:
	// the object behind the handle, which is none for no handle and for an object Godot has freed, as the
	// library gives Godot null for both.
	if (held == Kind::Ref && holds_handle(p_register, p_type)) {
		line("void *const " + temporary + " = " + object_behind(p_register) + ";");
		return "&" + temporary;
	}
	if (p_type == "StringName" && held == Kind::Str && text_[p_register] != NONE) {
		const std::string name = "c->names[" + std::to_string(thunks_.name_index(text_[p_register])) + "]";
		if (!text_exact_[p_register]) {
			*r_guard += " && " + reg(p_register) + " == c->strings[" + std::to_string(text_[p_register]) + "]";
		}
		*r_guard += " && " + name;
		return name;
	}
	return std::nullopt;
}

bool FunctionWriter::engine_bound(const Instruction &p_in) {
	uint32_t receiver = NONE, name = NONE, out = NONE;
	std::vector<uint32_t> given;
	std::vector<std::string_view> parameters;
	std::string_view result = "void";
	if (p_in.op == Op::HostGet || p_in.op == Op::HostSet) {
		receiver = p_in.op == Op::HostGet ? p_in.b : p_in.a;
		name = p_in.op == Op::HostGet ? p_in.c : p_in.b;
		const std::vector<std::string_view> words = words_of(module_.engine_names[name].value);
		if (words.size() != 8 || words[1] != "property" || words[7] != "-") {
			return false;
		}
		if (p_in.op == Op::HostGet) {
			out = p_in.a;
			result = words[2];
		} else {
			given.push_back(p_in.c);
			parameters.push_back(words[2]);
		}
	} else if (p_in.op == Op::HostCall) {
		const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
		name = p_in.b;
		const std::vector<std::string_view> words = words_of(module_.engine_names[name].value);
		if (list.empty() || words.size() != 5 || words[1] != "method") {
			return false;
		}
		receiver = list[0];
		given.assign(list.begin() + 1, list.end());
		for (size_t at = 0; words[4] != "-" && at <= words[4].size();) {
			const size_t comma = std::min(words[4].find(',', at), words[4].size());
			parameters.push_back(words[4].substr(at, comma - at));
			at = comma + 1;
		}
		out = p_in.a;
		result = words[3];
	} else {
		return false;
	}
	const size_t missing = parameters.size() - std::min(parameters.size(), given.size());
	const bool gives = result != "void";
	// What comes back in a handle is made where a new handle keeps it, by the host (EngineApi::call_kept).
	const bool kept = in_handle(result);
	const bool math_handle = out != NONE && has_parts(result) && holds_handle(out, result);
	if (kind(receiver) != Kind::Ref || given.size() > parameters.size() || missing > 4 ||
			(out != NONE && (!gives || !(kept || math_handle ? holds_handle(out, result) : holds(out, result)))) ||
			(gives && !(result == "float" || result == "bool" || whole_type(result) || has_parts(result) || kept))) {
		return false;
	}
	// The temporaries are written into a piece of their own: if an argument does not fit, nothing was.
	const std::string before = body_;
	const uint32_t use = thunks_.use_index(thunks_.use(function_, p_in).thunk);
	const std::string resolved = "c->resolved[" + std::to_string(use) + "]";
	line("{");
	const bool repeated = std::any_of(counted_.begin(), counted_.end(), [&](const Counted &p_loop) { return p_loop.from <= pc_ && pc_ <= p_loop.to; });
	line("void *const bind = " + (repeated ? "bind" + std::to_string(use) : resolved + ".bind") + ";");
	line("void *const on = " + object_behind(receiver) + ";");
	std::string guard = "bind && on";
	std::string inside;
	std::string addresses;
	for (size_t i = 0; i < parameters.size(); i++) {
		std::optional<std::string> address = i < given.size() ? bare_argument(given[i], parameters[i], i, &guard, &inside)
															   : std::optional<std::string>("&" + resolved + ".defaults[" + std::to_string(i - given.size()) + "]");
		if (!address) {
			body_ = before;
			return false;
		}
		addresses += (i ? ", " : "") + *address;
	}
	if (repeated) {
		loop_binds_.insert(use);
	}
	line("if (KAS3_LIKELY(" + guard + (kept ? " && " + resolved + ".handle" : "") + ")) {");
	body_ += inside;
	if (!parameters.empty()) {
		line("const void *const p[] = { " + addresses + " };");
	}
	const std::string type = result == "float" ? "double" : result == "bool" ? "uint8_t" : whole_type(result) ? "int64_t" : std::string(result);
	const std::string given_to = "(bind, on, " + std::string(parameters.empty() ? "nullptr" : "p") + ", ";
	if (kept) {
		line("const Val v{ c->engine->call_kept" + given_to + resolved + ".handle) };");
	} else {
		if (gives) {
			line(type + " v{};");
		}
		line("c->engine->call" + given_to + (gives ? "&v" : "nullptr") + ");");
	}
	// What Godot ran meanwhile may have been a listener of the program's, and that may have raised.
	line("if (KAS3_UNLIKELY(c->listener_failed) && call_failed(c)) { " + std::string(kept ? "release(c, v); " : "") + fail() + " }");
	if (kept) {
		line(out != NONE ? "put(c, " + reg(out) + ", v);" : "release(c, v);");
	} else if (math_handle) {
		line("store_parts(c, " + reg(out) + ", v);");
	} else if (out != NONE) {
		line(reg(out) + " = " + (result == "bool" ? "v != 0" : "v") + ";");
	}
	line("} else");
	generic(p_in);
	line("}");
	return true;
}

bool FunctionWriter::engine(const Instruction &p_in) {
	return engine_alias(p_in) || engine_inline(p_in) || engine_emit(p_in) || engine_value(p_in) || engine_bound(p_in);
}

} // namespace backend_cpp
} // namespace kas3
