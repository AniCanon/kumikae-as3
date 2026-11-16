#include "interpreter_internal.h"
#include "table.h"

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
	void **singleton = nullptr; // For HostSingleton: where the singleton is kept once it is found.

	std::string error;
	Slot result{};
	Kind kind = Kind::Void;
	uint32_t target = NONE; // The register the result goes to, if any.

	Request(const ir::Module &p_module, Host &p_host, const Function &p_function, const Instruction &p_in, Slot *p_registers, Str *p_null_text) :
			module(p_module), host(p_host), function(p_function), in(p_in), registers(p_registers), null_text(p_null_text) {
		result.bits = 0; // All of it: `{}` on a union clears its first member only, which is half a pointer.
	}

	// Godot's name for what an engine instruction names (an E operand).
	const ir::EngineName &engine(uint32_t p_name) const { return module.engine_names[p_name]; }
	const std::string &name(uint32_t p_name) const { return module.engine_names[p_name].name; }
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
			// "Variant" is any of Godot's values: no one type is asked for.
			if (target < function.engine_types.size() && function.engine_types[target] != ir::ANY_GODOT_TYPE) {
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
	p_request.result.p = p_request.host.source.construct(p_request.name(p_request.in.b), arguments.values(), arguments.count(), &p_request.error);
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
	p_request.result.p = p_request.host.source.collection(p_request.name(p_request.in.c), element, vector->data, vector->length, &p_request.error);
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
	return p_request.done(p_request.host.handles.call(receiver, p_request.name(p_request.in.b), arguments.values(), arguments.count(),
			p_request.wanted(), p_request.result_or_none(), &p_request.error));
}

Outcome operate(Request &p_request) {
	const Gathered arguments(p_request, p_request.in.c, 0);
	// The description's name, but for the unary ones ("unary-"): AS3's spelling, with one operand.
	std::string_view op = p_request.name(p_request.in.b);
	if (op.substr(0, 5) == "unary") {
		op.remove_prefix(5);
	}
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

// Looked up once, then kept: a singleton lives as long as the engine.
Outcome singleton(Request &p_request) {
	p_request.target = p_request.in.a;
	p_request.kind = Kind::Ref;
	if (!*p_request.singleton) {
		*p_request.singleton = p_request.host.source.singleton(p_request.name(p_request.in.b), &p_request.error);
		if (!*p_request.singleton) {
			return Outcome::Refused;
		}
	}
	p_request.result.p = p_request.host.handles.copy(*p_request.singleton);
	return Outcome::Done;
}

Outcome evaluate(Request &p_request) {
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.source.evaluate(p_request.name(p_request.in.b), p_request.wanted(), &p_request.result, &p_request.error));
}

Outcome constant(Request &p_request) {
	p_request.into(p_request.in.a);
	const ir::EngineName &constant = p_request.engine(p_request.in.b);
	return p_request.done(p_request.host.source.constant(constant.owner, constant.name, p_request.wanted(), &p_request.result, &p_request.error));
}

// A class's method ("Vector2", and its whole entry, "from_angle method 889263119 Vector2 float"), or a
// global function (no class, and its signature).
Outcome call_static(Request &p_request) {
	const Gathered arguments(p_request, p_request.in.c, 0);
	const ir::EngineName &method = p_request.engine(p_request.in.b);
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.source.call_static(method.owner, method.value, arguments.values(), arguments.count(), p_request.wanted(),
			p_request.result_or_none(), &p_request.error));
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
	const bool fits = value != nullptr && p_request.host.handles.is_class(value, p_request.name(p_request.in.c));
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
	if (!value) {
		retain(p_request.null_text);
		p_request.result.p = p_request.null_text;
		return Outcome::Done;
	}
	p_request.result.p = p_request.host.handles.text(value);
	if (!p_request.result.p) {
		p_request.error = std::string(OUT_OF_MEMORY);
		return Outcome::Refused;
	}
	return Outcome::Done;
}

Outcome get(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.b);
	if (!receiver) {
		return Outcome::Missing;
	}
	p_request.into(p_request.in.a);
	return p_request.done(p_request.host.handles.get(receiver, p_request.name(p_request.in.c), p_request.wanted(), &p_request.result,
			&p_request.error));
}

Outcome set(Request &p_request) {
	void *receiver = p_request.handle(p_request.in.a);
	if (!receiver) {
		return Outcome::Missing;
	}
	return p_request.done(p_request.host.handles.set(receiver, p_request.name(p_request.in.b), p_request.argument(p_request.in.c), &p_request.error));
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

// The class of the object's that declares its field `p_field`: the first, from the object's own class down
// its bases, whose base has fewer fields.
uint32_t declaring_class(const ir::Module &p_module, uint32_t p_class, size_t p_field) {
	uint32_t at = p_class;
	while (p_module.classes[at].base != NONE && p_module.classes[p_module.classes[at].base].fields.size() > p_field) {
		at = p_module.classes[at].base;
	}
	return at;
}

bool extends_class(const ir::Module &p_module, uint32_t p_class, uint32_t p_base) {
	for (uint32_t at = p_class; at != NONE; at = p_module.classes[at].base) {
		if (at == p_base) {
			return true;
		}
	}
	return false;
}

// Whether the code running may reach the field by name, as AS3 sees names: a public one from anywhere, a
// protected one from the code of its class and of subclasses, any other from the code of its class.
bool field_seen(const Request &p_request, uint32_t p_class, size_t p_field) {
	const ir::Field &field = p_request.module.classes[p_class].fields[p_field];
	const uint32_t code = p_request.function.owner != NONE ? p_request.function.owner : p_request.function.written_in;
	if (field.is_public || code == NONE) {
		return field.is_public;
	}
	const uint32_t declared = declaring_class(p_request.module, p_class, p_field);
	return declared == code || (field.is_protected && extends_class(p_request.module, code, declared));
}

// `object[name]` on a script object: one of its fields by that name that the code may see, which must hold
// a signal. A public field comes first, a subclass's before a base's: a base's private field does not hide
// a subclass's public one. Nothing if it has none: the Godot object behind the script object is asked.
std::optional<Outcome> own_member(Request &p_request, Object *p_object, std::string_view p_name) {
	const ir::Class &klass = p_request.module.classes[p_object->klass];
	std::optional<size_t> found, hidden;
	for (size_t i = klass.fields.size(); i-- > 0 && !found;) {
		if (klass.fields[i].name != p_name || !field_seen(p_request, p_object->klass, i)) {
			continue;
		}
		if (klass.fields[i].is_public) {
			found = i;
		} else if (!hidden) {
			hidden = i;
		}
	}
	found = found ? found : hidden;
	if (!found) {
		return std::nullopt;
	}
	const ir::Field &field = klass.fields[*found];
	if (field.kind != Kind::Val || (!field.engine_type.empty() && field.engine_type != "Signal")) {
		p_request.error = not_a_signal(p_name);
		return Outcome::Refused;
	}
	void *held = p_object->fields()[*found].p;
	p_request.result.p = held ? p_request.host.handles.copy(held) : nullptr;
	return Outcome::Done;
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
void Interpreter::host_op(const Function &p_function, const Instruction &p_in) {
	const size_t frame_index = frames_.size() - 1;
	if (const std::optional<std::string> why = refused_by_godot(p_function, p_in, stack_.data() + frames_.back().base)) {
		raise(module_.type_error_class, *why);
		return;
	}
	Request request(module_, host_, p_function, p_in, stack_.data() + frames_.back().base, null_text_);
	if (p_in.op == Op::HostSingleton) {
		if (singletons_.size() <= p_in.b) {
			singletons_.resize(module_.engine_names.size(), nullptr);
		}
		request.singleton = &singletons_[p_in.b];
	}
	// A call the engine made back into this program may have failed: run() raises that error at the end of
	// this instruction, in place of any the outcome raises.
	const Outcome outcome = perform(request);
	switch (outcome) {
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
