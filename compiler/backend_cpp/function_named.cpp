// A literal AS3 member name has its own cache. Computed names keep their ordinary lookup.
#include "function_writer.h"

namespace kas3::backend_cpp {

bool FunctionWriter::fast_named(const ir::Instruction &p_in) {
	using ir::Kind;
	using ir::Op;
	const bool write = p_in.op == Op::DynSet;
	if (!write && p_in.op != Op::DynGet && p_in.op != Op::DynCall) {
		return false;
	}
	const uint32_t holder = write ? p_in.a : p_in.b;
	const auto *list = p_in.op == Op::DynCall ? &function_.argument_lists[p_in.c] : nullptr;
	const uint32_t key = list ? (*list)[0] : write ? p_in.b : p_in.c;
	if (kind(holder) != Kind::Any || kind(key) != Kind::Str || text_[key] == ir::NONE || !text_exact_[key] || (list && list->size() > 5)) {
		return false;
	}
	std::string given = write ? reg(p_in.c) : "";
	for (size_t i = 1; list && i < list->size(); i++) {
		given += (i == 1 ? "" : ", ") + reg((*list)[i]);
	}
	const size_t count = list ? list->size() - 1 : write ? 1 : 0;
	line("{");
	line("static thread_local kas3::vm::NamedSite site{};");
	if (count) {
		line("const Slot args[] = { " + given + " };");
	}
	mark();
	line("Slot w = empty();");
	line("if (kas3::vm::any::is_engine(" + reg(holder) + ") && named_access(c, &site, " + std::to_string(static_cast<unsigned>(p_in.op)) +
			", " + reg(holder) + ", " + reg(key) + ", " + (count ? "args" : "nullptr") + ", " + std::to_string(count) + ", &w)) {");
	check();
	if (!write && p_in.a != ir::NONE) {
		put(p_in.a, "w");
	} else {
		line("release(c, w);");
	}
	line("} else {");
	if (!fast_untyped(p_in)) {
		by_thunk(p_in);
	}
	line("}");
	line("}");
	return true;
}

} // namespace kas3::backend_cpp
