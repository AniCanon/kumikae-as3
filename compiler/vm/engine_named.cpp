#include "engine_named.h"

namespace kas3 {
namespace vm {

using ir::Kind;

void EngineNamed::construct(uint32_t p_name, const std::vector<Slot> &p_given, uint32_t p_out) {
	std::vector<Argument> arguments;
	for (const Slot each : p_given) {
		arguments.push_back(Argument{ Kind::Any, each });
	}
	std::string error;
	void *made = vm_.host_.source.construct(vm_.module_.engine_names[p_name].name, arguments.data(), arguments.size(), &error);
	if (!made) {
		raise_engine(vm_.library_, error);
		return;
	}
	// Every object of Godot's is an Object; a value type is not.
	const bool object = vm_.host_.handles.is_class(made, "Object");
	vm_.put(Kind::Any, vm_.stack_[vm_.frames_.back().base + p_out], object ? any::godot_object(made) : any::godot_value(made));
}

} // namespace vm
} // namespace kas3
