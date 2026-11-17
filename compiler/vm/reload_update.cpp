// Objects alive updated in place by a reload (reload.h, update_in_place).
#include "reload.h"

#include "fields.h"

#include <algorithm>
#include <map>

namespace kas3 {
namespace vm {

using ir::Kind;
using Record = Constructions::Record;

void Reload::carry_record(Object *p_old, Object *p_fresh) {
	const Record *kept = from_.constructions_ && to_.constructions_ ? from_.constructions_->of(p_old) : nullptr;
	if (!kept) {
		return;
	}
	const ir::Class &old_class = from_.module_.classes[p_old->klass];
	const ir::Class &new_class = to_.module_.classes[p_fresh->klass];
	// The arguments are given again only to a constructor that takes them as the one they were given to did.
	const ir::Function *made = new_class.constructor < to_.module_.functions.size() ? &to_.module_.functions[new_class.constructor] : nullptr;
	bool fits = made && !made->rest && !made->takes_extra && kept->arguments < made->parameters;
	for (uint32_t i = 0; fits && i < kept->arguments; i++) {
		fits = made->registers[1 + i] == from_.constructions_->kind_of(p_old->klass, *kept, i);
	}
	if (!fits) {
		unfit_.insert(p_fresh->klass);
		return;
	}
	Record *record = to_.constructions_->add(p_fresh, new_class.constructor, p_fresh->klass, kept->arguments);
	if (!record) {
		return;
	}
	for (uint32_t i = 0; i < kept->arguments; i++) {
		record->values[i] = Constructions::held(made->registers[1 + i], kept->values[i]);
	}
	if (kept->kept != Constructions::ALL) {
		record->kept = Constructions::CARRIED;
		carried_[p_fresh] = p_old->klass;
		return;
	}
	const std::vector<uint32_t> named = fields_by_name(old_class, new_class);
	for (uint32_t i = 0; i < new_class.fields.size(); i++) {
		const Kind kind = new_class.fields[i].kind;
		// A field that is new, or of another kind now, holds nothing the program gave it: what it holds is
		// its first value, so that it takes the one the new code gives.
		const bool carried = named[i] != ir::NONE && old_class.fields[named[i]].kind == kind;
		const Slot first = carried ? kept->values[kept->arguments + named[i]] : field(new_class, p_fresh, i);
		record->values[kept->arguments + i] = Constructions::held(kind, first);
	}
	record->kept = Constructions::ALL;
}

Object *Reload::made_again(Interpreter &p_program, uint32_t p_class, const Record &p_record) {
	const ir::Class &klass = p_program.module_.classes[p_class];
	Object *again = new_object(p_class, p_program.heap_.object_slots(p_class), p_program.heap_.list_for(p_class));
	if (!again) {
		return nullptr;
	}
	reset_fields(klass, again);
	std::vector<Slot> arguments(p_record.arguments + 1);
	arguments[0].bits = 0;
	arguments[0].p = again;
	std::copy(p_record.values, p_record.values + p_record.arguments, arguments.begin() + 1);
	if (!p_program.call(klass.constructor, arguments.data(), arguments.size())) {
		p_program.release_object(again);
		return nullptr;
	}
	return again;
}

bool Reload::first_values(Object *p_fresh, std::vector<Slot> *r_values, Object **r_made) {
	const auto from = carried_.find(p_fresh);
	const Record *kept = to_.constructions_->record(p_fresh);
	*r_made = from != carried_.end() && kept ? made_again(from_, from->second, *kept) : nullptr;
	if (!*r_made) {
		return false;
	}
	const ir::Class &old_class = from_.module_.classes[from->second];
	const ir::Class &new_class = to_.module_.classes[p_fresh->klass];
	const std::vector<uint32_t> named = fields_by_name(old_class, new_class);
	r_values->resize(new_class.fields.size());
	for (uint32_t i = 0; i < new_class.fields.size(); i++) {
		// As carry_record() has it: a field that is new, or of another kind now, starts from what it holds.
		const bool carried = named[i] != ir::NONE && old_class.fields[named[i]].kind == new_class.fields[i].kind;
		(*r_values)[i] = carried ? field(old_class, *r_made, named[i]) : field(new_class, p_fresh, i);
	}
	return true;
}

void Reload::keep_first_values(Object *p_fresh) {
	Record *record = to_.constructions_->record(p_fresh);
	const ir::Class &klass = to_.module_.classes[p_fresh->klass];
	std::vector<Slot> first;
	Object *made = nullptr;
	bool plain = first_values(p_fresh, &first, &made);
	for (uint32_t i = 0; plain && i < klass.fields.size(); i++) {
		plain = Constructions::plain(klass.fields[i].kind, first[i]);
	}
	if (plain) {
		for (uint32_t i = 0; i < klass.fields.size(); i++) {
			record->values[record->arguments + i] = Constructions::held(klass.fields[i].kind, first[i]);
		}
		record->kept = Constructions::ALL;
	} else {
		to_.constructions_->forget(p_fresh);
	}
	if (made) {
		from_.release_object(made);
	}
}

void Reload::settle(const std::vector<Object *> &p_objects, bool p_same) {
	if (!to_.constructions_) {
		return;
	}
	for (Object *fresh : p_objects) {
		Record *record = to_.constructions_->record(fresh);
		if (!record || record->kept != Constructions::CARRIED) {
			continue;
		}
		if (p_same) {
			record->kept = Constructions::ARGUMENTS;
		} else {
			keep_first_values(fresh);
		}
	}
}

// Whether a constant has another value in the new program than it had in the old one. One that holds an
// object can't be compared: it changed if the code that gives its class's static fields their values did.
bool Reload::constant_changed(const std::string &p_name) const {
	const auto index_in = [&](const ir::Module &p_module) {
		const auto found = std::find_if(p_module.globals.begin(), p_module.globals.end(), [&](const ir::Global &p_global) { return p_global.name == p_name; });
		return static_cast<size_t>(found - p_module.globals.begin());
	};
	const size_t before = index_in(from_.module_);
	const size_t after = index_in(to_.module_);
	if (after >= to_.globals_.size()) {
		return false;
	}
	const Kind kind = to_.module_.globals[after].kind;
	if (before >= from_.globals_.size() || from_.module_.globals[before].kind != kind) {
		return true;
	}
	if (Constructions::plain(kind, from_.globals_[before]) && Constructions::plain(kind, to_.globals_[after])) {
		return !Constructions::same(kind, from_.globals_[before], to_.globals_[after]);
	}
	const std::string owner = p_name.substr(0, p_name.rfind('.'));
	return std::find(changed_statics_.begin(), changed_statics_.end(), owner) != changed_statics_.end();
}

// One object: the new constructor run on a throwaway with the arguments the object was made with, and
// each field that still holds its old first value given the new one.
Reload::Again Reload::construct_again(Object *p_object) {
	Constructions *kept = to_.constructions_.get();
	Record *record = kept ? kept->record(p_object) : nullptr;
	// An object moved here has all of its record, or one still carried: its arguments alone would say
	// its first values are the new chain's, which is what is about to be found out.
	if (!record || (record->kept != Constructions::ALL && record->kept != Constructions::CARRIED)) {
		return Again::NotRemembered;
	}
	const ir::Class &klass = to_.module_.classes[p_object->klass];
	std::vector<Slot> was(record->values + record->arguments, record->values + record->arguments + klass.fields.size());
	Object *before = nullptr;
	if (record->kept == Constructions::CARRIED && !first_values(p_object, &was, &before)) {
		kept->forget(p_object);
		return Again::NotRemembered;
	}
	// An error in it is reported as any error of a call from outside is.
	Object *again = made_again(to_, p_object->klass, *record);
	bool plain = again != nullptr;
	for (uint32_t i = 0; plain && i < klass.fields.size(); i++) {
		plain = Constructions::plain(klass.fields[i].kind, field(klass, again, i));
	}
	if (plain) {
		for (uint32_t i = 0; i < klass.fields.size(); i++) {
			const Kind kind = klass.fields[i].kind;
			const Slot now = field(klass, again, i);
			Slot &held = field(klass, p_object, i);
			if (!Constructions::same(kind, was[i], now) && Constructions::same(kind, held, was[i])) {
				to_.put(kind, held, Constructions::held(kind, now));
			}
		}
		// The new first values are the object's from here on: what the new chain gives for its arguments.
		kept->keep_arguments_only(p_object);
	} else if (record->kept == Constructions::CARRIED) {
		// The old first values are kept, for an edit that puts the constructor right.
		for (uint32_t i = 0; i < klass.fields.size(); i++) {
			record->values[record->arguments + i] = Constructions::held(klass.fields[i].kind, was[i]);
		}
		record->kept = Constructions::ALL;
	}
	if (again) {
		to_.release_object(again);
	}
	if (before) {
		from_.release_object(before);
	}
	return plain ? Again::Updated : Again::Failed;
}

InPlace Reload::update_in_place(const std::vector<std::string> &p_run_once) {
	// By class of the new program: its objects that are alive. A table, a vector and an object of the
	// library's are never made again.
	std::map<uint32_t, std::vector<Object *>> alive;
	for (const auto &[old, fresh] : moved_) {
		const ir::Class &klass = to_.module_.classes[fresh->klass];
		if (!klass.table && klass.element == Kind::Void && !klass.built_in) {
			alive[fresh->klass].push_back(fresh);
		}
	}
	InPlace done;
	for (const auto &[index, objects] : alive) {
		const std::string &name = to_.module_.classes[index].name;
		const ir::ConstructorChain chain = ir::constructor_chain(to_.module_, index);
		const bool edited = std::find(p_run_once.begin(), p_run_once.end(), name) != p_run_once.end();
		if (!edited && std::none_of(chain.constants.begin(), chain.constants.end(), [&](const std::string &p_name) { return constant_changed(p_name); })) {
			settle(objects, !chain.refusal);
			continue;
		}
		if (chain.refusal) {
			settle(objects, false);
			done.left.emplace_back(name, *chain.refusal);
			continue;
		}
		bool updated = false, unknown = false, failed = false;
		for (size_t i = 0; i < objects.size() && !failed; i++) {
			const Again result = construct_again(objects[i]);
			updated = updated || result == Again::Updated;
			unknown = unknown || result == Again::NotRemembered;
			failed = result == Again::Failed; // Once is enough: every object of the class would fail, and say so.
		}
		settle(objects, false); // Those after one that failed.
		if (updated) {
			done.updated.push_back(name);
		}
		if (failed) {
			done.left.emplace_back(name, "its constructor failed when it was run again with the new code");
		} else if (unknown) {
			done.left.emplace_back(name, !to_.constructions_ ? "what its objects were made with is not remembered"
					: unfit_.count(index)					 ? "its constructor's parameters changed"
															 : "an object of it was made with an argument or a first value that is not a number, a Boolean, text or null");
		}
	}
	std::sort(done.updated.begin(), done.updated.end());
	std::sort(done.left.begin(), done.left.end());
	return done;
}

} // namespace vm
} // namespace kas3
