// An object of a class and an Object or a Dictionary made, as functions of plain values (vm/library.h).
#include "library.h"

#include "table.h"

namespace kas3 {
namespace vm {

using ir::Kind;

Object *object_new(Library &p_library, uint32_t p_class) {
	const ir::Class &klass = p_library.module().classes[p_class];
	Object *object = new_object(p_class, slots_of(klass), p_library.heap.list_for(p_class));
	if (!object) {
		p_library.runner.out_of_memory();
		return nullptr;
	}
	reset_fields(klass, object);
	if (!klass.native_base.empty() && !klass.fields.empty() && klass.fields[0].kind == Kind::Ref) {
		// A script class that extends one of Godot's: the Godot object is made, with the script attached.
		std::string error;
		void *owner = p_library.heap.source.attach_new(p_class, object, &error);
		if (!owner) {
			p_library.heap.release_object(object);
			p_library.runner.raise(p_library.module().error_class, error.empty() ? "the engine could not make a '" + klass.name + "'" : error);
			return nullptr;
		}
		owner_field(object).p = owner;
	}
	return object;
}

Object *table_new(Library &p_library, uint32_t p_class, bool p_weak) {
	if (p_weak) {
		p_library.raise_unsupported("a Dictionary with weak keys");
		return nullptr;
	}
	Object *made = new_table(p_class, p_library.heap.list_for(p_class));
	if (!made) {
		p_library.runner.out_of_memory();
	}
	return made;
}

} // namespace vm
} // namespace kas3
