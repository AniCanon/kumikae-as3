#include "interpreter_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

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
			module(p_module), host(p_host), function(p_function), in(p_in), registers(p_registers), null_text(p_null_text) {
		result.bits = 0; // All of it: `{}` on a union clears its first member only, which is half a pointer.
	}

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
	p_request.into(p_request.in.a); // One of Godot's values, or one of its objects.
	p_request.result.p = p_request.host.source.construct(p_request.text(p_request.in.b), arguments.values(), arguments.count(), &p_request.error);
	return p_request.done(p_request.result.p != nullptr);
}

// A vector handed to the engine: its elements, as they are held, become one of Godot's collections.
// No vector gives nothing: Godot's null.
Outcome collection(Request &p_request) {
	const Vector *vector = static_cast<const Vector *>(p_request.handle(p_request.in.b));
	p_request.target = p_request.in.a;
	p_request.kind = Kind::Val;
	if (!vector) {
		return Outcome::Done;
	}
	const Kind element = p_request.module.classes[vector->klass].element;
	std::vector<Argument> elements(vector->length);
	for (uint32_t i = 0; i < vector->length; i++) {
		elements[i] = Argument{ element, vector->data[i] };
	}
	p_request.result.p = p_request.host.source.collection(p_request.text(p_request.in.c), elements.data(), elements.size(), &p_request.error);
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
	return p_request.done(p_request.host.handles.call(receiver, p_request.text(p_request.in.b), arguments.values(), arguments.count(),
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
	return p_request.done(p_request.host.handles.operate(op, arguments.values(), arguments.count(), p_request.wanted(), &p_request.result,
			&p_request.error));
}

Outcome singleton(Request &p_request) {
	p_request.target = p_request.in.a;
	p_request.kind = Kind::Ref;
	p_request.result.p = p_request.host.source.singleton(p_request.text(p_request.in.b), &p_request.error);
	return p_request.done(p_request.result.p != nullptr);
}

Outcome evaluate(Request &p_request) {
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.source.evaluate(p_request.text(p_request.in.b), p_request.wanted(), &p_request.result, &p_request.error));
}

Outcome constant(Request &p_request) {
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.source.constant(p_request.text(p_request.in.b), p_request.text(p_request.in.c), p_request.wanted(),
			&p_request.result, &p_request.error));
}

Outcome call_static(Request &p_request) {
	const Gathered arguments(p_request, p_request.in.c, 0);
	// "Vector2.from_angle": the type, and the method.
	const std::string_view name = p_request.text(p_request.in.b);
	const size_t dot = name.rfind('.');
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.source.call_static(name.substr(0, dot), name.substr(dot + 1), arguments.values(), arguments.count(),
			p_request.wanted(), p_request.result_or_none(), &p_request.error));
}

Outcome present(Request &p_request) {
	p_request.target = p_request.in.a;
	p_request.kind = Kind::Bool;
	void *value = p_request.handle(p_request.in.b);
	p_request.result.b = value != nullptr && p_request.host.handles.present(value);
	return Outcome::Done;
}

// `is` and `as` with one of Godot's classes.
Outcome class_test(Request &p_request) {
	void *value = p_request.handle(p_request.in.b);
	const bool fits = value != nullptr && p_request.host.handles.is_class(value, p_request.text(p_request.in.c));
	p_request.target = p_request.in.a;
	if (p_request.in.op == Op::HostIsClass) {
		p_request.kind = Kind::Bool;
		p_request.result.b = fits;
	} else {
		p_request.kind = Kind::Ref;
		p_request.result.p = fits ? p_request.host.handles.copy(value) : nullptr;
	}
	return Outcome::Done;
}

// Godot's text for a value or an object; "null" for a missing one.
Outcome to_text(Request &p_request) {
	p_request.target = p_request.in.a;
	p_request.kind = Kind::Str;
	void *value = p_request.handle(p_request.in.b);
	p_request.result.p = value ? p_request.host.handles.text(value) : nullptr;
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
	return p_request.done(p_request.host.handles.get(receiver, p_request.text(p_request.in.c), p_request.wanted(), &p_request.result,
			&p_request.error));
}

Outcome set(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.a);
	if (!receiver) {
		return Outcome::Missing;
	}
	return p_request.done(p_request.host.handles.set(receiver, p_request.text(p_request.in.b), p_request.argument(p_request.in.c), &p_request.error));
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
	p_request.result.p = p_request.host.source.bind(object, function, klass.native_base.empty() ? nullptr : object->fields()[0].p, &p_request.error);
	return p_request.done(p_request.result.p != nullptr);
}

std::string not_a_signal(std::string_view p_name) {
	return "'" + std::string(p_name) + "' is not a signal";
}

// `object[name]` on a script object: one of its own fields by that name, which must hold a signal.
// Nothing if it has no such field: the Godot object behind the script object is then asked.
std::optional<Outcome> own_member(Request &p_request, Object *p_object, std::string_view p_name) {
	const ir::Class &klass = p_request.module.classes[p_object->klass];
	for (size_t i = klass.fields.size(); i-- > 0;) {
		if (klass.fields[i].name != p_name) {
			continue;
		}
		if (klass.fields[i].kind != Kind::Val) {
			p_request.error = not_a_signal(p_name);
			return Outcome::Refused;
		}
		p_request.result.p = p_request.host.handles.copy(p_object->fields()[i].p);
		return Outcome::Done;
	}
	return std::nullopt;
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
		if (const std::optional<Outcome> own = own_member(p_request, object, name->view())) {
			return *own;
		}
		const ir::Class &klass = p_request.module.classes[object->klass];
		receiver = klass.native_base.empty() ? nullptr : object->fields()[0].p;
		if (!receiver) {
			p_request.error = not_a_signal(name->view());
			return Outcome::Refused;
		}
	}
	return p_request.done(p_request.host.handles.member(receiver, name->view(), p_request.wanted(), &p_request.result, &p_request.error));
}

Outcome index_get(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.b);
	if (!receiver) {
		return Outcome::Missing;
	}
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.handles.index_get(receiver, p_request.argument(p_request.in.c), p_request.wanted(), &p_request.result,
			&p_request.error));
}

Outcome index_set(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.a);
	if (!receiver) {
		return Outcome::Missing;
	}
	return p_request.done(p_request.host.handles.index_set(receiver, p_request.argument(p_request.in.b), p_request.argument(p_request.in.c),
			&p_request.error));
}

Outcome perform(Request &p_request) {
	switch (p_request.in.op) {
		case Op::HostNew:
			return construct(p_request);
		case Op::HostCollection:
			return collection(p_request);
		case Op::HostCall:
			return call(p_request);
		case Op::HostOperate:
			return operate(p_request);
		case Op::HostSingleton:
			return singleton(p_request);
		case Op::HostConstant:
			return constant(p_request);
		case Op::HostDefault:
			return evaluate(p_request);
		case Op::HostCallStatic:
			return call_static(p_request);
		case Op::HostToStr:
			return to_text(p_request);
		case Op::HostPresent:
			return present(p_request);
		case Op::HostIsClass:
		case Op::HostAsClass:
			return class_test(p_request);
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
// A script object inside an untyped value has no form Godot can hold: said before Godot is asked.
static bool hands_script_object(const Function &p_function, const Instruction &p_in, const Slot *p_registers) {
	const char *shape = ir::op_shape(p_in.op);
	const uint32_t operands[3] = { p_in.a, p_in.b, p_in.c };
	const auto object = [&](uint32_t p_register) {
		return p_function.registers[p_register] == Kind::Any && any::tag(p_registers[p_register]) == AnyTag::Object;
	};
	for (int n = 1; n < 3; n++) {
		if (shape[n] == 'R' && object(operands[n])) {
			return true;
		}
		if (shape[n] == 'A') {
			for (uint32_t each : p_function.argument_lists[operands[n]]) {
				if (object(each)) {
					return true;
				}
			}
		}
	}
	return false;
}

void Interpreter::host_op(const Function &p_function, const Instruction &p_in) {
	const size_t frame_index = frames_.size() - 1;
	if (hands_script_object(p_function, p_in, stack_.data() + frames_.back().base)) {
		raise(module_.type_error_class, "a script object can't be given to Godot: only Godot's own values and objects can.");
		return;
	}
	Request request(module_, host_, p_function, p_in, stack_.data() + frames_.back().base, null_text_);
	switch (perform(request)) {
		case Outcome::Missing:
			raise_null();
			return;
		case Outcome::Refused: {
			// The engine's side may say that an error is AS3's TypeError: see engine_handles.h.
			static const std::string_view type_error = "TypeError: ";
			static const std::string_view range_error = "RangeError: ";
			if (request.error.compare(0, type_error.size(), type_error) == 0) {
				raise(module_.type_error_class, request.error.substr(type_error.size()));
			} else if (request.error.compare(0, range_error.size(), range_error) == 0) {
				raise(module_.range_error_class, request.error.substr(range_error.size()));
			} else {
				raise(module_.error_class, request.error.empty() ? "the engine refused" : request.error);
			}
			return;
		}
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
