#include "facts.h"

#include "function_writer.h"
#include "ir_reach.h"
#include "program_writer.h"
#include "thunks.h"

#include <algorithm>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// The operations that are sums, tests and moves, or reach only a vector's elements or a static field.
bool is_plain(Op p_op) {
	switch (p_op) {
		case Op::Stmt:
		case Op::ConstI32:
		case Op::ConstU32:
		case Op::ConstF64:
		case Op::ConstBool:
		case Op::ConstStr:
		case Op::ConstNull:
		case Op::ConstI64:
		case Op::Move:
		case Op::I32ToF64:
		case Op::U32ToF64:
		case Op::BoolToF64:
		case Op::F64ToI32:
		case Op::F64ToU32:
		case Op::I32ToU32:
		case Op::U32ToI32:
		case Op::I32ToBool:
		case Op::U32ToBool:
		case Op::F64ToBool:
		case Op::StrToBool:
		case Op::ObjToBool:
		case Op::I32ToU8: case Op::U8ToI32: case Op::U32ToU8: case Op::U8ToU32:
		case Op::I64ToF32:
		case Op::F64ToF32:
		case Op::F32ToF64:
		case Op::AddF32:
		case Op::SubF32:
		case Op::MulF32:
		case Op::DivF32:
		case Op::ModF32:
		case Op::AddF64:
		case Op::SubF64:
		case Op::MulF64:
		case Op::DivF64:
		case Op::ModF64:
		case Op::NegF64:
		case Op::BitAnd:
		case Op::BitOr:
		case Op::BitXor:
		case Op::Shl:
		case Op::Shr:
		case Op::UShr:
		case Op::BitNot:
		case Op::LtF64:
		case Op::LeF64:
		case Op::EqF64:
		case Op::EqBool:
		case Op::EqRef:
		case Op::Not:
		case Op::MathUnary:
		case Op::Atan2:
		case Op::Pow:
		case Op::MinF64:
		case Op::MaxF64:
		case Op::Random:
		case Op::Jump:
		case Op::JumpIf:
		case Op::JumpIfNot:
		case Op::GetField:
		case Op::SetField:
		case Op::GetGlobal:
		case Op::SetGlobal:
		case Op::NullCheck:
		case Op::ArgMissing:
		case Op::VecGet:
		case Op::VecSet:
		case Op::VecLength:
		case Op::IsClass:
		case Op::AsClass:
		case Op::Return:
		case Op::ReturnVoid:
			return true;
		default:
			return false;
	}
}

// Whether what a register of the kind held may end as it is let go: a String ends alone.
bool may_end(Kind p_kind) {
	return ir::is_held(p_kind) && p_kind != Kind::Str;
}

// The most classes an object's class is compared with before its method table is asked.
constexpr size_t MOST_COMPARED = 4;

// The class a CallVirtual's receiver is known to be of (that class or one that extends it), or NONE. The
// lowerer names the class that declares the method for a receiver other than `this`: the receiver's
// declared type is that class or extends it, and a register holds nothing that is not of its declared type
// (a value that arrives untyped, from Godot or out of a vector is checked as it is stored). With no class
// named, the receiver is known only where it is the method's own object, which is of the method's class:
// register 0 of a method, where nothing writes that register. Anything else may be of any class.
uint32_t receiver_class(const ir::Module &p_module, const ir::Function &p_function, uint32_t p_pc, bool p_keeps_this) {
	const Instruction &in = p_function.code[p_pc];
	uint32_t found = NONE;
	for (const auto &[at, declared] : p_function.virtual_receivers) {
		found = at == p_pc ? declared : found;
	}
	if (found == NONE && p_keeps_this && p_function.owner != NONE && p_function.parameters > 0 && ir::on_itself(p_function, in)) {
		found = p_function.owner;
	}
	return found < p_module.classes.size() && in.b < p_module.classes[found].methods.size() ? found : NONE;
}

// Whether no instruction of the function writes its first register.
bool keeps_this(const ir::Function &p_function) {
	for (const ir::Handler &handler : p_function.handlers) {
		if (handler.reg == 0) {
			return false;
		}
	}
	for (const Instruction &in : p_function.code) {
		const char first = ir::op_shape(in.op)[0];
		if (((first == 'R' || first == 'r') && in.a == 0 && !first_is_read(in.op)) || changed_in_place(p_function, in) == 0) {
			return false;
		}
	}
	return true;
}

// The functions a method table may hold in a slot, for a receiver of a class or of one that extends it:
// one for each such class of the program, each given to p_take with the class.
template <class Take>
void each_held(const ir::Module &p_module, uint32_t p_declared, uint32_t p_slot, const Take &p_take) {
	for (uint32_t klass = 0; klass < p_module.classes.size(); klass++) {
		if (p_slot < p_module.classes[klass].methods.size() && ir::extends_class(p_module, klass, p_declared)) {
			p_take(p_module.classes[klass].methods[p_slot], klass);
		}
	}
}

// Every call through a method table whose receiver's class is known, and for each such class and slot the
// functions the table may hold: one for each class of the program that is the class or extends it.
void find_tables(const ir::Module &p_module, Facts *r_facts) {
	std::vector<bool> made(p_module.classes.size(), false);
	for (const ir::Function &function : p_module.functions) {
		for (const Instruction &in : function.code) {
			if (in.op == Op::New && in.b < made.size()) {
				made[in.b] = true;
			}
		}
	}
	r_facts->receivers.resize(p_module.functions.size());
	for (size_t i = 0; i < p_module.functions.size(); i++) {
		const ir::Function &function = p_module.functions[i];
		const bool keeps = keeps_this(function);
		for (uint32_t pc = 0; pc < function.code.size(); pc++) {
			const uint32_t declared = function.code[pc].op == Op::CallVirtual ? receiver_class(p_module, function, pc, keeps) : NONE;
			if (declared == NONE) {
				continue;
			}
			const uint32_t slot = function.code[pc].b;
			r_facts->receivers[i].emplace_back(pc, declared);
			const auto [entry, added] = r_facts->tables.try_emplace(std::make_pair(declared, slot));
			if (!added) {
				continue;
			}
			TableFacts &table = entry->second;
			bool one = true;
			each_held(p_module, declared, slot, [&](uint32_t p_held, uint32_t p_class) {
				one = one && (table.only == NONE || table.only == p_held);
				table.only = p_held;
				if (made[p_class]) {
					table.made.emplace_back(p_class, p_held);
				}
			});
			table.only = one ? table.only : NONE;
			if (table.made.size() > MOST_COMPARED) {
				table.made.clear();
			}
		}
	}
}

// A function's own instructions: the fields they read and write, and whether any may run code.
void look(const ir::Module &p_module, const ir::Function &p_function, FunctionFacts *r_facts) {
	// A field reached through the method's own object is of the class that declares it: the one the
	// method's class is or extends that first has the slot.
	const bool own = p_function.owner != NONE && !p_function.is_static && p_function.parameters > 0 && keeps_this(p_function);
	const auto field = [&](uint32_t p_object, uint32_t p_slot) {
		uint32_t declares = own && p_object == 0 ? p_function.owner : NONE;
		while (declares != NONE && p_module.classes[declares].base < p_module.classes.size() &&
				p_slot < p_module.classes[p_module.classes[declares].base].fields.size()) {
			declares = p_module.classes[declares].base;
		}
		return std::make_pair(declares, p_slot);
	};
	r_facts->opaque = false;
	for (const Instruction &in : p_function.code) {
		r_facts->opaque = r_facts->opaque || may_run_code(p_module, p_function, in);
		r_facts->stores_elements = r_facts->stores_elements || in.op == Op::VecSet;
		if (in.op == Op::GetField) {
			r_facts->reads.insert(field(in.b, in.c));
		} else if (in.op == Op::SetField) {
			r_facts->writes.insert(field(in.a, in.b));
		}
	}
}

// One function's facts added to another's: what the two do, taken together. Whether anything changed.
bool add_facts(const FunctionFacts &p_more, FunctionFacts *r_facts) {
	const size_t before = r_facts->reads.size() + r_facts->writes.size() + (r_facts->stores_elements ? 1 : 0) + (r_facts->opaque ? 1 : 0);
	r_facts->stores_elements = r_facts->stores_elements || p_more.stores_elements;
	r_facts->opaque = r_facts->opaque || p_more.opaque;
	r_facts->reads.insert(p_more.reads.begin(), p_more.reads.end());
	r_facts->writes.insert(p_more.writes.begin(), p_more.writes.end());
	return r_facts->reads.size() + r_facts->writes.size() + (r_facts->stores_elements ? 1 : 0) + (r_facts->opaque ? 1 : 0) != before;
}

// What a function calls is added to what it does itself, until nothing more is. A call through a method
// table adds every function the table may hold, and one whose receiver may be of any class is anything.
void follow_calls(const ir::Module &p_module, Facts *r_facts) {
	for (bool changed = true; changed;) {
		changed = false;
		for (uint32_t i = 0; i < p_module.functions.size(); i++) {
			FunctionFacts &own = r_facts->functions[i];
			const auto add = [&](uint32_t p_callee, uint32_t) { changed = (p_callee != i && add_facts(r_facts->functions[p_callee], &own)) || changed; };
			const std::vector<Instruction> &code = p_module.functions[i].code;
			auto receiver = r_facts->receivers[i].begin();
			for (uint32_t pc = 0; pc < code.size() && !own.opaque; pc++) {
				if (code[pc].op == Op::Call) {
					add(code[pc].b, NONE);
				} else if (code[pc].op == Op::CallVirtual) {
					while (receiver != r_facts->receivers[i].end() && receiver->first < pc) {
						receiver++;
					}
					if (receiver != r_facts->receivers[i].end() && receiver->first == pc) {
						each_held(p_module, receiver->second, code[pc].b, add);
					} else {
						own.opaque = true;
						changed = true;
					}
				}
			}
		}
	}
	for (auto &[key, table] : r_facts->tables) {
		table.does.opaque = false;
		table.does.quiet = true;
		each_held(p_module, key.first, key.second, [&, &table = table](uint32_t p_held, uint32_t) {
			add_facts(r_facts->functions[p_held], &table.does);
			table.does.quiet = table.does.quiet && r_facts->functions[p_held].quiet;
		});
	}
}

void look_at_classes(const ir::Module &p_module, Facts *r_facts) {
	const size_t count = p_module.classes.size();
	r_facts->ends_alone.assign(count, false);
	r_facts->starts_quietly.assign(count, true);
	for (size_t i = 0; i < count; i++) {
		const ir::Class &klass = p_module.classes[i];
		bool alone = has_struct(klass) && klass.native_base.empty() && !klass.counted && !klass.built_in;
		for (const ir::Field &field : klass.fields) {
			alone = alone && !may_end(field.kind);
		}
		r_facts->ends_alone[i] = alone;
		for (uint32_t at = static_cast<uint32_t>(i); at < count; at = p_module.classes[at].base) {
			r_facts->starts_quietly[i] = r_facts->starts_quietly[i] && p_module.classes[at].static_init == NONE;
		}
	}
	// An object of a class may be of any class that extends it.
	for (size_t i = 0; i < count; i++) {
		for (uint32_t base = p_module.classes[i].base; !r_facts->ends_alone[i] && base < count; base = p_module.classes[base].base) {
			r_facts->ends_alone[base] = false;
		}
	}
}

// Each static const String global that is only ever assigned one string literal across the whole module,
// remembered by that literal's index so callers can cache a Godot StringName across classes.
void find_const_strings(const ir::Module &p_module, Facts *r_facts) {
	const size_t count = p_module.globals.size();
	r_facts->const_strings.assign(count, NONE);
	std::vector<bool> invalid(count, false);
	for (const ir::Function &function : p_module.functions) {
		std::vector<bool> landed(function.code.size() + 1, false);
		for (const Instruction &in : function.code) {
			const uint32_t target = jump_target(in);
			if (target != NONE && target < landed.size()) {
				landed[target] = true;
			}
		}
		for (const ir::Handler &handler : function.handlers) {
			if (handler.target < landed.size()) {
				landed[handler.target] = true;
			}
		}
		std::vector<uint32_t> str(function.registers.size(), NONE);
		for (uint32_t pc = 0; pc < function.code.size(); pc++) {
			const Instruction &in = function.code[pc];
			if (landed[pc]) {
				std::fill(str.begin(), str.end(), NONE);
			}
			if (in.op == Op::SetGlobal && in.a < count) {
				const uint32_t value = in.b < str.size() ? str[in.b] : NONE;
				if (!p_module.globals[in.a].is_const || p_module.globals[in.a].kind != Kind::Str || value == NONE ||
						(r_facts->const_strings[in.a] != NONE && r_facts->const_strings[in.a] != value)) {
					invalid[in.a] = true;
				} else {
					r_facts->const_strings[in.a] = value;
				}
			}
			const char first = ir::op_shape(in.op)[0];
			if ((first == 'R' || first == 'r') && in.a != NONE && !first_is_read(in.op) && in.a < str.size()) {
				if (in.op == Op::ConstStr) {
					str[in.a] = in.b;
				} else if (in.op == Op::Move && in.b < str.size()) {
					str[in.a] = str[in.b];
				} else {
					str[in.a] = NONE;
				}
			}
		}
	}
	for (size_t i = 0; i < count; i++) {
		if (invalid[i]) {
			r_facts->const_strings[i] = NONE;
		}
	}
}

} // namespace

bool may_run_code(const ir::Module &p_module, const ir::Function &p_function, const Instruction &p_in) {
	return may_run_code(p_module, p_function, p_in, std::vector<bool>());
}

bool may_be_field(const ir::Module &p_module, const std::set<std::pair<uint32_t, uint32_t>> &p_fields, uint32_t p_class, uint32_t p_slot) {
	for (const auto &[declares, slot] : p_fields) {
		if (slot == p_slot && (declares == NONE || ir::extends_class(p_module, p_class, declares))) {
			return true;
		}
	}
	return false;
}

const ir::Function *table_signature(const ir::Module &p_module, const ir::Function &p_function, uint32_t p_pc) {
	uint32_t declared = NONE;
	for (const auto &[at, klass] : p_function.virtual_receivers) {
		if (at == p_pc) {
			declared = klass;
		}
	}
	if (declared == NONE) {
		declared = p_function.owner;
	}
	if (declared == NONE || p_function.code[p_pc].b >= p_module.classes[declared].methods.size()) {
		return nullptr;
	}
	return &p_module.functions[p_module.classes[declared].methods[p_function.code[p_pc].b]];
}

const TableFacts *Facts::table(const ir::Module &p_module, uint32_t p_function, uint32_t p_pc) const {
	if (p_function >= receivers.size()) {
		return nullptr;
	}
	const std::vector<std::pair<uint32_t, uint32_t>> &calls = receivers[p_function];
	const auto found = std::lower_bound(calls.begin(), calls.end(), std::make_pair(p_pc, uint32_t(0)));
	if (found == calls.end() || found->first != p_pc) {
		return nullptr;
	}
	return &tables.find(std::make_pair(found->second, p_module.functions[p_function].code[p_pc].b))->second;
}

const FunctionFacts *Facts::called(const ir::Module &p_module, uint32_t p_function, uint32_t p_pc) const {
	const Instruction &in = p_module.functions[p_function].code[p_pc];
	if (in.op == Op::Call) {
		return &functions[in.b];
	}
	const TableFacts *found = in.op == Op::CallVirtual ? table(p_module, p_function, p_pc) : nullptr;
	return found ? &found->does : nullptr;
}

bool may_run_code(const ir::Module &p_module, const ir::Function &p_function, const Instruction &p_in, const std::vector<bool> &p_counts_nothing) {
	if (p_in.op == Op::Call) {
		return may_end(p_module.functions[p_in.b].result); // Kept or not, a result is let go of in the end.
	}
	if (p_in.op == Op::CallVirtual) {
		// The same, of the result every function in the slot gives. What the functions do is not said here.
		const size_t pc = static_cast<size_t>(&p_in - p_function.code.data());
		const ir::Function *callee = pc < p_function.code.size() ? table_signature(p_module, p_function, static_cast<uint32_t>(pc)) : nullptr;
		return !callee || may_end(callee->result);
	}
	if (!is_plain(p_in.op)) {
		return true;
	}
	const char first = ir::op_shape(p_in.op)[0];
	if ((first == 'R' || first == 'r') && p_in.a != NONE && !first_is_read(p_in.op) && may_end(p_function.registers[p_in.a]) &&
			!(p_in.a < p_counts_nothing.size() && p_counts_nothing[p_in.a])) {
		return true; // What the register held is let go of.
	}
	// And what a field, a static field or an element held, when another is stored there.
	const uint32_t stored = p_in.op == Op::SetField || p_in.op == Op::VecSet ? p_in.c : p_in.op == Op::SetGlobal ? p_in.b : NONE;
	return stored != NONE && may_end(p_function.registers[stored]);
}

Facts find_facts(const ir::Module &p_module, bool p_keeps_stack) {
	Facts facts;
	facts.functions.resize(p_module.functions.size());
	ThunkTable scratch; // The thunks a function uses are numbered as the functions are really written.
	for (uint32_t i = 0; i < p_module.functions.size(); i++) {
		FunctionWriter writer(p_module, i, scratch, p_keeps_stack, nullptr);
		writer.write();
		facts.functions[i].quiet = !writer.fails();
		look(p_module, p_module.functions[i], &facts.functions[i]);
	}
	find_tables(p_module, &facts);
	follow_calls(p_module, &facts);
	look_at_classes(p_module, &facts);
	find_const_strings(p_module, &facts);
	return facts;
}

} // namespace backend_cpp
} // namespace kas3
