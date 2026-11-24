#include "engine_named.h"
#include "vector_calls.h"

namespace kas3::vm {

bool Interpreter::access_named(NamedSite &p_site, ir::Op p_op, Slot p_holder, std::string_view p_name, const Slot *p_args, size_t p_count, Slot *r_value, std::optional<NamedCall> *r_call, const Wanted &p_wanted) {
	std::string error;
	ScriptRead read = ScriptRead::Declined;
	const auto done = named_sites_.access(p_site, module_, host_.handles, p_op, p_holder, p_name, p_args, p_count, r_value, r_call, &error, &read, p_wanted);
	if (read == ScriptRead::Method || read == ScriptRead::Missing) {
		EngineNamed(*this).finish_read(read, p_holder, p_name, r_value);
		return true;
	}
	if (done && !*done && !failing_) {
		raise_engine(library_, error);
	}
	return done.has_value();
}

bool Interpreter::read_computed(Slot p_holder, std::string_view p_name, Slot *r_value, void *p_reuse) {
	std::string error;
	const auto read = host_.handles.read_script(any::pointer(p_holder), nullptr, p_name, nullptr, { ir::Kind::Any, {}, p_reuse }, r_value, &error);
	if (read == ScriptRead::Declined) {
		PropertyKey key;
		key.borrowed = p_name;
		return any_null_check(p_holder) && ordinary_property(p_holder, key, r_value);
	}
	if (read == ScriptRead::Failed) {
		raise_engine(library_, error);
	} else if (read == ScriptRead::Method || read == ScriptRead::Missing) {
		EngineNamed(*this).finish_read(read, p_holder, p_name, r_value);
	}
	return !failing_ && !stopped_;
}

// Keep a vector's String element in its register across a getter that can reenter AS3.
bool Interpreter::indexed_read(const ir::Function &p_function, const ir::Instruction &p_in, Slot *p_registers) {
	Frame &frame = frames_.back();
	if (frame.pc == p_function.code.size()) {
		return false;
	}
	const auto &get = p_function.code[frame.pc];
	if (get.op != ir::Op::DynGet || get.c != p_in.a || p_function.registers[get.b] != ir::Kind::Any ||
			any::tag(p_registers[get.b]) != AnyTag::Godot) {
		return false;
	}
	const auto *vector = static_cast<const Vector *>(p_registers[p_in.b].p);
	if (!vector) {
		return false;
	}
	const auto at = element_index(p_function.registers[p_in.c], p_registers[p_in.c], vector->length);
	if (!at) {
		return false;
	}
	auto *key = static_cast<Str *>(slots_of(vector)[*at].p);
	if (!key) {
		return false;
	}
	auto *old = static_cast<Str *>(p_registers[p_in.a].p);
	if (key != old) {
		retain(key);
		p_registers[p_in.a].p = key;
		if (release(old)) {
			free_string(old);
		}
	}
	const size_t base = frame.base;
	const uint32_t moved = named_sites_.move_target(frame.function, frame.pc);
	const uint32_t target = moved == ir::NONE ? get.a : moved;
	const Slot previous = p_registers[target];
	void *reuse = any::tag(previous) == AnyTag::Value ? any::pointer(previous) : nullptr;
	frame.pc++;
	Slot value = any::undefined();
	if (read_computed(p_registers[get.b], key->view(), &value, reuse)) {
		if (moved != ir::NONE) {
			frames_.back().pc++;
		}
		Slot &destination = stack_[base + target];
		if (reuse && previous.bits == value.bits) {
			return true;
		}
		if (any::holds(destination)) {
			put(ir::Kind::Any, destination, value);
		} else {
			destination = value;
		}
	}
	return true;
}

bool Interpreter::cached_property(const ir::Function &p_function, const ir::Instruction &p_in, const Slot *p_borrow) {
	const bool write = p_in.op == ir::Op::DynSet;
	const uint32_t holder = write ? p_in.a : p_in.b;
	const Frame &frame = frames_.back();
	const size_t base = frame.base;
	const Slot *registers = stack_.data() + base;
	if (p_function.registers[holder] != ir::Kind::Any || !any::is_engine(registers[holder])) {
		return false;
	}
	const auto *list = p_in.op == ir::Op::DynCall ? &p_function.argument_lists[p_in.c] : nullptr;
	const uint32_t key = list ? (*list)[0] : write ? p_in.b : p_in.c;
	const bool string_key = p_function.registers[key] == ir::Kind::Str;
	const bool computed = !write && !list && p_function.registers[key] == ir::Kind::Any && any::tag(registers[key]) == AnyTag::String;
	if ((!string_key && !computed) || (list && list->size() > 5)) {
		return false;
	}
	const auto *text = static_cast<const Str *>(computed ? any::pointer(registers[key]) : registers[key].p);
	InterpreterSite *site = named_sites_.at(frame.function, frame.pc - 1, text, strings_);
	if (!site) {
		Slot result = any::undefined();
		if (write || list || !text || any::tag(registers[holder]) != AnyTag::Godot) {
			return false;
		}
		if (read_computed(registers[holder], text->view(), &result)) {
			put(ir::Kind::Any, stack_[base + p_in.a], result);
		}
		return true;
	}
	if (p_borrow) {
		// The adjacent ToAny supplied the setter's sole argument. It lives across a reentrant setter.
		Slot result = any::undefined();
		const bool done = access_named(*site, p_in.op, registers[holder], text->view(), p_borrow, 1, &result, nullptr, { ir::Kind::Void, {} });
		if (any::holds(result)) {
			drop_any(result);
		}
		return done;
	}
	Slot args[4];
	size_t count = 0;
	if (write) {
		args[count++] = registers[p_in.c];
	} else if (list) {
		for (size_t i = 1; i < list->size(); i++) {
			args[count++] = registers[(*list)[i]];
		}
	}
	Slot result = any::undefined();
	std::optional<NamedCall> call;
	Wanted wanted{ write ? ir::Kind::Void : ir::Kind::Any, {} };
	if (site->math) {
		const uint32_t type = p_function.code[frame.pc].c;
		wanted.type = type == ir::NONE ? std::string_view{} : std::string_view(module_.engine_names[type].name);
		wanted.reuse = registers[site->math->target].p;
	} else if (p_in.op == ir::Op::DynGet && any::tag(registers[p_in.a]) == AnyTag::Value) {
		wanted.reuse = any::pointer(registers[p_in.a]);
	}
	if (!access_named(*site, p_in.op, registers[holder], text->view(), args, count, &result, &call, wanted)) {
		if (!call || count || module_.functions[call->function].parameters != 1 || module_.functions[call->function].rest) {
			return false;
		}
		Slot first{};
		first.p = call->receiver;
		retain(call->receiver);
		if (p_in.a != ir::NONE) {
			put(ir::Kind::Any, stack_[base + p_in.a], any::undefined());
		}
		push(call->function, &first, 1, p_in.a);
		release_object(call->receiver);
		return true;
	}
	if (!write && p_in.a != ir::NONE && !failing_) {
		if (site->math && wanted.reuse && any::tag(result) == AnyTag::Value && any::pointer(result) == wanted.reuse) {
			frames_.back().pc += site->math->skip;
		} else if (site->math || !wanted.reuse || result.bits != stack_[base + p_in.a].bits) {
			put(ir::Kind::Any, stack_[base + p_in.a], result);
		}
	} else if (!wanted.reuse || any::tag(result) != AnyTag::Value || any::pointer(result) != wanted.reuse) {
		drop_any(result);
	}
	return true;
}

} // namespace kas3::vm
