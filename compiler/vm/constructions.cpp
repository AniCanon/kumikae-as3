#include "constructions.h"

#include "any.h"
#include "fields.h"

#include <cstdlib>
#include <cstring>

namespace kas3 {
namespace vm {

using ir::Kind;

namespace {

Str *text_in(Kind p_kind, Slot p_value) {
	if (p_kind == Kind::Str) {
		return static_cast<Str *>(p_value.p);
	}
	return p_kind == Kind::Any && any::tag(p_value) == AnyTag::String ? static_cast<Str *>(any::pointer(p_value)) : nullptr;
}

void give_up(Kind p_kind, Slot p_value) {
	Str *text = text_in(p_kind, p_value);
	if (release(text)) {
		free_string(text);
	}
}

} // namespace

bool Constructions::plain(Kind p_kind, Slot p_value) {
	switch (p_kind) {
		case Kind::Obj:
		case Kind::Val:
		case Kind::Ref:
			return p_value.p == nullptr;
		case Kind::Any: {
			const AnyTag tag = any::tag(p_value);
			return tag != AnyTag::Object && tag != AnyTag::Value && tag != AnyTag::Godot && tag != AnyTag::Wide;
		}
		default:
			return true;
	}
}

bool Constructions::same(Kind p_kind, Slot p_a, Slot p_b) {
	const Str *a = text_in(p_kind, p_a);
	const Str *b = text_in(p_kind, p_b);
	if (a && b) {
		return a->view() == b->view();
	}
	switch (p_kind) {
		// What the slot holds beyond the value's own bytes is not the value.
		case Kind::I32:
		case Kind::U32:
			return p_a.u == p_b.u;
		case Kind::Bool:
			return p_a.b == p_b.b;
		default:
			return p_a.bits == p_b.bits;
	}
}

Slot Constructions::held(Kind p_kind, Slot p_value) {
	retain(text_in(p_kind, p_value));
	return p_value;
}

Constructions::Constructions(const ir::Module &p_module) :
		module_(p_module), constructors_(p_module.functions.size(), 0), table_(64) {
	for (const ir::Class &klass : module_.classes) {
		const bool script = !klass.built_in && !klass.is_interface && !klass.table && klass.element == Kind::Void && klass.native_base.empty();
		// A constructor that takes any number of arguments is given them as an Array: never plain.
		if (script && klass.constructor < module_.functions.size() && !module_.functions[klass.constructor].rest &&
				!module_.functions[klass.constructor].takes_extra) {
			constructors_[klass.constructor] = 1;
		}
	}
}

Constructions::~Constructions() {
	for (Entry &entry : table_) {
		if (entry.record) {
			let_go(entry.record);
		}
	}
	for (std::vector<Record *> &records : spare_) {
		for (Record *record : records) {
			std::free(record);
		}
	}
}

Kind Constructions::kind_of(const Record &p_record, uint32_t p_value) const {
	if (p_value < p_record.arguments) {
		return module_.functions[p_record.function].registers[1 + p_value];
	}
	return module_.classes[p_record.klass].fields[p_value - p_record.arguments].kind;
}

uint32_t Constructions::room(const Record &p_record) const {
	return p_record.arguments + static_cast<uint32_t>(module_.classes[p_record.klass].fields.size());
}

size_t Constructions::home(const Object *p_object) const {
	// An object is at least 16-aligned: the low bits say nothing.
	return ((reinterpret_cast<uintptr_t>(p_object) >> 4) * 0x9E3779B97F4A7C15ull >> 20) & (table_.size() - 1);
}

size_t Constructions::place(const Object *p_object) const {
	const size_t mask = table_.size() - 1;
	size_t at = home(p_object);
	while (table_[at].object && table_[at].object != p_object) {
		at = (at + 1) & mask;
	}
	return at;
}

void Constructions::grow() {
	std::vector<Entry> old(table_.size() * 2);
	old.swap(table_);
	for (const Entry &entry : old) {
		if (entry.object) {
			table_[place(entry.object)] = entry;
		}
	}
}

void Constructions::let_go(Record *p_record) {
	const uint32_t kept = p_record->arguments + (p_record->fields == NOT_YET ? 0 : p_record->fields);
	for (uint32_t i = 0; i < kept; i++) {
		give_up(kind_of(*p_record, i), p_record->values[i]);
	}
	const uint32_t values = room(*p_record);
	if (spare_.size() <= values) {
		spare_.resize(values + 1);
	}
	spare_[values].push_back(p_record);
}

// The entry at `p_gap` has been emptied: the entries after it that were pushed past their own places move
// back, so that a search never stops short at the gap.
void Constructions::remove(size_t p_gap) {
	const size_t mask = table_.size() - 1;
	size_t gap = p_gap;
	for (size_t at = (gap + 1) & mask; table_[at].object; at = (at + 1) & mask) {
		const size_t wanted = home(table_[at].object);
		// One whose own place is after the gap and no later than where it is stays: a search for it starts there.
		const bool stays = gap <= at ? (wanted > gap && wanted <= at) : (wanted > gap || wanted <= at);
		if (!stays) {
			table_[gap] = table_[at];
			table_[at] = Entry();
			gap = at;
		}
	}
}

Constructions::Record *Constructions::add(const Object *p_object, uint32_t p_function, uint32_t p_class, uint32_t p_arguments) {
	const uint32_t values = p_arguments + static_cast<uint32_t>(module_.classes[p_class].fields.size());
	Record *record = nullptr;
	if (values < spare_.size() && !spare_[values].empty()) {
		record = spare_[values].back();
		spare_[values].pop_back();
	} else {
		record = static_cast<Record *>(std::malloc(sizeof(Record) + sizeof(Slot) * values));
	}
	if (!record) {
		forget(p_object); // With no memory for it, the object is one that is not remembered.
		return nullptr;
	}
	if ((used_ + 1) * 2 > table_.size()) {
		grow();
	}
	const size_t at = place(p_object);
	if (table_[at].record) {
		let_go(table_[at].record);
	} else {
		used_++;
	}
	record->function = p_function;
	record->klass = p_class;
	record->arguments = p_arguments;
	record->fields = NOT_YET;
	table_[at] = Entry{ p_object, record };
	return record;
}

bool Constructions::begun(uint32_t p_function, const Slot *p_arguments, size_t p_count) {
	if (!constructors_[p_function] || p_count == 0) {
		return false;
	}
	const ir::Function &function = module_.functions[p_function];
	const Object *object = static_cast<const Object *>(p_arguments[0].p);
	// A `super(...)` call runs a base's constructor on an object of another class: the object's own
	// constructor, which made that call, is the one whose arguments are kept.
	if (!object || object->klass != function.owner || p_count > function.parameters) {
		return false;
	}
	for (size_t i = 1; i < p_count; i++) {
		if (!plain(function.registers[i], p_arguments[i])) {
			forget(object);
			return false;
		}
	}
	Record *record = add(object, p_function, object->klass, static_cast<uint32_t>(p_count - 1));
	if (!record) {
		return false;
	}
	for (size_t i = 1; i < p_count; i++) {
		record->values[i - 1] = held(function.registers[i], p_arguments[i]);
	}
	return true;
}

void Constructions::constructed(Object *p_object) {
	const size_t at = place(p_object);
	Record *record = table_[at].record;
	if (!record) {
		return;
	}
	const ir::Class &klass = module_.classes[record->klass];
	for (uint32_t i = 0; i < klass.fields.size(); i++) {
		if (!plain(klass.fields[i].kind, field(klass, p_object, i))) {
			forget(p_object);
			return;
		}
	}
	for (uint32_t i = 0; i < klass.fields.size(); i++) {
		record->values[record->arguments + i] = held(klass.fields[i].kind, field(klass, p_object, i));
	}
	record->fields = static_cast<uint32_t>(klass.fields.size());
}

void Constructions::forget(const Object *p_object) {
	const size_t at = place(p_object);
	if (!table_[at].record) {
		return;
	}
	let_go(table_[at].record);
	table_[at] = Entry();
	used_--;
	remove(at);
}

const Constructions::Record *Constructions::of(const Object *p_object) const {
	const Record *record = table_[place(p_object)].record;
	return record && record->fields != NOT_YET ? record : nullptr;
}

void Constructions::hand_over(const Object *p_from, const Object *p_to) {
	const size_t from = place(p_from);
	Record *record = table_[from].record;
	if (!record) {
		return;
	}
	table_[from] = Entry();
	used_--;
	remove(from);
	forget(p_to);
	if ((used_ + 1) * 2 > table_.size()) {
		grow();
	}
	table_[place(p_to)] = Entry{ p_to, record };
	used_++;
}

} // namespace vm
} // namespace kas3
