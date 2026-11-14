#include "interpreter_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

enum class Outcome {
	Done,
	Refused, // The engine said no: `error` says why.
	Missing, // The request was made of nothing: AS3's null reference error.
};

// One request to the engine, while it is being made. The registers are read before the engine is called
// and never after: the engine may call back into AS3, which can move them.
struct Request {
	const ir::Module &module;
	Host &host;
	const Function &function;
	const Instruction &in;
	Slot *registers;
	Str *null_text;

	std::string error;
	Slot result{};
	Kind kind = Kind::Void;
	uint32_t target = NONE; // The register the result goes to, if any.

	Request(const ir::Module &p_module, Host &p_host, const Function &p_function, const Instruction &p_in, Slot *p_registers, Str *p_null_text) :
			module(p_module), host(p_host), function(p_function), in(p_in), registers(p_registers), null_text(p_null_text) {}

	const std::string &text(uint32_t p_string) const { return module.strings[p_string]; }
	void *handle(uint32_t p_register) const { return registers[p_register].p; }
	Argument argument(uint32_t p_register) const { return Argument{ function.registers[p_register], registers[p_register] }; }

	// The result goes to this register, as the kind it was declared to hold.
	void into(uint32_t p_target) {
		target = p_target;
		kind = p_target == NONE ? Kind::Void : function.registers[p_target];
	}
	// What the engine gives must be what the register was declared to hold.
	Wanted wanted() const {
		Wanted want;
		if (target != NONE) {
			want.kind = function.registers[target];
			if (target < function.engine_types.size()) {
				want.type = function.engine_types[target];
			}
		}
		return want;
	}
	Slot *result_or_none() { return target == NONE ? nullptr : &result; }
	Outcome done(bool p_ok) const { return p_ok ? Outcome::Done : Outcome::Refused; }
};

// The arguments of a call, copied out of their registers. Up to sixteen sit on the stack.
class Gathered {
	Argument few_[16];
	std::vector<Argument> many_;
	Argument *values_ = few_;
	size_t count_ = 0;

public:
	Gathered(const Request &p_request, uint32_t p_list, size_t p_from) {
		const std::vector<uint32_t> &list = p_request.function.argument_lists[p_list];
		count_ = list.size() - p_from;
		if (count_ > 16) {
			many_.resize(count_);
			values_ = many_.data();
		}
		for (size_t i = 0; i < count_; i++) {
			values_[i] = p_request.argument(list[p_from + i]);
		}
	}
	const Argument *values() const { return values_; }
	size_t count() const { return count_; }
};

Outcome construct(Request &p_request) {
	const Gathered arguments(p_request, p_request.in.c, 0);
	p_request.target = p_request.in.a;
	p_request.kind = Kind::Val;
	p_request.result.p = p_request.host.values.construct(p_request.text(p_request.in.b), arguments.values(), arguments.count(), &p_request.error);
	return p_request.done(p_request.result.p != nullptr);
}

Outcome call(Request &p_request) {
	const std::vector<uint32_t> &list = p_request.function.argument_lists[p_request.in.c];
	void *receiver = list.empty() ? nullptr : p_request.handle(list[0]);
	if (!receiver) {
		return Outcome::Missing;
	}
	const Gathered arguments(p_request, p_request.in.c, 1);
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.objects.call(receiver, p_request.text(p_request.in.b), arguments.values(), arguments.count(),
			p_request.wanted(), p_request.result_or_none(), &p_request.error));
}

Outcome operate(Request &p_request) {
	const Gathered arguments(p_request, p_request.in.c, 0);
	const std::string &op = p_request.text(p_request.in.b);
	// Only a comparison takes a missing value: `node == null`. Arithmetic on one is an error.
	if (op != "==" && op != "!=") {
		for (size_t i = 0; i < arguments.count(); i++) {
			if (ir::is_engine(arguments.values()[i].kind) && !arguments.values()[i].slot.p) {
				return Outcome::Missing;
			}
		}
	}
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.values.operate(op, arguments.values(), arguments.count(), p_request.wanted(), &p_request.result,
			&p_request.error));
}

Outcome singleton(Request &p_request) {
	p_request.target = p_request.in.a;
	p_request.kind = Kind::Ref;
	p_request.result.p = p_request.host.objects.singleton(p_request.text(p_request.in.b), &p_request.error);
	return p_request.done(p_request.result.p != nullptr);
}

Outcome constant(Request &p_request) {
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.values.constant(p_request.text(p_request.in.b), p_request.text(p_request.in.c), p_request.wanted(),
			&p_request.result, &p_request.error));
}

Outcome call_static(Request &p_request) {
	const Gathered arguments(p_request, p_request.in.c, 0);
	// "Vector2.from_angle": the type, and the method.
	const std::string_view name = p_request.text(p_request.in.b);
	const size_t dot = name.rfind('.');
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.objects.call_static(name.substr(0, dot), name.substr(dot + 1), arguments.values(), arguments.count(),
			p_request.wanted(), p_request.result_or_none(), &p_request.error));
}

// Godot's text for a value or an object; "null" for a missing one.
Outcome to_text(Request &p_request) {
	p_request.target = p_request.in.a;
	p_request.kind = Kind::Str;
	void *value = p_request.handle(p_request.in.b);
	p_request.result.p = value ? p_request.host.values.text(value) : nullptr;
	if (!p_request.result.p) {
		retain(p_request.null_text);
		p_request.result.p = p_request.null_text;
	}
	return Outcome::Done;
}

Outcome get(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.b);
	if (!receiver) {
		return Outcome::Missing;
	}
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.objects.get(receiver, p_request.text(p_request.in.c), p_request.wanted(), &p_request.result,
			&p_request.error));
}

Outcome set(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.a);
	if (!receiver) {
		return Outcome::Missing;
	}
	return p_request.done(p_request.host.objects.set(receiver, p_request.text(p_request.in.b), p_request.argument(p_request.in.c), &p_request.error));
}

Outcome bind(Request &p_request) {
	Object *object = static_cast<Object *>(p_request.handle(p_request.in.b));
	if (!object) {
		return Outcome::Missing;
	}
	const ir::Class &klass = p_request.module.classes[object->klass];
	const uint32_t operand = p_request.in.c;
	const uint32_t function = (operand & ir::BIND_SLOT) ? klass.methods[operand & ~ir::BIND_SLOT] : operand;
	p_request.target = p_request.in.a;
	p_request.kind = Kind::Val;
	p_request.result.p = p_request.host.objects.bind(object, function, klass.native_base.empty() ? nullptr : object->fields()[0].p, &p_request.error);
	return p_request.done(p_request.result.p != nullptr);
}

// `object[name]` on a script object: one of its own fields by that name, which must hold a signal. Says
// whether there is such a field; if not, the Godot object behind the script object is asked.
bool own_member(Request &p_request, Object *p_object, std::string_view p_name, Outcome *r_outcome) {
	const ir::Class &klass = p_request.module.classes[p_object->klass];
	for (size_t i = klass.fields.size(); i-- > 0;) {
		if (klass.fields[i].name != p_name) {
			continue;
		}
		if (klass.fields[i].kind != Kind::Val) {
			p_request.error = "'" + std::string(p_name) + "' is not a signal";
			*r_outcome = Outcome::Refused;
		} else {
			p_request.result.p = p_request.host.values.copy(p_object->fields()[i].p);
			*r_outcome = Outcome::Done;
		}
		return true;
	}
	return false;
}

Outcome member(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.b);
	const Str *name = static_cast<const Str *>(p_request.handle(p_request.in.c));
	if (!receiver || !name) {
		return Outcome::Missing;
	}
	p_request.into(p_request.in.a);
	if (p_request.function.registers[p_request.in.b] == Kind::Obj) {
		Object *object = static_cast<Object *>(receiver);
		Outcome outcome = Outcome::Done;
		if (own_member(p_request, object, name->view(), &outcome)) {
			return outcome;
		}
		const ir::Class &klass = p_request.module.classes[object->klass];
		receiver = klass.native_base.empty() ? nullptr : object->fields()[0].p;
		if (!receiver) {
			p_request.error = "'" + std::string(name->view()) + "' is not a signal";
			return Outcome::Refused;
		}
	}
	return p_request.done(p_request.host.objects.member(receiver, name->view(), p_request.wanted(), &p_request.result, &p_request.error));
}

Outcome index_get(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.b);
	if (!receiver) {
		return Outcome::Missing;
	}
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.values.index_get(receiver, p_request.argument(p_request.in.c), p_request.wanted(), &p_request.result,
			&p_request.error));
}

Outcome index_set(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.a);
	if (!receiver) {
		return Outcome::Missing;
	}
	return p_request.done(p_request.host.values.index_set(receiver, p_request.argument(p_request.in.b), p_request.argument(p_request.in.c),
			&p_request.error));
}

Outcome perform(Request &p_request) {
	switch (p_request.in.op) {
		case Op::HostNew:
			return construct(p_request);
		case Op::HostCall:
			return call(p_request);
		case Op::HostOperate:
			return operate(p_request);
		case Op::HostSingleton:
			return singleton(p_request);
		case Op::HostConstant:
			return constant(p_request);
		case Op::HostCallStatic:
			return call_static(p_request);
		case Op::HostToStr:
			return to_text(p_request);
		case Op::HostGet:
			return get(p_request);
		case Op::HostBind:
			return bind(p_request);
		case Op::HostMember:
			return member(p_request);
		case Op::HostIndexGet:
			return index_get(p_request);
		case Op::HostIndexSet:
			return index_set(p_request);
		default:
			return set(p_request);
	}
}

} // namespace

// One request to the engine. Raises an AS3 error if the engine refuses.
void Interpreter::host_op(const Function &p_function, const Instruction &p_in) {
	const size_t frame_index = frames_.size() - 1;
	Request request(module_, host_, p_function, p_in, stack_.data() + frames_.back().base, null_text_);
	switch (perform(request)) {
		case Outcome::Missing:
			raise_null();
			return;
		case Outcome::Refused:
			raise(module_.error_class, request.error.empty() ? "the engine refused" : request.error);
			return;
		case Outcome::Done:
			break;
	}
	// The frame is found again by its number: the engine may have called back in and moved the stack.
	if (request.target != NONE && frame_index < frames_.size()) {
		put(request.kind, stack_[frames_[frame_index].base + request.target], request.result);
	}
}

} // namespace vm
} // namespace kas3
