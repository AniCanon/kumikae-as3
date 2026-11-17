#include "constructions.h"

#include "any.h"
#include "fields.h"

#include <algorithm>
#include <cstddef>

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

void release_text(Kind p_kind, Slot p_value) {
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

// A record starts with its two numbers in the place of one slot, and its values follow as slots.
static_assert(offsetof(Constructions::Record, values) == sizeof(Slot), "a record's values start one slot in");

Constructions::Constructions(const ir::Module &p_module) :
		module_(p_module), owner_(p_module.functions.size(), ir::NONE), plans_(p_module.classes.size()) {
	const auto looked = [this](uint32_t p_value, Kind p_kind) {
		if (p_kind == Kind::Str || p_kind == Kind::Obj || p_kind == Kind::Val || p_kind == Kind::Ref || p_kind == Kind::Any) {
			looked_.push_back(Looked{ p_value, p_kind });
		}
	};
	for (uint32_t index = 0; index < module_.classes.size(); index++) {
		const ir::Class &klass = module_.classes[index];
		const bool script = !klass.built_in && !klass.is_interface && !klass.table && klass.element == Kind::Void && klass.native_base.empty();
		if (!script || klass.constructor >= module_.functions.size()) {
			continue;
		}
		const ir::Function &made = module_.functions[klass.constructor];
		// A constructor that takes any number of arguments is given them as an Array: never plain.
		if (made.rest || made.takes_extra || made.owner != index || made.parameters == 0) {
			continue;
		}
		Plan &plan = plans_[index];
		plan.at = static_cast<uint32_t>(slots_of(klass));
		plan.constructor = klass.constructor;
		plan.most = made.parameters - 1;
		plan.room = 1 + plan.most + static_cast<uint32_t>(klass.fields.size());
		plan.looked_arguments = static_cast<uint32_t>(looked_.size());
		for (uint32_t i = 0; i < plan.most; i++) {
			looked(i, made.registers[1 + i]);
		}
		plan.looked_fields = static_cast<uint32_t>(looked_.size());
		for (uint32_t i = 0; i < klass.fields.size(); i++) {
			looked(i, klass.fields[i].kind);
		}
		plan.looked_end = static_cast<uint32_t>(looked_.size());
		plan.plain_arguments = plan.looked_fields == plan.looked_arguments;
		owner_[klass.constructor] = index;
	}
}

Kind Constructions::kind_of(uint32_t p_class, const Record &p_record, uint32_t p_value) const {
	if (p_value < p_record.arguments) {
		return module_.functions[plans_[p_class].constructor].registers[1 + p_value];
	}
	return module_.classes[p_class].fields[p_value - p_record.arguments].kind;
}

void Constructions::give_up(const Plan &p_plan, Record *p_record) {
	for (uint32_t i = p_plan.looked_arguments; i < p_plan.looked_fields && looked_[i].value < p_record->arguments; i++) {
		release_text(looked_[i].kind, p_record->values[looked_[i].value]);
	}
	if (p_record->kept == ALL) {
		for (uint32_t i = p_plan.looked_fields; i < p_plan.looked_end; i++) {
			release_text(looked_[i].kind, p_record->values[p_record->arguments + looked_[i].value]);
		}
	}
}

Constructions::Chain Constructions::chain_of(Plan &p_plan, uint32_t p_class) {
	if (p_plan.chain == Chain::Unknown) {
		p_plan.chain = ir::constructor_refusal(module_, p_class) ? Chain::Once : Chain::Again;
		p_plan.quick = p_plan.chain == Chain::Again && p_plan.plain_arguments;
	}
	return p_plan.chain;
}

Constructions::Record *Constructions::add(Object *p_object, uint32_t p_function, uint32_t p_class, uint32_t p_arguments) {
	const Plan &plan = plans_[p_object->klass];
	if (plan.room == 0) {
		return nullptr;
	}
	Record *record = record_at(p_object, plan);
	if (p_object->klass != p_class || plan.constructor != p_function || p_arguments > plan.most) {
		forget(p_object);
		return nullptr;
	}
	if (record->kept != NOTHING) {
		give_up(plan, record);
	} else {
		used_++;
	}
	record->kept = ARGUMENTS;
	record->arguments = p_arguments;
	return record;
}

bool Constructions::keep_arguments(uint32_t p_class, const Slot *p_arguments, size_t p_count) {
	Plan &plan = plans_[p_class];
	Object *object = p_count == 0 ? nullptr : static_cast<Object *>(p_arguments[0].p);
	// A `super(...)` call runs a base's constructor on an object of another class: the object's own
	// constructor, which made that call, is the one whose arguments are kept.
	if (!object || object->klass != p_class || p_count > plan.most + 1) {
		return false;
	}
	Record *record = record_at(object, plan);
	if (!plan.quick || record->kept != NOTHING) {
		return keep_looked(plan, p_class, object, p_arguments, p_count);
	}
	const uint32_t given = static_cast<uint32_t>(p_count - 1);
	for (uint32_t i = 0; i < given; i++) {
		record->values[i] = p_arguments[1 + i];
	}
	record->kept = ARGUMENTS;
	record->arguments = given;
	used_++;
	return false;
}

bool Constructions::keep_looked(Plan &p_plan, uint32_t p_class, Object *p_object, const Slot *p_arguments, size_t p_count) {
	const uint32_t given = static_cast<uint32_t>(p_count - 1);
	for (uint32_t i = p_plan.looked_arguments; i < p_plan.looked_fields && looked_[i].value < given; i++) {
		if (!plain(looked_[i].kind, p_arguments[1 + looked_[i].value])) {
			forget(p_object);
			return false;
		}
	}
	Record *record = record_at(p_object, p_plan);
	if (record->kept != NOTHING) {
		give_up(p_plan, record);
	} else {
		used_++;
	}
	for (uint32_t i = 0; i < given; i++) {
		record->values[i] = p_arguments[1 + i];
	}
	for (uint32_t i = p_plan.looked_arguments; i < p_plan.looked_fields && looked_[i].value < given; i++) {
		held(looked_[i].kind, p_arguments[1 + looked_[i].value]);
	}
	record->kept = ARGUMENTS;
	record->arguments = given;
	// The first values are owed only where the chain can't give them again.
	return chain_of(p_plan, p_class) == Chain::Once;
}

void Constructions::constructed(Object *p_object) {
	const Plan &plan = plans_[p_object->klass];
	Record *record = plan.room != 0 ? record_at(p_object, plan) : nullptr;
	if (!record || record->kept != ARGUMENTS) {
		return;
	}
	const ir::Class &klass = module_.classes[p_object->klass];
	for (uint32_t i = plan.looked_fields; i < plan.looked_end; i++) {
		if (!plain(looked_[i].kind, field(klass, p_object, looked_[i].value))) {
			forget(p_object);
			return;
		}
	}
	Slot *first = record->values + record->arguments;
	for (uint32_t i = 0; i < klass.fields.size(); i++) {
		first[i] = field(klass, p_object, i);
	}
	for (uint32_t i = plan.looked_fields; i < plan.looked_end; i++) {
		held(looked_[i].kind, first[looked_[i].value]);
	}
	record->kept = ALL;
}

const Constructions::Record *Constructions::of(const Object *p_object) {
	Plan &plan = plans_[p_object->klass];
	const Record *record = plan.room != 0 ? record_at(const_cast<Object *>(p_object), plan) : nullptr;
	const bool known = record && (record->kept == ALL || (record->kept == ARGUMENTS && chain_of(plan, p_object->klass) == Chain::Again));
	return known ? record : nullptr;
}

void Constructions::keep_arguments_only(Object *p_object) {
	const Plan &plan = plans_[p_object->klass];
	Record *record = plan.room != 0 ? record_at(p_object, plan) : nullptr;
	if (!record || record->kept == NOTHING) {
		return;
	}
	if (record->kept == ALL) {
		for (uint32_t i = plan.looked_fields; i < plan.looked_end; i++) {
			release_text(looked_[i].kind, record->values[record->arguments + looked_[i].value]);
		}
	}
	record->kept = ARGUMENTS;
}

} // namespace vm
} // namespace kas3
