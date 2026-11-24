// A literal uses its own site. The host bounds its cache for computed foreign reads.
#include "function_writer.h"

#include "names.h"

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
	const bool literal = kind(key) == Kind::Str && text_[key] != ir::NONE && text_exact_[key];
	if (kind(holder) != Kind::Any || (!literal && (p_in.op != Op::DynGet || (kind(key) != Kind::Str && kind(key) != Kind::Any))) ||
			(list && list->size() > 5)) {
		return false;
	}
	std::string given = write ? reg(p_in.c) : "";
	for (size_t i = 1; list && i < list->size(); i++) {
		given += (i == 1 ? "" : ", ") + reg((*list)[i]);
	}
	const size_t count = list ? list->size() - 1 : write ? 1 : 0;
	line("{");
	if (literal) {
		line("static kas3::vm::NamedSite site{};");
	}
	if (count) {
		line("const Slot args[] = { " + given + " };");
	}
	mark();
	line("Slot w = empty();");
	std::optional<ir::MathConsumer> math;
	if (literal && p_in.op == Op::DynGet && kept_.empty() && twice_.empty()) {
		if (!adjacent_) {
			adjacent_.emplace(function_);
		}
		math = ir::math_consumer(function_, pc_, *adjacent_);
		if (math && (!owns(math->target) || math->target < function_.parameters || !parts(math->target).empty() ||
				math->target >= function_.engine_types.size() || !has_parts(function_.engine_types[math->target]))) {
			math = {};
		}
	}
	const std::string reuse = math ? "\"" + function_.engine_types[math->target] + "\", " + reg(math->target) + ".handle" : "nullptr, nullptr";
	const std::string guard = (literal ? "!kas3::vm::any::is_engine(" + reg(holder) + ")" :
			"kas3::vm::any::tag(" + reg(holder) + ") != kas3::vm::AnyTag::Godot") + " ? kas3::vm::NamedResult::Declined : ";
	const std::string receiver = reg(holder);
	line("const auto handled = " + guard + "named_access(c, " + (literal ? "&site, " : "nullptr, ") + std::to_string(static_cast<unsigned>(p_in.op)) +
			", " + receiver + ", " + (literal ? reg(key) : "key_of(c, " + reg(key) + ")") + ", " + (count ? "args" : "nullptr") + ", " + std::to_string(count) + ", " + reuse + ", &w);");
	line("if (handled != kas3::vm::NamedResult::Declined) {");
	check();
	if (math) {
		const uint32_t after = pc_ + math->skip + 1;
		targets_[after] = true;
		line("if (handled == kas3::vm::NamedResult::Reused) goto L" + std::to_string(after) + ";");
	}
	if (!write && p_in.a != ir::NONE) {
		std::optional<uint32_t> moved;
		if (!literal && pc_ + 2 < function_.code.size() && kept_.empty() && twice_.empty()) {
			if (!adjacent_) {
				adjacent_.emplace(function_);
			}
			moved = ir::move_consumer(function_, pc_, *adjacent_);
		}
		put(moved.value_or(p_in.a), "w");
		if (moved) {
			targets_[pc_ + 2] = true;
			line("goto L" + std::to_string(pc_ + 2) + ";");
		}
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
