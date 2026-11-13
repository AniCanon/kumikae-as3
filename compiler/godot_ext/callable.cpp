#include "callable.h"

#include "program.h"

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

namespace kas3 {

namespace {

class BoundMethod : public CallableCustom {
	vm::Object *object_;
	uint32_t function_;
	ObjectID owner_; // Null for a class with no Godot object behind it.
	uint64_t program_; // Which load of the project's scripts the object belongs to.

	// Whether the script object can still be used: its program is the one loaded, and its owner lives.
	bool alive() const {
		return Project::generation() == program_ && (owner_.is_null() || ObjectDB::get_instance(owner_) != nullptr);
	}

public:
	BoundMethod(vm::Object *p_object, uint32_t p_function, Object *p_owner) :
			object_(p_object), function_(p_function), program_(Project::generation()) {
		if (p_owner) {
			owner_ = ObjectID(p_owner->get_instance_id());
		}
		vm::retain(object_);
	}
	~BoundMethod() override {
		// If the program has been unloaded since, its objects went with it.
		if (Project::generation() == program_) {
			Project::get().interpreter().release_object(object_);
		}
	}

	// The same method of the same object is the same listener.
	static bool equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		const BoundMethod *a = static_cast<const BoundMethod *>(p_a);
		const BoundMethod *b = static_cast<const BoundMethod *>(p_b);
		return a->object_ == b->object_ && a->function_ == b->function_ && a->program_ == b->program_;
	}
	static bool less(const CallableCustom *p_a, const CallableCustom *p_b) {
		const BoundMethod *a = static_cast<const BoundMethod *>(p_a);
		const BoundMethod *b = static_cast<const BoundMethod *>(p_b);
		return a->object_ != b->object_ ? a->object_ < b->object_ : a->function_ < b->function_;
	}

	uint32_t hash() const override {
		uint64_t mixed = (uint64_t)(uintptr_t)object_ * 0x9E3779B97F4A7C15ull + function_;
		return (uint32_t)(mixed ^ (mixed >> 32));
	}
	String get_as_text() const override {
		if (Project::generation() != program_) {
			return String("<unloaded ActionScript method>");
		}
		return String::utf8(Project::get().module().functions[function_].name.c_str());
	}
	CompareEqualFunc get_compare_equal_func() const override { return equal; }
	CompareLessFunc get_compare_less_func() const override { return less; }
	bool is_valid() const override { return alive(); }
	ObjectID get_object() const override { return owner_; }

	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, GDExtensionCallError &r_call_error) const override {
		if (!alive()) {
			// The tombstone: the object this method belongs to is gone.
			r_call_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
			return;
		}
		// The call may drop the last other reference to this callable (a listener that removes itself).
		vm::Object *object = object_;
		vm::retain(object);
		call_script(object, function_, p_arguments, p_argcount, &r_return_value, &r_call_error);
		if (Project::generation() == program_) {
			Project::get().interpreter().release_object(object);
		}
	}
};

} // namespace

Variant *make_callable(vm::Object *p_object, uint32_t p_function, Object *p_owner) {
	return new Variant(Callable(memnew(BoundMethod(p_object, p_function, p_owner))));
}

} // namespace kas3
