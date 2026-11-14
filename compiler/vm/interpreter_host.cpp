#include "interpreter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

// One request to the engine. Raises an AS3 error if the engine refuses.
const char *Interpreter::host_op(const Function &p_function, const Instruction &p_in) {
	const size_t base = frames_.back().base;
	const size_t frame_index = frames_.size() - 1;
	Slot *r = stack_.data() + base;
	static const char *const NULL_ERROR = "Error #1009: Cannot access a property or method of a null object reference.";
	std::string error;
	bool ok = false;
	Slot result;
	result.bits = 0;
	Kind kind = Kind::Void;
	uint32_t target = NONE;
	// What the engine gives must be what the register was declared to hold.
	auto wanted = [&](uint32_t p_target) {
		Wanted want;
		if (p_target != NONE) {
			want.kind = p_function.registers[p_target];
			if (p_target < p_function.engine_types.size()) {
				want.type = p_function.engine_types[p_target];
			}
		}
		return want;
	};
	Argument arguments[16];
	std::vector<Argument> many;
	Argument *values = arguments;
	size_t count = 0;
	auto gather = [&](const std::vector<uint32_t> &p_list, size_t p_from) {
		count = p_list.size() - p_from;
		if (count > 16) {
			many.resize(count);
			values = many.data();
		}
		for (size_t i = 0; i < count; i++) {
			values[i].kind = p_function.registers[p_list[p_from + i]];
			values[i].slot = r[p_list[p_from + i]];
		}
	};
	switch (p_in.op) {
		case Op::HostNew: {
			gather(p_function.argument_lists[p_in.c], 0);
			target = p_in.a;
			kind = Kind::Val;
			result.p = host_.values.construct(module_.strings[p_in.b], values, count, &error);
			ok = result.p != nullptr;
			break;
		}
		case Op::HostCall: {
			const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
			void *receiver = list.empty() ? nullptr : r[list[0]].p;
			if (!receiver) {
				raise_null();
				return NULL_ERROR;
			}
			gather(list, 1);
			target = p_in.a;
			kind = target == NONE ? Kind::Void : p_function.registers[target];
			ok = host_.objects.call(receiver, module_.strings[p_in.b], values, count, wanted(target), target == NONE ? nullptr : &result, &error);
			break;
		}
		case Op::HostOperate: {
			gather(p_function.argument_lists[p_in.c], 0);
			const std::string &op = module_.strings[p_in.b];
			// Only a comparison takes a missing value: `node == null`. Arithmetic on one is an error.
			if (op != "==" && op != "!=") {
				for (size_t i = 0; i < count; i++) {
					if (ir::is_engine(values[i].kind) && !values[i].slot.p) {
						raise_null();
						return NULL_ERROR;
					}
				}
			}
			target = p_in.a;
			kind = p_function.registers[target];
			ok = host_.values.operate(op, values, count, wanted(target), &result, &error);
			break;
		}
		case Op::HostSingleton: {
			target = p_in.a;
			kind = Kind::Ref;
			result.p = host_.objects.singleton(module_.strings[p_in.b], &error);
			ok = result.p != nullptr;
			break;
		}
		case Op::HostConstant: {
			target = p_in.a;
			kind = p_function.registers[target];
			ok = host_.values.constant(module_.strings[p_in.b], module_.strings[p_in.c], wanted(target), &result, &error);
			break;
		}
		case Op::HostCallStatic: {
			gather(p_function.argument_lists[p_in.c], 0);
			// "Vector2.from_angle": the type, and the method.
			std::string_view name = module_.strings[p_in.b];
			size_t dot = name.rfind('.');
			target = p_in.a;
			kind = target == NONE ? Kind::Void : p_function.registers[target];
			ok = host_.objects.call_static(name.substr(0, dot), name.substr(dot + 1), values, count, wanted(target), target == NONE ? nullptr : &result, &error);
			break;
		}
		case Op::HostToStr: {
			target = p_in.a;
			kind = Kind::Str;
			result.p = r[p_in.b].p ? host_.values.text(r[p_in.b].p) : nullptr;
			if (!result.p) {
				retain(null_text_);
				result.p = null_text_;
			}
			ok = true;
			break;
		}
		case Op::HostGet: {
			void *receiver = r[p_in.b].p;
			if (!receiver) {
				raise_null();
				return NULL_ERROR;
			}
			target = p_in.a;
			kind = p_function.registers[target];
			ok = host_.objects.get(receiver, module_.strings[p_in.c], wanted(target), &result, &error);
			break;
		}
		case Op::HostBind: {
			Object *object = static_cast<Object *>(r[p_in.b].p);
			if (!object) {
				raise_null();
				return NULL_ERROR;
			}
			const ir::Class &klass = module_.classes[object->klass];
			uint32_t function = (p_in.c & ir::BIND_SLOT) ? klass.methods[p_in.c & ~ir::BIND_SLOT] : p_in.c;
			target = p_in.a;
			kind = Kind::Val;
			result.p = host_.objects.bind(object, function, klass.native_base.empty() ? nullptr : object->fields()[0].p, &error);
			ok = result.p != nullptr;
			break;
		}
		case Op::HostMember: {
			void *receiver = r[p_in.b].p;
			const Str *name = static_cast<const Str *>(r[p_in.c].p);
			if (!receiver || !name) {
				raise_null();
				return NULL_ERROR;
			}
			target = p_in.a;
			kind = p_function.registers[target];
			if (p_function.registers[p_in.b] == Kind::Obj) {
				// A script object: one of its own fields by that name, or else the Godot object behind it.
				Object *object = static_cast<Object *>(receiver);
				const ir::Class &klass = module_.classes[object->klass];
				bool own = false;
				for (size_t i = klass.fields.size(); i-- > 0 && !own;) {
					if (klass.fields[i].name == name->view()) {
						own = true;
						if (klass.fields[i].kind != Kind::Val) {
							error = "'" + std::string(name->view()) + "' is not a signal";
						} else {
							result.p = host_.values.copy(object->fields()[i].p);
							ok = true;
						}
					}
				}
				if (own) {
					break;
				}
				receiver = klass.native_base.empty() ? nullptr : object->fields()[0].p;
				if (!receiver) {
					error = "'" + std::string(name->view()) + "' is not a signal";
					break;
				}
			}
			ok = host_.objects.member(receiver, name->view(), wanted(target), &result, &error);
			break;
		}
		case Op::HostIndexGet: {
			void *receiver = r[p_in.b].p;
			if (!receiver) {
				raise_null();
				return NULL_ERROR;
			}
			Argument key;
			key.kind = p_function.registers[p_in.c];
			key.slot = r[p_in.c];
			target = p_in.a;
			kind = p_function.registers[target];
			ok = host_.values.index_get(receiver, key, wanted(target), &result, &error);
			break;
		}
		case Op::HostIndexSet: {
			void *receiver = r[p_in.a].p;
			if (!receiver) {
				raise_null();
				return NULL_ERROR;
			}
			Argument key, value;
			key.kind = p_function.registers[p_in.b];
			key.slot = r[p_in.b];
			value.kind = p_function.registers[p_in.c];
			value.slot = r[p_in.c];
			ok = host_.values.index_set(receiver, key, value, &error);
			break;
		}
		default: {
			void *receiver = r[p_in.a].p;
			if (!receiver) {
				raise_null();
				return NULL_ERROR;
			}
			Argument value;
			value.kind = p_function.registers[p_in.c];
			value.slot = r[p_in.c];
			ok = host_.objects.set(receiver, module_.strings[p_in.b], value, &error);
			break;
		}
	}
	if (!ok) {
		raise(module_.error_class, error.empty() ? "the engine refused" : error);
		return "refused";
	}
	if (target != NONE && frame_index < frames_.size()) {
		put(kind, stack_[frames_[frame_index].base + target], result);
	}
	return nullptr;
}

} // namespace vm
} // namespace kas3
