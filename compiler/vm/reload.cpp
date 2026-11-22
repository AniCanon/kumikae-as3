#include "reload.h"

#include "any.h"
#include "held.h"
#include "table.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

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
		if (!functions_.emplace(fresh.functions[i].name, static_cast<uint32_t>(i)).second) {
			ambiguous_functions_.insert(fresh.functions[i].name);
		}
	}
	std::unordered_set<std::string_view> prior_names;
	for (const auto &prior : from_.module_.functions) {
		if (!prior_names.insert(prior.name).second) {
			ambiguous_functions_.insert(prior.name);
		}
	}
	// The new program does not start over: what ran at the start of the old one has run.
	to_.started_ = from_.started_;
	if (from_.constructions_) {
		to_.remember_constructions(); // Before anything is made there.
	}
	if (from_.constructions_ && to_.constructions_) {
		for (const std::string &name : from_.constructions_->natively_made()) {
			to_.constructions_->made_natively(name);
		}
		for (const std::string &name : from_.constructions_->once_natively_made()) {
			to_.constructions_->made_once_natively(name);
		}
	}
}

Reload::~Reload() {
	for (auto &[old, fresh] : moved_) {
		// What update_in_place() did not settle: its first values go with the old program.
		Constructions::Record *record = to_.constructions_ ? to_.constructions_->record(fresh) : nullptr;
		if (record && record->kept == Constructions::CARRIED) {
			to_.constructions_->forget(fresh);
		}
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

Slot Reload::carry_into(Kind p_kind, uint32_t p_class, std::string_view p_engine_type, Slot p_value) {
	Slot out = carry(p_kind, p_value);
	if (p_kind == Kind::Any) {
		return out; // Any value fits.
	}
	if (p_kind == Kind::Obj && out.p && p_class != ir::NONE && !to_.is_class(static_cast<Object *>(out.p), p_class)) {
		to_.release_object(static_cast<Object *>(out.p));
		out.bits = 0;
	}
	// One of Godot's the new field's type does not take: the calls prepared for that type would run on
	// an object of another.
	if (ir::is_engine(p_kind) && out.p && !p_engine_type.empty() && !to_.host_.handles.is_class(out.p, p_engine_type)) {
		to_.drop(p_kind, out);
		out.bits = 0;
	}
	return out;
}

// Each property, in order. A key that is an object whose class is gone is left out, with its value, and
// missing() names the class: a host refuses such a reload.
void Reload::carry_entries(const Entries &p_from, Entries *p_to) {
	for (const Entry &entry : p_from.all()) {
		if (any::tag(entry.key) == AnyTag::Undefined) {
			continue;
		}
		const Slot key = carry(Kind::Any, entry.key);
		if (any::tag(key) == AnyTag::Null) {
			continue;
		}
		const bool text = any::tag(key) == AnyTag::String;
		const bool present = text ? p_to->find_text(static_cast<Str *>(any::pointer(key))->view()).has_value()
								  : p_to->find_object(any::pointer(key)).has_value();
		if (present) {
			to_.drop_any(key); // Two keys that became one object: the first is kept.
			continue;
		}
		p_to->add(key, carry(Kind::Any, entry.value), entry.hidden);
	}
}

Object *Reload::lost(uint32_t p_old_class) {
	if (missing_.empty()) {
		missing_ = from_.module_.classes[p_old_class].name;
	}
	missing_classes_.resize(from_.module_.classes.size(), 0);
	missing_classes_[p_old_class] = 1;
	return nullptr;
}

// A Function holds a module-local index. Rebind by name and check that an instance method still has
// its receiver, or that a closure still has every environment its new body expects.
std::optional<uint32_t> Reload::callback_target(Object *p_old) const {
	const uint32_t old = static_cast<uint32_t>(own_field(p_old, ir::FUNCTION_INDEX).i);
	uint32_t target = function(old);
	if (target == ir::NONE) {
		return std::nullopt;
	}
	const ir::Function &before = from_.module_.functions[old];
	if (ambiguous_functions_.count(before.name)) {
		// Sibling anonymous expressions share their printed name. Preserve a unique declaration at
		// the same source position, or an unchanged body after another declaration moved it.
		target = matching_closure(before);
		if (target == ir::NONE) {
			return std::nullopt;
		}
	}
	const ir::Function &after = to_.module_.functions[target];
	if (before.is_static != after.is_static || (before.owner == ir::NONE) != (after.owner == ir::NONE)) {
		return std::nullopt;
	}
	if (!after.is_static && after.owner == ir::NONE) {
		Object *environment = static_cast<Object *>(own_field(p_old, ir::FUNCTION_FIRST).p);
		uint32_t expected = after.parameter_classes.empty() ? ir::NONE : after.parameter_classes[0];
		while (expected != ir::NONE) {
			const ir::Class &layout = to_.module_.classes[expected];
			if (!environment || from_.module_.classes[environment->klass].name != layout.name || layout.fields.empty()) {
				return std::nullopt;
			}
			const ir::Class &old_layout = from_.module_.classes[environment->klass];
			if (classes_.at(old_layout.name) != expected || old_layout.fields.size() != layout.fields.size()) {
				return std::nullopt;
			}
			for (size_t i = 0; i < layout.fields.size(); ++i) {
				if (layout.fields[i].name != old_layout.fields[i].name || layout.fields[i].kind != old_layout.fields[i].kind) {
					return std::nullopt;
				}
			}
			environment = static_cast<Object *>(own_field(environment, 0).p);
			expected = layout.fields[0].klass;
		}
	}
	return target;
}

uint32_t Reload::matching_closure(const ir::Function &p_before) const {
	uint32_t by_position = ir::NONE;
	uint32_t by_body = ir::NONE;
	size_t positions = 0, bodies = 0;
	const std::string body = ir::print(from_.module_, p_before);
	for (uint32_t i = 0; i < to_.module_.functions.size(); ++i) {
		const ir::Function &candidate = to_.module_.functions[i];
		if (candidate.name != p_before.name || to_.module_.files[candidate.file] != from_.module_.files[p_before.file]) {
			continue;
		}
		if (candidate.position == p_before.position) {
			by_position = i;
			++positions;
		}
		if (ir::print(to_.module_, candidate) == body) {
			by_body = i;
			++bodies;
		}
	}
	if (bodies == 1) {
		return by_body;
	}
	return positions == 1 ? by_position : ir::NONE;
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
	if (p_old->klass == from_.module_.class_class) {
		// A class value: the class of the same name in the new program, whose one object for it this is.
		const uint32_t code = static_cast<uint32_t>(own_field(p_old, ir::CLASS_CODE).i);
		const auto same = code < from_.module_.classes.size() ? classes_.find(from_.module_.classes[code].name) : classes_.end();
		if (code < from_.module_.classes.size() && same == classes_.end()) {
			return lost(code);
		}
		Object *fresh = to_.module_.class_class == ir::NONE ? nullptr : to_.class_value(same == classes_.end() ? code : same->second);
		if (!fresh) {
			return lost(p_old->klass);
		}
		retain(fresh);
		return fresh;
	}
	const auto named = classes_.find(old_class.name);
	if (named == classes_.end()) {
		return lost(p_old->klass);
	}
	const ir::Class &new_class = to_.module_.classes[named->second];
	if (old_class.table || new_class.table) {
		// An Object or a Dictionary: each key and value carried as an untyped field is.
		Object *table = old_class.table == new_class.table ? new_table(named->second, to_.heap_.list_for(named->second)) : nullptr;
		if (!table) {
			return lost(p_old->klass);
		}
		moved_.emplace(p_old, table);
		retain(table);
		carry_entries(*entries_of(p_old), entries_of(table));
		return table;
	}
	if (old_class.element != Kind::Void || new_class.element != Kind::Void) {
		return moved_vector(static_cast<Vector *>(p_old), named->second);
	}
	std::optional<uint32_t> callback;
	if (p_old->klass == from_.module_.function_class) {
		callback = callback_target(p_old);
		if (!callback) {
			return lost(p_old->klass);
		}
	}
	Object *fresh = new_object(named->second, to_.heap_.object_slots(named->second), to_.heap_.list_for(named->second));
	if (!fresh) {
		return nullptr;
	}
	// Before its fields: an object it refers to may refer back.
	moved_.emplace(p_old, fresh);
	retain(fresh);
	carry_fields(p_old, fresh);
	if (callback) {
		own_field(fresh, ir::FUNCTION_INDEX).i = static_cast<int32_t>(*callback);
	}
	carry_record(p_old, fresh);
	return fresh;
}

// A vector: the same length, and each element carried as a field is. Its type must still hold elements of
// the same kind.
Object *Reload::moved_vector(Vector *p_old, uint32_t p_class) {
	const ir::Class &old_class = from_.module_.classes[p_old->klass];
	const ir::Class &new_class = to_.module_.classes[p_class];
	const bool alike = old_class.element == new_class.element && old_class.element_bare == new_class.element_bare;
	Vector *vector = alike ? new_vector_of(new_class, p_class, p_old->length, p_old->fixed, to_.heap_.list_for(p_class)) : nullptr;
	if (!vector) {
		return lost(p_old->klass);
	}
	moved_.emplace(p_old, vector);
	retain(vector);
	for (uint32_t i = 0; i < p_old->length; i++) {
		set_place(vector, i, carry(place_kind(new_class), place(p_old, i)));
	}
	Entries *properties = p_old->properties && p_old->properties->size() ? properties_of(vector) : nullptr;
	if (properties) {
		carry_entries(*p_old->properties, properties);
	}
	return vector;
}

std::vector<uint32_t> Reload::fields_by_name(const ir::Class &p_old, const ir::Class &p_new) {
	std::vector<uint32_t> found(p_new.fields.size(), ir::NONE);
	std::unordered_map<std::string_view, size_t> seen;
	for (size_t i = 0; i < p_new.fields.size(); i++) {
		size_t skip = seen[p_new.fields[i].name]++;
		for (size_t j = 0; j < p_old.fields.size() && found[i] == ir::NONE; j++) {
			if (p_old.fields[j].name == p_new.fields[i].name && skip-- == 0) {
				found[i] = static_cast<uint32_t>(j);
			}
		}
	}
	return found;
}

// The new object's fields, each carried from the old one's field of its name and kind, or empty.
void Reload::carry_fields(Object *p_old, Object *p_fresh) {
	const ir::Class &old_class = from_.module_.classes[p_old->klass];
	const ir::Class &new_class = to_.module_.classes[p_fresh->klass];
	const std::vector<uint32_t> named = fields_by_name(old_class, new_class);
	for (size_t i = 0; i < new_class.fields.size(); i++) {
		const ir::Field &field = new_class.fields[i];
		if (named[i] == ir::NONE) {
			vm::field(new_class, p_fresh, i) = empty_value(ir::is_floating(field.kind), field.starts_null);
		} else if (old_class.fields[named[i]].kind == field.kind) {
			vm::field(new_class, p_fresh, i) = carry_into(field.kind, field.klass, field.engine_type, vm::field(old_class, p_old, named[i]));
		}
	}
}

Slot Reload::global_of(const Interpreter &p_program, size_t p_index) {
	return p_program.native_ ? *p_program.native_->global(static_cast<uint32_t>(p_index)) : p_program.globals_[p_index];
}

bool Reload::used_in(const Interpreter &p_program, size_t p_class) {
	return p_program.class_used_[p_class] || (p_program.native_ && p_program.native_->statics_given(static_cast<uint32_t>(p_class)));
}

void Reload::globals(const std::vector<std::string> &p_changed) {
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
			to_.globals_[i] = carry_into(fresh.globals[i].kind, fresh.globals[i].klass, fresh.globals[i].engine_type, global_of(from_, found->second));
		}
	}
	for (size_t i = 0; i < old.classes.size() && i < from_.class_used_.size(); i++) {
		const auto named = classes_.find(old.classes[i].name);
		if (named != classes_.end() && named->second < to_.class_used_.size()) {
			to_.class_used_[named->second] = used_in(from_, i);
		}
	}
	changed_statics_ = p_changed;
	for (const std::string &name : p_changed) {
		constants_again(name);
	}
}

// A class whose static fields' first values changed, and which had been given them. A constant is never
// written by the program, so it takes what the new code gives it. That code is one function for the whole
// class and runs again whole: the class's variables are set aside meanwhile and put back, so they keep what
// the program made of them.
void Reload::constants_again(const std::string &p_class) {
	const auto found = classes_.find(p_class);
	if (found == classes_.end() || found->second >= to_.class_used_.size() || !to_.class_used_[found->second]) {
		return;
	}
	const ir::Class &klass = to_.module_.classes[found->second];
	const std::vector<ir::Global> &globals = to_.module_.globals;
	if (klass.static_init == ir::NONE) {
		return;
	}
	const auto own = [&](const ir::Global &p_global) {
		const std::string_view name = p_global.name;
		return name.size() > klass.name.size() + 1 && name.compare(0, klass.name.size(), klass.name) == 0 && name[klass.name.size()] == '.' &&
				name.find('.', klass.name.size() + 1) == std::string_view::npos;
	};
	std::vector<std::pair<size_t, Slot>> kept;
	for (size_t i = 0; i < globals.size() && i < to_.globals_.size(); i++) {
		if (own(globals[i]) && !globals[i].is_const) {
			kept.emplace_back(i, to_.globals_[i]);
			to_.globals_[i] = empty_value(ir::is_floating(globals[i].kind), globals[i].starts_null);
		}
	}
	(void)to_.call(klass.static_init, nullptr, 0); // An error in it is reported as on the class's first use.
	for (const auto &[index, value] : kept) {
		to_.drop(globals[index].kind, to_.globals_[index]);
		to_.globals_[index] = value;
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

bool Reload::statics_given(const std::string &p_class) const {
	const auto found = classes_.find(p_class);
	return found != classes_.end() && found->second < to_.class_used_.size() && to_.class_used_[found->second];
}

std::vector<uint8_t> Reload::reached(const Interpreter &p_program, const std::vector<Object *> &p_roots) {
	const ir::Module &module = p_program.module_;
	std::vector<uint8_t> classes(module.classes.size(), 0);
	std::unordered_set<Object *> seen;
	std::vector<Object *> pending;
	const auto visit = [&](Kind p_kind, Slot p_value) {
		Object *object = p_kind == Kind::Obj ? static_cast<Object *>(p_value.p)
				: p_kind == Kind::Any && any::tag(p_value) == AnyTag::Object ? static_cast<Object *>(any::pointer(p_value))
																			 : nullptr;
		if (object && seen.insert(object).second) {
			pending.push_back(object);
		}
	};
	for (size_t i = 0; i < module.globals.size() && i < p_program.globals_.size(); i++) {
		visit(module.globals[i].kind, global_of(p_program, i));
	}
	for (Object *root : p_roots) {
		if (root && seen.insert(root).second) {
			pending.push_back(root);
		}
	}
	while (!pending.empty()) {
		Object *object = pending.back();
		pending.pop_back();
		classes[object->klass] = 1;
		each_held(module.classes[object->klass], object, visit);
	}
	return classes;
}

bool Reload::keep_containers(const ir::Module &p_old, const std::vector<uint8_t> &p_live, ir::Module *r_new) {
	std::unordered_set<std::string> named;
	for (const ir::Class &klass : r_new->classes) {
		named.insert(klass.name);
	}
	bool added = false;
	for (size_t i = 0; i < p_old.classes.size() && i < p_live.size(); i++) {
		const ir::Class &klass = p_old.classes[i];
		// A container of the library: a name and what its elements are, with no code of its own to move.
		const bool container = (klass.element != Kind::Void || klass.table) && klass.fields.empty() && klass.methods.empty() &&
				klass.base == ir::NONE && klass.constructor == ir::NONE;
		if (!p_live[i] || !container || named.count(klass.name)) {
			continue;
		}
		const uint32_t index = static_cast<uint32_t>(r_new->classes.size());
		r_new->classes.push_back(klass);
		added = true;
		uint32_t *kept = klass.array ? &r_new->array_class : klass.dictionary ? &r_new->dictionary_class
				: klass.table													 ? &r_new->object_class
																				 : nullptr;
		if (kept && *kept == ir::NONE) {
			*kept = index;
		}
	}
	return added;
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
