#include "reload.h"

#include "any.h"

#include <algorithm>
#include <limits>

namespace kas3 {
namespace vm {

using ir::Kind;

Reload::Reload(Interpreter &p_from, Interpreter &p_to) :
		from_(p_from), to_(p_to) {
	const ir::Module &fresh = to_.module_;
	for (size_t i = 0; i < fresh.classes.size(); i++) {
		classes_.emplace(fresh.classes[i].name, (uint32_t)i);
	}
	for (size_t i = 0; i < fresh.functions.size(); i++) {
		functions_.emplace(fresh.functions[i].name, (uint32_t)i); // The first of a name: a getter before its setter.
	}
	// The new program does not start over: what ran at the start of the old one has run.
	to_.started_ = from_.started_;
}

Reload::~Reload() {
	for (auto &[old, fresh] : moved_) {
		to_.release_object(fresh);
	}
}

// A value of the old program as the new one holds it, with a reference of its own.
Slot Reload::carry(Kind p_kind, Slot p_value) {
	Slot out = p_value;
	switch (p_kind) {
		case Kind::Str:
			retain(static_cast<Str *>(p_value.p));
			break;
		case Kind::Obj:
			out.p = object(static_cast<Object *>(p_value.p));
			break;
		case Kind::Val:
		case Kind::Ref:
			// The engine's value is the host's, and the host is the same: each program holds its own.
			out.p = p_value.p ? to_.host_.handles.copy(p_value.p) : nullptr;
			break;
		case Kind::Any:
			if (any::tag(p_value) == AnyTag::Object) {
				Object *moved = object(static_cast<Object *>(any::pointer(p_value)));
				out = moved ? any::object(moved) : any::null();
			} else {
				out = to_.share(Kind::Any, p_value); // A string, a box or a handle: the same thing, held again.
			}
			break;
		default:
			break;
	}
	return out;
}

Slot Reload::carry_into(Kind p_kind, uint32_t p_class, Slot p_value) {
	Slot out = carry(p_kind, p_value);
	if (p_kind == Kind::Any) {
		return out; // Any value fits.
	}
	if (p_kind == Kind::Obj && out.p && p_class != ir::NONE && !to_.is_class(static_cast<Object *>(out.p), p_class)) {
		to_.release_object(static_cast<Object *>(out.p));
		out.bits = 0;
	}
	return out;
}

Object *Reload::object(Object *p_old) {
	if (!p_old) {
		return nullptr;
	}
	if (auto found = moved_.find(p_old); found != moved_.end()) {
		retain(found->second);
		return found->second;
	}
	const ir::Class &old_class = from_.module_.classes[p_old->klass];
	const auto named = classes_.find(old_class.name);
	if (named == classes_.end()) {
		if (missing_.empty()) {
			missing_ = old_class.name;
		}
		return nullptr;
	}
	const ir::Class &new_class = to_.module_.classes[named->second];
	if (old_class.element != Kind::Void || new_class.element != Kind::Void) {
		// A vector: the same length, and each element carried as a field is. Its type must still hold
		// elements of the same kind.
		const Vector *old = static_cast<const Vector *>(p_old);
		Vector *vector = old_class.element == new_class.element ? new_vector(named->second, old->length, old->fixed) : nullptr;
		if (!vector) {
			if (missing_.empty()) {
				missing_ = old_class.name;
			}
			return nullptr;
		}
		moved_.emplace(p_old, vector);
		retain(vector);
		for (uint32_t i = 0; i < old->length; i++) {
			vector->data[i] = carry(new_class.element, old->data[i]);
		}
		return vector;
	}
	Object *fresh = new_object(named->second, new_class.fields.size());
	if (!fresh) {
		return nullptr;
	}
	// Before its fields: an object it refers to may refer back.
	moved_.emplace(p_old, fresh);
	retain(fresh);
	// A base and its subclass may each have a private field of one name: the n-th of a name is the n-th.
	std::unordered_map<std::string_view, size_t> seen;
	for (size_t i = 0; i < new_class.fields.size(); i++) {
		const ir::Field &field = new_class.fields[i];
		size_t skip = seen[field.name]++;
		bool carried = false;
		for (size_t j = 0; j < old_class.fields.size() && !carried; j++) {
			if (old_class.fields[j].name != field.name || skip-- > 0) {
				continue;
			}
			if (old_class.fields[j].kind == field.kind) {
				fresh->fields()[i] = carry_into(field.kind, field.klass, p_old->fields()[j]);
			}
			carried = true;
		}
		if (!carried) {
			fresh->fields()[i] = empty_value(field.kind == Kind::F64, field.starts_null);
		}
	}
	return fresh;
}

void Reload::globals() {
	const ir::Module &old = from_.module_;
	const ir::Module &fresh = to_.module_;
	std::unordered_map<std::string_view, size_t> held;
	for (size_t i = 0; i < old.globals.size(); i++) {
		held.emplace(old.globals[i].name, i);
	}
	for (size_t i = 0; i < fresh.globals.size() && i < to_.globals_.size(); i++) {
		const auto found = held.find(fresh.globals[i].name);
		if (found != held.end() && old.globals[found->second].kind == fresh.globals[i].kind) {
			to_.drop(fresh.globals[i].kind, to_.globals_[i]);
			to_.globals_[i] = carry_into(fresh.globals[i].kind, fresh.globals[i].klass, from_.globals_[found->second]);
		}
	}
	for (size_t i = 0; i < old.classes.size() && i < from_.class_used_.size(); i++) {
		const auto named = classes_.find(old.classes[i].name);
		if (named != classes_.end() && named->second < to_.class_used_.size()) {
			to_.class_used_[named->second] = from_.class_used_[i];
		}
	}
}

bool Reload::alive(const std::vector<std::string> &p_classes) const {
	for (const auto &[old, fresh] : moved_) {
		const std::string &name = to_.module_.classes[fresh->klass].name;
		if (std::find(p_classes.begin(), p_classes.end(), name) != p_classes.end()) {
			return true;
		}
	}
	return false;
}

uint32_t Reload::function(uint32_t p_old) const {
	if (p_old >= from_.module_.functions.size()) {
		return ir::NONE;
	}
	const auto found = functions_.find(from_.module_.functions[p_old].name);
	return found == functions_.end() ? ir::NONE : found->second;
}

} // namespace vm
} // namespace kas3
