// A signal's emit(), worked out once (GodotHost::Worked, What::Emit) and run with nothing looked up.
//
// Godot has emit() as a method of the signal that takes any number of values, which is reached by its
// name on the signal held as a Variant: the name is looked up at every call, and so is the signal's object.
// What it does then is Object's emit_signal, and that has a method bind: the emit is worked out as that
// bind, called on the signal's object with the signal's name first. The name and the object's id are asked
// of the signal once, and again only when the place is handed another signal (SignalSeen).
#include "godot_calls.h"
#include "worked.h"

#include "godot_names.h"

#include <cstring>

using namespace godot;

namespace kas3 {

std::unique_ptr<GodotHost::Worked> emit_of(const ir::EngineName &p_entry, const std::vector<std::string_view> &p_words) {
	if (p_entry.key != "Signal.emit" || p_words.size() < 6 || p_words[1] != "method") {
		return nullptr;
	}
	// Object's own method, by the hash its entry in the name table carries.
	const std::optional<std::string_view> owner = godot_value("GodotObject");
	const std::optional<std::string_view> method = godot_value("GodotObject.emitSignal");
	const std::vector<std::string_view> words = method ? godot_words(*method) : std::vector<std::string_view>();
	if (!owner || words.size() < 3 || words[1] != "method") {
		return nullptr;
	}
	auto worked = std::make_unique<GodotHost::Worked>();
	worked->what = GodotHost::Worked::What::Emit;
	worked->name = std::string(p_words[0]);
	worked->bound = method_bind(*owner, words[0], words[2]);
	return worked->bound ? std::move(worked) : nullptr;
}

bool emit_signal(const GodotHost::Worked &p_worked, const Variant *p_signal, const Variant **p_pointers, size_t p_count) {
	if (!p_signal || p_signal->get_type() != Variant::SIGNAL) {
		return false;
	}
	const void *held = inside(*const_cast<Variant *>(p_signal), Variant::SIGNAL);
	SignalSeen &seen = p_worked.seen;
	if (!seen.any || std::memcmp(held, seen.bytes, sizeof(seen.bytes)) != 0) {
		const Signal &signal = *static_cast<const Signal *>(held);
		seen.name = signal.get_name();
		seen.object = static_cast<uint64_t>(signal.get_object_id());
		std::memcpy(seen.bytes, held, sizeof(seen.bytes));
		seen.any = true;
	}
	// By its id, each time: an object Godot has freed since is not there to emit anything.
	const GDExtensionObjectPtr object = seen.object ? gdextension_interface::object_get_instance_from_id(seen.object) : nullptr;
	if (!object) {
		return false;
	}
	// Godot copies the name before it runs a listener, which may emit another signal through this place.
	p_pointers[0] = &seen.name;
	alignas(Variant) unsigned char result[sizeof(Variant)];
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	gdextension_interface::object_method_bind_call(p_worked.bound, object, reinterpret_cast<GDExtensionConstVariantPtr *>(p_pointers),
			static_cast<GDExtensionInt>(p_count + 1), result, &error);
	// What emit_signal gives (whether anything listens, as a number) is nothing a signal's emit() gives,
	// and nothing to give back to Godot. Nothing of the handle is read from here on: a listener may have
	// let go of it (a field the signal was lent from, given another).
	return true;
}

} // namespace kas3
