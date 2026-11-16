// The fast paths of properties by name and of a vector that grows by one (runtime/fast.h): an Object's or a
// Dictionary's property under a String key, read and written where the library's own tables keep it
// (vm/table.h), and one element added at a vector's end. Each is the library's rule for that one case
// (vm/interpreter_properties.cpp, vm/interpreter_vectors.cpp); every other case is left to it.
#include "fast.h"

#include "native_host.h"
#include "table.h"

namespace kas3 {
namespace rt {

using vm::AnyTag;

Object *holder_of_any(Context *, Slot p_any) {
	return vm::any::tag(p_any) == AnyTag::Object ? static_cast<Object *>(vm::any::pointer(p_any)) : nullptr;
}

Str *key_of_any(Context *, Slot p_any) {
	return vm::any::tag(p_any) == AnyTag::String ? static_cast<Str *>(vm::any::pointer(p_any)) : nullptr;
}

Slot *property(const Context *c, Object *p_holder, const Str *p_key) {
	if (!p_holder || !p_key || !c->classes[p_holder->klass].table) {
		return nullptr;
	}
	vm::Entries *entries = vm::entries_of(p_holder);
	const std::optional<uint32_t> at = entries->find_text(p_key->view());
	return at ? &entries->value_at(*at) : nullptr;
}

bool set_property(Context *c, Object *p_holder, Str *p_key, Slot p_value) {
	if (!p_holder || !p_key || !c->classes[p_holder->klass].table) {
		return false;
	}
	vm::Entries *entries = vm::entries_of(p_holder);
	if (const std::optional<uint32_t> at = entries->find_text(p_key->view())) {
		store(c, entries->value_at(*at), p_value);
		return true;
	}
	// A new property, last in order: the table holds the key and the value from here on.
	retain(c, p_key);
	entries->add(vm::any::string(p_key), shared(c, p_value));
	return true;
}

Object *new_array(Context *c, const Slot *p_values, uint32_t p_count) {
	const Slot asked[] = { slot(p_count), slot(false) };
	Vector *made = static_cast<Vector *>(c->host->builtin(Builtin::NewArray, asked).p);
	for (uint32_t i = 0; made && i < p_count; i++) {
		made->data[i] = shared(c, p_values[i]);
	}
	return made;
}

Slot *element_added(Context *, Object *p_vector, uint32_t p_index) {
	Vector *const vector = static_cast<Vector *>(p_vector);
	if (!vector || p_index != vector->length || vector->fixed || p_index >= vm::MAX_VECTOR_LENGTH || !vm::vector_reserve(vector, p_index + 1)) {
		return nullptr;
	}
	vector->length = p_index + 1;
	return vector->data + p_index; // Empty: the room past a vector's end always is.
}

} // namespace rt
} // namespace kas3
