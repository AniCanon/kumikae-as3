#include "bound_method.h"

#include "any.h"
#include "instance.h"

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace godot;

namespace kas3 {

std::unordered_set<BoundMethod *> &bound_methods() {
	static std::unordered_set<BoundMethod *> all;
	return all;
}

std::unordered_map<uint64_t, BoundMethod *> &by_number() {
	static std::unordered_map<uint64_t, BoundMethod *> all;
	return all;
}

void BoundMethod::let_go() {
	// Releasing the object may free what holds this callable, and so this callable: nothing of it is
	// touched afterwards.
	vm::Object *object = object_;
	object_ = nullptr;
	if (object && !weak_ && Project::generation() == program_) {
		Project::get().interpreter().release_object(object);
	}
}

void BoundMethod::listens_to(const Signal &p_signal) {
	if (std::find(signals_.begin(), signals_.end(), p_signal) != signals_.end()) {
		return;
	}
	signals_.push_back(p_signal);
}

bool BoundMethod::in(const Signal &p_signal) const {
	if (!ObjectDB::get_instance(p_signal.get_object_id())) {
		return false;
	}
	Array connections = p_signal.get_connections();
	for (int64_t i = 0; i < connections.size(); i++) {
		Callable callable = Dictionary(connections[i])["callable"];
		if (callable.is_custom() && callable.get_custom() == this) {
			return true;
		}
	}
	return false;
}

BoundMethod::BoundMethod(vm::Object *p_object, uint32_t p_function, Object *p_owner) :
		object_(p_object), function_(p_function), program_(Project::generation()), key_object_(p_object), key_function_(p_function), key_program_(program_) {
	static uint64_t numbers = 0;
	number_ = ++numbers;
	if (p_owner) {
		owner_ = ObjectID(p_owner->get_instance_id());
	}
	// The same method bound before a reload is the same listener: it is found by what it is now.
	for (const BoundMethod *other : bound_methods()) {
		if (other->object_ == object_ && other->function_ == function_ && other->program_ == program_) {
			key_object_ = other->key_object_;
			key_function_ = other->key_function_;
			key_program_ = other->key_program_;
			break;
		}
	}
	weak_ = p_owner && counted_on(p_object->klass, p_owner);
	if (!weak_) {
		vm::retain(object_);
	}
	bound_methods().insert(this);
	by_number()[number_] = this;
}

BoundMethod::~BoundMethod() {
	bound_methods().erase(this);
	by_number().erase(number_);
	let_go();
}

bool BoundMethod::equal(const CallableCustom *p_a, const CallableCustom *p_b) {
	const BoundMethod *a = static_cast<const BoundMethod *>(p_a);
	const BoundMethod *b = static_cast<const BoundMethod *>(p_b);
	return a->key_object_ == b->key_object_ && a->key_function_ == b->key_function_ && a->key_program_ == b->key_program_;
}

bool BoundMethod::less(const CallableCustom *p_a, const CallableCustom *p_b) {
	const BoundMethod *a = static_cast<const BoundMethod *>(p_a);
	const BoundMethod *b = static_cast<const BoundMethod *>(p_b);
	return a->key_object_ != b->key_object_ ? a->key_object_ < b->key_object_ : a->key_function_ < b->key_function_;
}

uint32_t BoundMethod::hash() const {
	uint64_t mixed = (uint64_t)(uintptr_t)key_object_ * 0x9E3779B97F4A7C15ull + key_function_;
	return (uint32_t)(mixed ^ (mixed >> 32));
}

String BoundMethod::get_as_text() const {
	if (!object_ || Project::generation() != program_) {
		return String("<unloaded ActionScript method>");
	}
	return String::utf8(Project::get().module().functions[function_].name.c_str());
}

void BoundMethod::call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, GDExtensionCallError &r_call_error) const {
	if (!on_main_thread()) {
		refuse_thread(function_);
		r_call_error.error = GDEXTENSION_CALL_OK;
		return;
	}
	if (!alive()) {
		// The tombstone: the object this method belongs to is gone.
		r_call_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return;
	}
	// The call may drop the last other reference to this callable (a listener that removes itself).
	vm::Object *object = object_;
	const uint64_t program = program_;
	vm::retain(object);
	const uint32_t function = function_;
	call_script(object, function, p_arguments, p_argcount, &r_return_value, &r_call_error);
	if (Project::generation() != program) {
		return;
	}
	if (r_call_error.error != GDEXTENSION_CALL_OK) {
		// The listener was not called: what it was given does not fit its parameters. Godot's own
		// report can't name the method of a callable like this one, so the report is made here, in
		// AS3's words.
		const ir::Function &callee = Project::get().module().functions[function];
		std::string text;
		if (r_call_error.error == GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT && r_call_error.argument >= 0 && r_call_error.argument < p_argcount) {
			CharString given = Variant::get_type_name(p_arguments[r_call_error.argument]->get_type()).utf8();
			const size_t at = (size_t)r_call_error.argument + (callee.is_static ? 0 : 1);
			ir::Kind kind = callee.registers[at];
			const std::string &type = at < callee.engine_types.size() ? callee.engine_types[at] : std::string();
			text = "TypeError: Error #1034: Type Coercion failed: cannot convert argument " + std::to_string(r_call_error.argument + 1) +
					" of " + callee.name + "(), a " + given.get_data() + ", to " + (type.empty() ? ir::kind_name(kind) : type.c_str()) + ".";
		} else {
			const size_t first = callee.is_static ? 0 : 1;
			size_t most = callee.parameters > first ? callee.parameters - first : 0;
			size_t least = callee.required > first ? callee.required - first : 0;
			text = "ArgumentError: Error #1063: Argument count mismatch on " + callee.name + "(). Expected " +
					std::to_string((size_t)p_argcount > most ? most : least) + ", got " + std::to_string(p_argcount) + ".";
		}
		// The listener was never entered, so there is no stack: say where it is declared.
		const ir::Module &module = Project::get().module();
		// Reported in AS3's words, and Godot is told the call failed, so that whatever made it (a
		// signal, callv, filter) knows.
		if (callee.file < module.files.size() && callee.file < module.lines.size()) {
			const std::vector<uint32_t> &starts = module.lines[callee.file];
			size_t line = 0;
			while (line < starts.size() && starts[line] <= callee.position) {
				line++;
			}
			text += "\n\tat " + callee.name + "()[" + module.files[callee.file] + ":" + std::to_string(line) + "]";
			Project::get().host().error_at(text, callee.name + "()", module.files[callee.file], (int32_t)line);
		} else {
			Project::get().host().error(text);
		}
	}
	Project::get().interpreter().release_object(object);
}

void listener_added(const Callable &p_callable, const Signal &p_signal) {
	if (BoundMethod *bound = p_callable.is_custom() ? dynamic_cast<BoundMethod *>(p_callable.get_custom()) : nullptr) {
		bound->listens_to(p_signal);
	}
}

bool listeners_can_move(vm::Reload &p_reload) {
	const uint64_t current = Project::generation();
	for (const BoundMethod *bound : bound_methods()) {
		if (bound->object_ && bound->program_ == current) {
			vm::Object *moved = p_reload.object(bound->object_);
			if (!moved) {
				return false;
			}
			vm::release(moved); // The reload keeps its own until it is done.
		}
	}
	return true;
}

void move_listeners(vm::Interpreter &p_old, vm::Reload &p_reload, uint64_t p_generation) {
	const uint64_t current = Project::generation();
	const std::vector<BoundMethod *> all(bound_methods().begin(), bound_methods().end());
	for (BoundMethod *bound : all) {
		if (!bound_methods().count(bound) || !bound->object_ || bound->program_ != current) {
			continue;
		}
		vm::Object *old = bound->object_;
		const uint32_t function = p_reload.function(bound->function_);
		// A method that is gone from the new scripts: the listener stays where it is and refuses to run.
		bound->object_ = function == ir::NONE ? nullptr : p_reload.object(old);
		bound->function_ = function == ir::NONE ? bound->function_ : function;
		bound->program_ = p_generation;
		if (bound->weak_) {
			// It holds neither the old object nor the new one.
			if (bound->object_ && vm::release(bound->object_)) {
				// Its instance keeps it.
			}
			continue;
		}
		p_old.release_object(old); // May destroy other bound methods: hence the check above.
	}
}

void release_listeners() {
	std::vector<BoundMethod *> all(bound_methods().begin(), bound_methods().end());
	for (BoundMethod *bound : all) {
		// Letting go of one object may destroy other bound methods.
		if (bound_methods().count(bound)) {
			bound->let_go();
		}
	}
}

void forget_listeners(const vm::Object *p_object) {
	for (BoundMethod *bound : bound_methods()) {
		if (bound->weak_ && bound->object_ == p_object) {
			bound->object_ = nullptr;
		}
	}
}

bool is_script_listener(const Callable &p_callable) {
	return p_callable.is_custom() && dynamic_cast<BoundMethod *>(p_callable.get_custom()) != nullptr;
}

Variant *make_callable(vm::Object *p_object, uint32_t p_function, Object *p_owner) {
	return new Variant(Callable(memnew(BoundMethod(p_object, p_function, p_owner))));
}

} // namespace kas3
