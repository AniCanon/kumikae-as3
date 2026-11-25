// Typed method binds whose arguments or result need conversion by the engine host.
#include "function_writer.h"
#include "names.h"

namespace kas3 {
namespace backend_cpp {

using ir::Kind;
using ir::NONE;

bool FunctionWriter::engine_method(const ir::Instruction &p_in) {
	if (p_in.op != ir::Op::HostCall) {
		return false;
	}
	const auto &list = function_.argument_lists[p_in.c];
	const auto words = words_of(module_.engine_names[p_in.b].value);
	if (list.empty() || kind(list[0]) != Kind::Ref || words.size() != 5 || words[1] != "method" || words[2] == "-") {
		return false;
	}
	const auto parameters = parameter_types(words[4]);
	const size_t count = parameters.size();
	const size_t given = list.size() - 1;
	if (given > count || count - given > 4 || count > 32) {
		return false;
	}
	const size_t start = body_.size();
	line("{");
	const std::string use = "c->resolved[$U]";
	uint32_t handles = 0;
	std::string addresses, guard = use + ".method", inside;
	for (size_t i = 1; i < list.size(); ++i) {
		const auto type = parameters[i - 1];
		if (type == "Variant") {
			body_.resize(start);
			return false;
		}
		const uint32_t arg = list[i];
		std::optional<std::string> address;
		if (kind(arg) == Kind::Str && (type == "String" || type == "StringName" || type == "NodePath")) {
			address = reg(arg);
		} else if (kind(arg) == Kind::Val && holds_handle(arg, type)) {
			handles |= uint32_t{ 1 } << (i - 1);
			address = (value_from_[arg] != NONE ? own_field(value_from_[arg]) : reg(arg)) + ".handle";
		} else if (kind(arg) == Kind::Ref && arg < function_.engine_types.size() && !function_.engine_types[arg].empty()) {
			// The host checks the declared argument class against the method's parameter on load.
			const auto local = "a" + std::to_string(i);
			line("void *const " + local + " = " + object_behind(arg) + ";");
			address = "&" + local;
		} else if (kind(arg) == Kind::U8 && type == "int") {
			const auto local = "a" + std::to_string(i);
			line("const int64_t " + local + " = " + reg(arg) + ";");
			address = "&" + local;
		} else {
			address = bare_argument(arg, type, i, &guard, &inside);
		}
		if (!address) {
			body_.resize(start);
			return false;
		}
		addresses += (i == 1 ? "" : ", ") + *address;
	}
	for (size_t i = given; i < count; ++i) {
		addresses += (i ? ", " : "") + ("&" + use + ".defaults[" + std::to_string(i - given) + "]");
	}
	const auto out = p_in.a;
	const bool inline_result = out != NONE && !parts(out).empty();
	line("if (KAS3_LIKELY(" + guard + ")) {");
	body_ += inside;
	if (!addresses.empty()) {
		line("const void *p[] = { " + addresses + " };");
	}
	if (inline_result) {
		line(std::string(parts(out)) + " v{};");
	}
	line("Slot result{};");
	mark();
	line("c->engine->method(c, " + use + ", " + reg(list[0]) + ", " + object_behind(list[0]) + ", " +
			(addresses.empty() ? "nullptr" : "p") + ", " + std::to_string(handles) + "u, &result, " + (inline_result ? "&v" : "nullptr") + ");");
	const std::string owned = out == NONE ? "" : from_slot(kind(out), "result");
	const std::string drop = out != NONE && ir::is_held(kind(out)) && !inline_result ? "release(c, " + owned + "); " : "";
	line("if (KAS3_UNLIKELY((c->listener_failed && call_failed(c)) || c->failing)) { " + drop + fail() + " }");
	if (inline_result) {
		line(reg(out) + " = v;");
	} else if (out != NONE) {
		put(out, owned);
	}
	line("} else");
	generic(p_in);
	line("}");
	settle_use(p_in, start);
	return true;
}

} // namespace backend_cpp
} // namespace kas3
