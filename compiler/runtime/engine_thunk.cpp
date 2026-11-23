// An instruction that asks the engine, left to the library by generated code: carried out on the values
// handed over, with no frame of the interpreter's. The interpreter's own request to the engine does the
// work (vm::Interpreter::engine_instruction), so what is asked, what is refused and the words of an error
// are the same; only the frame around it, and a copy of every handle put into that frame, are not made.
#include "native_host.h"

namespace kas3 {
namespace rt {

using ir::Kind;
using ir::Op;

bool asks_engine(uint8_t p_op) {
	switch (static_cast<Op>(p_op)) {
		case Op::HostNew:
		case Op::HostCall:
		case Op::HostGet:
		case Op::HostSet:
		case Op::HostOperate:
		case Op::HostToStr:
		case Op::HostPresent:
		case Op::HostIsClass:
		case Op::HostAsClass:
		case Op::HostDefault:
		case Op::HostConstant:
		case Op::HostCallStatic:
		case Op::HostCollection:
		case Op::HostBind:
		case Op::HostBindMethod:
		case Op::HostMember:
		case Op::HostIndexGet:
		case Op::HostIndexSet:
			return true;
		default:
			return false; // HostSingleton among them: the interpreter keeps what it found by the frame's function.
	}
}

Slot engine_thunk(Context *c, uint32_t p_function, const ThunkInfo &p_info, const Slot *p_given, Slot *r_updated) {
	Host &host = *c->host;
	Slot few[16];
	std::vector<Slot> many;
	Slot *registers = few;
	if (p_info.registers > 16) {
		many.resize(p_info.registers);
		registers = many.data();
	}
	for (uint32_t i = 0; i < p_info.registers; i++) {
		registers[i] = i < p_info.given ? p_given[i] : empty();
	}
	// What is handed over stays the caller's, but for a register the instruction writes: the one it moves
	// on, and what it gives where that is also something it reads. Each is given a reference of its own,
	// as a frame would hold, and what is there afterwards is handed back.
	const auto kind = [&](uint32_t p_register) { return static_cast<Kind>(p_info.kinds[p_register]); };
	const uint32_t result = p_info.result;
	const uint32_t moved = p_info.updated != result ? p_info.updated : NONE;
	if (result != NONE && result < p_info.given) {
		registers[result] = host.interpreter->heap().share(kind(result), registers[result]);
	}
	if (moved != NONE) {
		registers[moved] = host.interpreter->heap().share(kind(moved), registers[moved]);
	}
	const uint32_t carries = host.interpreter->carries();
	const uint32_t outer = host.asking;
	const bool prev_caller_active = host.caller_active;
	const vm::Object *const prev_caller_object = host.caller_object;
	const uint32_t prev_caller_class = host.caller_class;
	host.caller_active = true;
	host.caller_class = p_info.code_class;
	host.caller_object = p_info.self != NONE && p_info.self < p_info.registers ? static_cast<const vm::Object *>(registers[p_info.self].p) : nullptr;
	host.direct++;
	host.asking = host.direct;
	host.interpreter->direct_began();
	host.interpreter->engine_instruction(p_function, registers);
	host.interpreter->direct_ended();
	host.direct--;
	host.asking = outer;
	host.caller_active = prev_caller_active;
	host.caller_object = prev_caller_object;
	host.caller_class = prev_caller_class;
	settle_direct(c, carries);
	if (host.held.active && host.held.depth == c->depth) {
		// A function the engine ran for this request failed (Host::enter kept its error): it comes out of
		// the request, in place of anything the request itself raised.
		if (c->failing) {
			host.interpreter->discard(Kind::Any, c->pending);
			c->pending = empty();
			c->failing = false;
		}
		call_failed(c);
	}
	Slot given = result != NONE ? registers[result] : empty();
	if (moved != NONE && r_updated && !c->failing) {
		*r_updated = registers[moved];
	} else if (moved != NONE) {
		host.interpreter->discard(kind(moved), registers[moved]);
	}
	if (c->failing && result != NONE) {
		host.interpreter->discard(kind(result), given);
		given = empty();
	} else if (result != NONE && result == p_info.updated && r_updated) {
		*r_updated = host.interpreter->heap().share(kind(result), given);
	}
	return given;
}

} // namespace rt
} // namespace kas3
