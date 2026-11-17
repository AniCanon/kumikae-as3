#include "library.h"

#include <cstdlib>

namespace kas3 {
namespace vm {

Str *Library::text(std::string_view p_text) {
	Str *made = new_string(p_text);
	if (!made) {
		runner.out_of_memory();
	}
	return made;
}

Vector *Library::fresh_vector(uint32_t p_class, uint64_t p_length) {
	Vector *fresh = p_length <= MAX_VECTOR_LENGTH ? new_vector(p_class, static_cast<uint32_t>(p_length), false, heap.list_for(p_class)) : nullptr;
	if (!fresh) {
		runner.out_of_memory();
	}
	return fresh;
}

void Library::raise_null() {
	runner.raise(heap.module.type_error_class, "Error #1009: Cannot access a property or method of a null object reference.");
}

void Library::raise_overflow() {
	runner.raise(heap.module.error_class, "Error #1023: Stack overflow occurred.");
}

void Library::raise_unsupported(const std::string &p_what) {
	runner.raise(heap.module.error_class, p_what + " is not supported by the interpreter yet");
}

Object *new_error(Heap &p_heap, uint32_t p_class, Str *p_message) {
	const ir::Module &module = p_heap.module;
	Object *error = p_class != ir::NONE ? new_object(p_class, p_heap.object_slots(p_class), p_heap.list_for(p_class)) : nullptr;
	if (!error) {
		if (release(p_message)) {
			free_string(p_message);
		}
		return nullptr;
	}
	own_field(error, ir::ERROR_MESSAGE).p = p_message;
	own_field(error, ir::ERROR_NAME).p = new_string(module.classes[p_class].name);
	const std::string_view words = p_message ? p_message->view() : std::string_view();
	own_field(error, ir::ERROR_ID).i = words.compare(0, 7, "Error #") == 0 ? std::atoi(std::string(words.substr(7, 6)).c_str()) : 0;
	return error;
}

} // namespace vm
} // namespace kas3
