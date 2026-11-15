// Where a name leads: its definition, and what counts as the same thing when its uses are looked for.
#include "query.h"

namespace kas3 {

namespace {

// True if the expression after `new` ends in the name at the offset: `X` or `a.b.X`.
bool names_at(const Expr *p_callee, uint32_t p_offset) {
	if (!p_callee) {
		return false;
	}
	if (p_callee->kind == NodeKind::Name) {
		return p_callee->offset == p_offset;
	}
	return p_callee->kind == NodeKind::Member && static_cast<const Member *>(p_callee)->name_offset == p_offset;
}

// A method or property that another class's or an interface's may stand for.
bool shared(const Symbol *p_member) {
	return p_member && p_member->is_instance_member() && p_member->kind != SymbolKind::Field &&
			!(p_member->modifiers & Modifiers::Private) && p_member->owner && p_member->owner->klass;
}

const Symbol *own_member(const Symbol *p_type, std::string_view p_name) {
	auto found = p_type->klass->members.find(p_name);
	return found == p_type->klass->members.end() ? nullptr : found->second;
}

// The member of that name a class's objects have: its own, or the nearest base's.
const Symbol *nearest_member(const Symbol *p_class, std::string_view p_name) {
	for (const Symbol *at = p_class; at && at->klass; at = at->klass->base) {
		if (const Symbol *found = own_member(at, p_name)) {
			return found;
		}
	}
	return nullptr;
}

// The interface and every interface it extends.
void add_interface(const Symbol *p_interface, std::unordered_set<const Symbol *> &r_found) {
	std::vector<const Symbol *> pending = { p_interface };
	while (!pending.empty()) {
		const Symbol *at = pending.back();
		pending.pop_back();
		if (!at || !at->klass || !r_found.insert(at).second) {
			continue;
		}
		pending.insert(pending.end(), at->klass->interfaces.begin(), at->klass->interfaces.end());
	}
}

// Every interface a class's objects are: what it and its bases implement, and what those extend. For an
// interface, itself and what it extends.
std::unordered_set<const Symbol *> interfaces_of(const Symbol *p_type) {
	std::unordered_set<const Symbol *> found;
	if (p_type->kind == SymbolKind::Interface) {
		add_interface(p_type, found);
		return found;
	}
	for (const Symbol *at = p_type; at && at->klass; at = at->klass->base) {
		for (const Symbol *interface : at->klass->interfaces) {
			add_interface(interface, found);
		}
	}
	return found;
}

} // namespace

Place Queries::definition(const Unit &p_unit, uint32_t p_offset) {
	const Occurrence *found = at(p_unit, p_offset);
	if (!found) {
		return Place();
	}
	const Symbol *symbol = found->symbol;
	if (symbol->klass && symbol->klass->constructor && !found->is_declaration) {
		for (const Node *node : context_nodes(p_unit)) {
			if (node->kind == NodeKind::New && names_at(static_cast<const New *>(node)->callee, found->offset)) {
				symbol = symbol->klass->constructor;
				break;
			}
		}
	}
	return declaration(symbol);
}

const std::vector<const Symbol *> &Queries::types() {
	if (!types_listed_) {
		types_listed_ = true;
		std::unordered_set<const Package *> packages;
		auto take = [&](const Symbol *p_symbol) {
			if (p_symbol->klass) {
				types_.push_back(p_symbol);
			}
		};
		for (const Unit &unit : compilation_.units()) {
			if (unit.package && packages.insert(unit.package).second) {
				for (const auto &[name, symbol] : unit.package->definitions) {
					take(symbol);
				}
			}
			for (const auto &[name, symbol] : unit.helpers) {
				take(symbol);
			}
		}
	}
	return types_;
}

std::unordered_set<const Symbol *> Queries::same_through_interfaces(const Symbol *p_member) {
	std::unordered_set<const Symbol *> same;
	if (!shared(p_member)) {
		return same;
	}
	const Symbol *owner = p_member->owner;
	auto add = [&](const Symbol *p_other) {
		if (p_other != p_member && shared(p_other) && p_other->kind == p_member->kind) {
			same.insert(p_other);
		}
	};
	const bool declared_by_interface = owner->kind == SymbolKind::Interface;
	for (const Symbol *type : types()) {
		const std::unordered_set<const Symbol *> interfaces = interfaces_of(type);
		if (type->kind == SymbolKind::Interface) {
			// One that extends the other and declares the method again.
			if (declared_by_interface && (interfaces.count(owner) || interfaces_of(owner).count(type))) {
				add(own_member(type, p_member->name));
			}
			continue;
		}
		const Symbol *has = nearest_member(type, p_member->name);
		if (declared_by_interface) {
			// What a call through the interface runs on an object of this class.
			if (interfaces.count(owner)) {
				add(has);
			}
		} else if (has == p_member) {
			// The interfaces through which this class's objects are called.
			for (const Symbol *interface : interfaces) {
				add(own_member(interface, p_member->name));
			}
		}
	}
	return same;
}

} // namespace kas3
