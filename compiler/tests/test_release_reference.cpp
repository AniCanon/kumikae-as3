// The release pass against its definition: liveness worked out one instruction and one register at a
// time, as the pass first did. That is too slow to ship and simple enough to trust, so every function
// of every test program must come out of both the same.
#include "check.h"
#include "builtins.h"
#include "last_use.h"
#include "lower.h"
#include "names.h"
#include "source.h"
#include "temporary_releases.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

using namespace kas3;
using namespace kas3::ir;

namespace {

struct Uses {
	std::vector<uint32_t> read;
	uint32_t written = NONE;
	std::vector<uint32_t> previous;
	std::vector<uint32_t> exceptional;
};

std::vector<Uses> uses_of(const Function &p_function) {
	std::vector<Uses> uses(p_function.code.size() + 1);
	for (uint32_t pc = 0; pc < p_function.code.size(); ++pc) {
		const auto &in = p_function.code[pc];
		const char *shape = op_shape(in.op);
		const uint32_t operands[] = { in.a, in.b, in.c };
		for (size_t n = 0; n < 3; ++n) {
			const auto reg = operands[n];
			if (shape[n] == 'L') {
				uses[reg].previous.push_back(pc);
			} else if (shape[n] == 'A') {
				const auto &args = p_function.argument_lists[reg];
				uses[pc].read.insert(uses[pc].read.end(), args.begin(), args.end());
			} else if ((shape[n] == 'R' || shape[n] == 'r') && reg != NONE) {
				if (n == 0 && !first_is_read(in.op)) {
					uses[pc].written = reg;
				} else {
					uses[pc].read.push_back(reg);
				}
			}
		}
		if (in.op != Op::Jump && in.op != Op::Return && in.op != Op::ReturnVoid && in.op != Op::Throw && in.op != Op::Rethrow) {
			uses[pc + 1].previous.push_back(pc);
		}
	}
	for (const auto &handler : p_function.handlers) {
		for (uint32_t pc = handler.start; pc < handler.end; ++pc) {
			uses[handler.target].exceptional.push_back(pc);
		}
	}
	return uses;
}

uint32_t stored(const Instruction &p_in) {
	switch (p_in.op) {
		case Op::Move: case Op::SetGlobal: return p_in.b;
		case Op::SetField: case Op::VecSet: return p_in.c;
		default: return NONE;
	}
}

using Registers = std::vector<std::unordered_set<uint32_t>>;

struct Live {
	Registers before;
	Registers after;
};

Live liveness(const std::vector<Uses> &p_uses, const std::vector<bool> &p_temporary) {
	Registers after(p_uses.size()), before(p_uses.size());
	std::vector<std::pair<uint32_t, uint32_t>> pending;
	for (uint32_t pc = 0; pc < p_uses.size(); ++pc) {
		for (auto reg : p_uses[pc].read) {
			if (p_temporary[reg]) {
				pending.emplace_back(pc, reg);
			}
		}
	}
	while (!pending.empty()) {
		const auto [pc, reg] = pending.back();
		pending.pop_back();
		if (!before[pc].insert(reg).second) {
			continue;
		}
		for (uint32_t prev : p_uses[pc].previous) {
			if (after[prev].insert(reg).second && p_uses[prev].written != reg) {
				pending.emplace_back(prev, reg);
			}
		}
		for (uint32_t prev : p_uses[pc].exceptional) {
			after[prev].insert(reg);
			pending.emplace_back(prev, reg); // A failed instruction need not have written its result.
		}
	}
	return { std::move(before), std::move(after) };
}

struct Lifetime {
	std::vector<bool> transfers;
	std::vector<std::vector<uint32_t>> release;
	std::vector<std::vector<uint32_t>> taken;
	std::vector<std::vector<uint32_t>> caught;
};

std::vector<bool> temporaries(const Function &p_function, const std::vector<bool> &p_owned, const std::vector<Uses> &p_uses, bool p_stripped) {
	auto temporary = p_owned;
	for (uint32_t reg = 0; reg < p_function.parameters; ++reg) {
		temporary[reg] = false;
	}
	for (const auto &local : p_function.locals) {
		temporary[local.reg] = false;
	}
	for (const auto &handler : p_function.handlers) {
		temporary[handler.reg] = false;
	}
	// Older modules can omit local names. A Move destination may then be a source local.
	if (p_stripped && p_function.locals.empty()) {
		for (const auto &in : p_function.code) {
			if (in.op == Op::Move) {
				temporary[in.a] = false;
			}
		}
	}

	// A literal string belongs to the module, and a bare math handle contains only scalar parts.
	// Keeping either available lets a warmed loop reuse its storage.
	const auto bare_math = [&](uint32_t reg) {
		return reg < p_function.engine_types.size() && p_function.registers[reg] == Kind::Val &&
				bare_named(p_function.engine_types[reg]) != Bare::None;
	};
	std::vector<bool> can_hold(temporary.size());
	for (size_t pc = 0; pc < p_function.code.size(); ++pc) {
		const auto &in = p_function.code[pc];
		const uint32_t reg = p_uses[pc].written;
		if (reg != NONE && in.op != Op::ConstStr && in.op != Op::AnyConst && in.op != Op::ConstNull &&
				!(in.op == Op::ToAny && (!is_held(p_function.registers[in.b]) || bare_math(in.b)))) {
			can_hold[reg] = true;
		}
	}
	for (size_t reg = 0; reg < temporary.size(); ++reg) {
		const bool math = bare_math(static_cast<uint32_t>(reg));
		temporary[reg] = temporary[reg] && can_hold[reg] && !math;
	}
	return temporary;
}

void edge_deaths(const std::unordered_set<uint32_t> &p_live, const std::unordered_set<uint32_t> &p_needed, std::vector<uint32_t> &r_deaths) {
	for (auto reg : p_live) {
		if (!p_needed.count(reg)) {
			r_deaths.push_back(reg);
		}
	}
	std::sort(r_deaths.begin(), r_deaths.end());
}

Lifetime lifetimes(const Function &p_function, const std::vector<bool> &p_owned, bool p_stripped) {
	const auto uses = uses_of(p_function);
	const auto temporary = temporaries(p_function, p_owned, uses, p_stripped);
	const auto live = liveness(uses, temporary);
	Lifetime result;
	result.transfers.resize(p_function.code.size());
	result.release.resize(p_function.code.size());
	result.taken.resize(p_function.code.size());
	for (uint32_t pc = 0; pc < p_function.code.size(); ++pc) {
		const auto &in = p_function.code[pc];
		auto &release = result.release[pc];
		const auto consider = [&](uint32_t p_reg) {
			if (p_reg != NONE && temporary[p_reg] && !live.after[pc].count(p_reg) && std::find(release.begin(), release.end(), p_reg) == release.end()) {
				release.push_back(p_reg);
			}
		};
		for (auto reg : uses[pc].read) {
			consider(reg);
		}
		consider(uses[pc].written);
		if (in.op == Op::JumpIf || in.op == Op::JumpIfNot) {
			result.taken[pc] = release; // What the branch itself last uses ends on both of its edges.
			edge_deaths(live.after[pc], live.before[in.b], result.taken[pc]);
		}
		if (!falls_through(in.op)) {
			release.clear(); // A return or a throw hands its operand on, and nothing runs after it.
			continue;
		}
		edge_deaths(live.after[pc], live.before[pc + 1], release);
		const uint32_t from = stored(in);
		const auto found = std::find(release.begin(), release.end(), from);
		if (found != release.end() && (in.op != Op::Move || (p_owned[in.a] && in.a != in.b))) {
			result.transfers[pc] = true;
			release.erase(found);
		}
	}
	// A string has no destructor that can run user code. Keep a producer and its consuming
	// Move adjacent so both back ends can write the result straight into the destination.
	for (size_t pc = 1; pc < p_function.code.size(); ++pc) {
		const auto &move = p_function.code[pc];
		auto &previous = result.release[pc - 1];
		if (move.op != Op::Move || previous.empty() || uses[pc].previous.size() != 1 || !uses[pc].exceptional.empty()) {
			continue;
		}
		auto &following = result.release[pc];
		previous.erase(std::remove_if(previous.begin(), previous.end(), [&](uint32_t p_reg) {
			if (p_function.registers[p_reg] != Kind::Str || p_reg == move.a || p_reg == move.b) {
				return false;
			}
			following.push_back(p_reg);
			return true;
		}), previous.end());
	}
	for (const auto &handler : p_function.handlers) {
		std::unordered_set<uint32_t> abandoned;
		for (uint32_t pc = handler.start; pc < handler.end; ++pc) {
			abandoned.insert(live.before[pc].begin(), live.before[pc].end());
			const uint32_t reg = uses[pc].written;
			if (reg != NONE && temporary[reg]) {
				abandoned.insert(reg);
			}
		}
		edge_deaths(abandoned, live.before[handler.target], result.caught.emplace_back());
	}
	return result;
}

// Only the edge that leaves the value behind enters its cleanup instructions.
uint32_t cleanup(std::vector<Instruction> &r_code, uint32_t p_target, const std::vector<uint32_t> &p_registers) {
	if (p_registers.empty()) {
		return p_target;
	}
	const uint32_t start = static_cast<uint32_t>(r_code.size());
	const uint32_t position = r_code[p_target].position;
	for (auto reg : p_registers) {
		r_code.push_back({ Op::Release, reg, 0, 0, position });
	}
	r_code.push_back({ Op::Jump, p_target, 0, 0, position });
	return start;
}

std::vector<bool> reference_last_stores(const Function &p_function, const std::vector<bool> &p_candidate) {
	auto last = p_candidate;
	if (std::none_of(last.begin(), last.end(), [](bool p_value) { return p_value; })) {
		return last;
	}
	std::vector<bool> sources(p_function.registers.size());
	for (size_t pc = 0; pc < last.size(); ++pc) {
		if (last[pc]) {
			sources[stored(p_function.code[pc])] = true;
		}
	}
	const auto live = liveness(uses_of(p_function), sources);
	for (size_t pc = 0; pc < last.size(); ++pc) {
		if (last[pc]) {
			last[pc] = !live.after[pc].count(stored(p_function.code[pc]));
		}
	}
	return last;
}

void reference_release_temporaries(Function &r_function, bool p_stripped) {
	std::vector<bool> held;
	for (Kind kind : r_function.registers) {
		held.push_back(is_held(kind));
	}
	const auto plan = lifetimes(r_function, held, p_stripped);
	std::vector<uint32_t> positions;
	std::vector<Instruction> code;
	for (uint32_t pc = 0; pc < r_function.code.size(); ++pc) {
		const Instruction &in = r_function.code[pc];
		positions.push_back(static_cast<uint32_t>(code.size()));
		code.push_back(in);
		if (plan.transfers[pc]) {
			switch (in.op) {
				case Op::Move: code.back().op = Op::TakeMove; break;
				case Op::SetField: code.back().op = Op::TakeField; break;
				case Op::SetGlobal: code.back().op = Op::TakeGlobal; break;
				case Op::VecSet: code.back().op = Op::TakeElement; break;
				default: break;
			}
		}
		const auto written = positions.back();
		for (auto reg : plan.release[pc]) {
			if (reg == in.a && op_shape(in.op)[0] == 'r') {
				code[written].a = NONE;
				continue;
			}
			code.push_back({ Op::Release, reg, 0, 0, in.position });
		}
	}
	positions.push_back(static_cast<uint32_t>(code.size()));
	for (auto &in : code) {
		if (in.op == Op::Jump) {
			in.a = positions[in.a];
		} else if (in.op == Op::JumpIf || in.op == Op::JumpIfNot) {
			in.b = positions[in.b];
		}
	}
	for (size_t pc = 0; pc < plan.taken.size(); ++pc) {
		if (!plan.taken[pc].empty()) {
			const auto branch = positions[pc];
			const auto target = cleanup(code, code[branch].b, plan.taken[pc]);
			code[branch].b = target;
		}
	}
	for (size_t i = 0; i < r_function.handlers.size(); ++i) {
		auto &handler = r_function.handlers[i];
		handler.start = positions[handler.start];
		handler.end = positions[handler.end];
		handler.target = cleanup(code, positions[handler.target], plan.caught[i]);
	}
	for (auto &[pc, klass] : r_function.virtual_receivers) {
		pc = positions[pc];
	}
	r_function.code = std::move(code);
}


bool same(const Function &p_a, const Function &p_b) {
	if (p_a.code.size() != p_b.code.size() || p_a.handlers.size() != p_b.handlers.size() || p_a.virtual_receivers != p_b.virtual_receivers) {
		return false;
	}
	for (size_t pc = 0; pc < p_a.code.size(); ++pc) {
		const auto &a = p_a.code[pc];
		const auto &b = p_b.code[pc];
		if (a.op.execution != b.op.execution || a.a != b.a || a.b != b.b || a.c != b.c || a.position != b.position) {
			return false;
		}
	}
	for (size_t i = 0; i < p_a.handlers.size(); ++i) {
		const auto &a = p_a.handlers[i];
		const auto &b = p_b.handlers[i];
		if (a.start != b.start || a.end != b.end || a.target != b.target) {
			return false;
		}
	}
	return true;
}

// Checks every function of a program both ways. False if the program does not compile.
bool compare(const std::vector<std::pair<std::string, std::string>> &p_files, bool p_godot) {
	Compilation compilation;
	if (p_godot) {
		compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	}
	for (const auto &[path, text] : p_files) {
		compilation.add(path, text);
	}
	compilation.check();
	Module module;
	if (compilation.has_errors() || !lower_holding_temporaries(compilation, &module, p_godot ? LowerMode::Library : LowerMode::Program)) {
		return false;
	}
	TemporaryReleases releases;
	for (const auto &fn : module.functions) {
		for (const bool stripped : { false, true }) {
			Function expected = fn;
			Function actual = fn;
			if (stripped) {
				expected.locals.clear();
				actual.locals.clear();
			}
			reference_release_temporaries(expected, stripped);
			releases.insert(actual, stripped);
			if (!same(expected, actual)) {
				check::failures()++;
				std::printf("FAIL %s: %s is released differently%s\n", p_files[0].first.c_str(), fn.name.c_str(), stripped ? " without its locals" : "");
			}
			check::checks()++;
		}
		std::vector<bool> stores(fn.code.size());
		for (size_t pc = 0; pc < fn.code.size(); ++pc) {
			stores[pc] = stored_by(fn.code[pc]) != NONE;
		}
		CHECK(last_stores(fn, stores) == reference_last_stores(fn, stores));
	}
	return true;
}

std::vector<std::filesystem::path> sources_in(const std::filesystem::path &p_folder) {
	std::vector<std::filesystem::path> found;
	for (const auto &entry : std::filesystem::directory_iterator(p_folder)) {
		if (entry.path().extension() == ".as") {
			found.push_back(entry.path());
		}
	}
	std::sort(found.begin(), found.end());
	return found;
}

std::pair<std::string, std::string> file(const std::filesystem::path &p_path) {
	std::string text;
	CHECK(read_file(p_path.string(), &text));
	return { p_path.string(), text };
}

// One long method: p_depth nested statements of a kind around p_count that make and drop temporaries.
std::string nested(const char *p_open, const char *p_close, int p_depth, int p_count) {
	std::string body;
	for (int i = 0; i < p_depth; ++i) {
		body += std::string(p_open) + "\n";
	}
	for (int i = 0; i < p_count; ++i) {
		body += i % 3 == 0 ? "label = label + k + \":\" + n;\n" : i % 3 == 1 ? "items.push(make(k));\n" : "items.push(k > n ? make(k) : label);\n";
	}
	for (int i = 0; i < p_depth; ++i) {
		body += std::string(p_close) + "\n";
	}
	return "package { public class F { private var label:String = \"a\"; private var items:Array = []; private var n:int = 0;\n"
			"public function make(i:int):Object { return items; }\npublic function many(...rest):int { return 1; }\n"
			"public function big(k:int):Object {\n" + body + "return many(make(1), k > 1 ? make(2) : label, make(3), label + k); } } }\n";
}

void test_programs() {
	const std::filesystem::path tests(KAS3_TESTS_DIR);
	int compared = 0;
	for (const char *folder : { "run", "bench", "corpus", "golden/ok" }) {
		for (const auto &path : sources_in(tests / folder)) {
			compared += compare({ file(path) }, false);
		}
	}
	for (const auto &path : sources_in(tests / "run" / "godot")) {
		compared += compare({ file(path) }, true);
	}
	std::vector<std::pair<std::string, std::string>> project;
	for (const auto &path : sources_in(tests / "godot_project")) {
		project.push_back(file(path));
		compared += compare({ project.back() }, true);
	}
	compared += compare(project, true);
	// Most of these are whole programs. A count that falls far means the folders moved, not that they agree.
	CHECK(compared > 200);
}

// Enough registers live at once that their set needs more than one word of bits.
void test_many_live_at_once() {
	CHECK(compare({ { "F.as", nested("for each (var x:Object in items) {", "}", 70, 30) } }, false));
	std::string chosen;
	for (int i = 0; i < 150; ++i) {
		chosen += (i == 0 ? "" : ", ") + std::string("k > ") + std::to_string(i) + " ? make(k) : label";
	}
	CHECK(compare({ { "F.as", "package { public class F { private var label:String = \"a\";\n"
			"public function make(i:int):Object { return this; }\npublic function many(...rest):int { return 1; }\n"
			"public function big(k:int):int { try { return many(" + chosen + "); } catch (e:Error) { return many(" + chosen + "); } return 0; } } }\n" } }, false));
}

void test_nesting() {
	CHECK(compare({ { "F.as", nested("try {", "} catch (e:Error) { n++; }", 12, 40) } }, false));
	CHECK(compare({ { "F.as", nested("try {", "} finally { n++; }", 9, 40) } }, false));
	CHECK(compare({ { "F.as", nested("for each (var x:Object in items) {", "}", 10, 40) } }, false));
	CHECK(compare({ { "F.as", nested("for each (var x:Object in items) { try {", "} catch (e:Error) { if (n > k) { continue; } n++; } }", 6, 40) } }, false));
	CHECK(compare({ { "F.as", nested("try { if (k == n) { return make(k); }", "} catch (e:Error) { n++; } finally { items.push(label + n); }", 6, 30) } }, false));
}

} // namespace

int main() {
	test_programs();
	test_nesting();
	test_many_live_at_once();
	return check::finish();
}
