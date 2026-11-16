// What a class has from its bases: its members as code may name them, the halves of a property, and whether
// the hierarchy above it is known.
#include "names.h"

namespace kas3 {

bool Compilation::hierarchy_broken(Symbol *p_class) const {
	for (const ClassInfo *info = p_class->klass; info; info = info->base ? info->base->klass : nullptr) {
		if (info->chain_known) {
			return info->chain_broken;
		}
		if (info->broken || info->incomplete) {
			return true;
		}
	}
	return false;
}

FunctionDecl *Compilation::property_half(Symbol *p_through, const Symbol *p_property, FunctionDecl::Accessor p_half,
		bool *r_known) {
	const auto half = [&](const Symbol *p_symbol) { return p_half == FunctionDecl::Getter ? p_symbol->getter : p_symbol->setter; };
	FunctionDecl *found = half(p_property);
	Symbol *from = p_through && p_through->kind == SymbolKind::Interface ? p_through : p_property->owner;
	bool known = from != nullptr;
	if (!found && from && from->kind == SymbolKind::Interface) {
		// The interface, then everything it extends: AVM2 finds an interface's member by its name.
		std::vector<Symbol *> pending{ from };
		for (size_t i = 0; i < pending.size() && !found; i++) {
			ClassInfo *info = pending[i]->klass;
			Symbol *member = find_in(info->members, p_property->name);
			found = member && member->kind == SymbolKind::Property ? half(member) : nullptr;
			const Node *node = pending[i]->node;
			known = known && !info->broken && node && node->kind == NodeKind::InterfaceDecl &&
					info->interfaces.size() == static_cast<const InterfaceDecl *>(node)->extends.count;
			for (Symbol *parent : info->interfaces) {
				known = known && parent;
				if (parent && std::find(pending.begin(), pending.end(), parent) == pending.end()) {
					pending.push_back(parent);
				}
			}
		}
	} else if (!found && from) {
		known = !hierarchy_broken(from);
	}
	if (r_known) {
		*r_known = known;
	}
	return found;
}

// Works the answer to hierarchy_broken out once for every class in the list, so that a deep hierarchy is not
// walked again for each question. Called when no base class will change any more.
void Compilation::settle_hierarchy(const std::vector<Symbol *> &p_classes) {
	std::vector<ClassInfo *> chain;
	for (Symbol *symbol : p_classes) {
		symbol->klass->chain_known = false;
	}
	for (Symbol *symbol : p_classes) {
		chain.clear();
		bool broken = false;
		for (ClassInfo *info = symbol->klass; info; info = info->base ? info->base->klass : nullptr) {
			if (info->chain_known) {
				broken = info->chain_broken;
				break;
			}
			chain.push_back(info);
		}
		for (size_t i = chain.size(); i-- > 0;) {
			broken = broken || chain[i]->broken || chain[i]->incomplete;
			chain[i]->chain_broken = broken;
			chain[i]->chain_known = true;
		}
	}
}

bool inherited_visible(const Symbol *p_member, const Symbol *p_base, const Package *p_package) {
	if (p_member->modifiers & Modifiers::Private) {
		return false;
	}
	if (p_member->modifiers & (Modifiers::Public | Modifiers::Protected)) {
		return true;
	}
	return p_base->package == p_package;
}

Symbol *Compilation::find_member(Symbol *p_class, std::string_view p_name, bool p_static_only,
		Symbol **r_instance, Symbol **r_hidden) const {
	// An object's own members come before those of its class, as they do when the program runs.
	for (int statics = 0; statics < 2; statics++) {
		for (Symbol *owner = p_class; owner; owner = owner->klass->base) {
			Symbol *member = find_in(owner->klass->table(statics != 0), p_name);
			if (!member) {
				continue;
			}
			if (owner != p_class && !inherited_visible(member, owner, p_class->package)) {
				if (r_hidden && !*r_hidden) {
					*r_hidden = member;
				}
				continue;
			}
			if (p_static_only && !statics) {
				if (r_instance && !*r_instance) {
					*r_instance = member;
				}
				break;
			}
			return member;
		}
	}
	return nullptr;
}

} // namespace kas3
