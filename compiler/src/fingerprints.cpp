#include "fingerprints.h"

#include "names.h"

namespace kas3 {

namespace {

uint64_t mix(uint64_t p_hash, uint64_t p_value) {
	return p_hash ^ (p_value + 0x9E3779B97F4A7C15ull + (p_hash << 6) + (p_hash >> 2));
}

uint64_t pointer(const void *p_pointer) {
	return (uint64_t)(uintptr_t)p_pointer;
}

uint64_t text(const Unit &p_unit, uint32_t p_begin, uint32_t p_end) {
	const std::string &source = p_unit.source.text();
	p_end = std::min<uint32_t>(p_end, (uint32_t)source.size());
	return p_begin >= p_end ? 0 : name_hash(std::string_view(source).substr(p_begin, p_end - p_begin));
}

// What the type resolved to, not how it is written: a written name that now finds another class differs.
uint64_t resolved(const TypeRef *p_type) {
	uint64_t hash = 1;
	for (; p_type; p_type = p_type->argument) {
		hash = mix(mix(hash, p_type->form), pointer(p_type->symbol));
	}
	return hash;
}

uint64_t metadata(const Unit &p_unit, const List<Metadata> &p_tags) {
	uint64_t hash = 2;
	for (const Metadata &tag : p_tags) {
		hash = mix(hash, text(p_unit, tag.offset, tag.end));
	}
	return hash;
}

// A function as a caller sees it: everything up to its body. An accessor that a base class supplies is
// read from the base class's file (p_unit null: the file of the function's symbol).
uint64_t function_print(const Unit *p_unit, const FunctionDecl *p_function) {
	const Unit *in = p_unit ? p_unit : p_function && p_function->symbol ? p_function->symbol->unit : nullptr;
	if (!p_function || !in) {
		return 3;
	}
	const Unit &unit = *in;
	uint64_t hash = text(unit, p_function->offset, p_function->body ? p_function->body->offset : p_function->end);
	hash = mix(hash, metadata(unit, p_function->metadata));
	for (const Parameter &parameter : p_function->parameters) {
		hash = mix(hash, resolved(parameter.type));
	}
	return mix(hash, resolved(p_function->return_type));
}

uint64_t variable_print(const Symbol *p_symbol) {
	const VarDecl *decl = static_cast<const VarDecl *>(p_symbol->node);
	uint64_t hash = mix(metadata(*p_symbol->unit, decl->metadata), decl->is_const);
	for (const Declarator &declarator : decl->declarators) {
		if (declarator.symbol == p_symbol) {
			hash = mix(hash, text(*p_symbol->unit, declarator.offset, declarator.end));
			hash = mix(hash, resolved(declarator.type));
		}
	}
	return hash;
}

// Every member a class or an interface writes, one that repeats another's name too: what a subtype must
// implement, override or not conflict with is read from the declarations, not only from the symbols.
uint64_t members_print(const Unit &p_unit, const Node *p_node) {
	uint64_t hash = 5;
	if (p_node->kind == NodeKind::InterfaceDecl) {
		for (const FunctionDecl *method : static_cast<const InterfaceDecl *>(p_node)->methods) {
			hash = mix(hash, function_print(&p_unit, method));
		}
		return hash;
	}
	for (const Node *member : static_cast<const ClassDecl *>(p_node)->members) {
		if (member->kind == NodeKind::FunctionDecl) {
			hash = mix(hash, function_print(&p_unit, static_cast<const FunctionDecl *>(member)));
		} else if (member->kind == NodeKind::VarDecl) {
			const VarDecl *decl = static_cast<const VarDecl *>(member);
			hash = mix(mix(hash, text(p_unit, decl->offset, decl->end)), metadata(p_unit, decl->metadata));
			for (const Declarator &declarator : decl->declarators) {
				hash = mix(hash, resolved(declarator.type));
			}
		}
	}
	return hash;
}

uint64_t header_print(const Symbol *p_symbol) {
	const Unit &unit = *p_symbol->unit;
	if (p_symbol->node->kind == NodeKind::ClassDecl) {
		const ClassDecl *decl = static_cast<const ClassDecl *>(p_symbol->node);
		const uint32_t end = decl->members.count ? decl->members[0]->offset : decl->end;
		return mix(text(unit, decl->offset, end), metadata(unit, decl->metadata));
	}
	const InterfaceDecl *decl = static_cast<const InterfaceDecl *>(p_symbol->node);
	const uint32_t end = decl->methods.count ? decl->methods[0]->offset : decl->end;
	return mix(text(unit, decl->offset, end), metadata(unit, decl->metadata));
}

uint64_t signature_print(const Symbol *p_symbol) {
	uint64_t hash = mix(mix(mix((uint64_t)p_symbol->kind, p_symbol->modifiers), p_symbol->is_const), p_symbol->file_private);
	hash = mix(mix(mix(hash, pointer(p_symbol->package)), pointer(p_symbol->owner)), name_hash(p_symbol->name));
	switch (p_symbol->kind) {
		case SymbolKind::Class:
		case SymbolKind::Interface:
			return p_symbol->node ? mix(hash, header_print(p_symbol)) : hash;
		case SymbolKind::Property:
			return mix(mix(hash, function_print(nullptr, p_symbol->getter)), function_print(nullptr, p_symbol->setter));
		case SymbolKind::Field:
		case SymbolKind::Variable:
			return p_symbol->node ? mix(hash, variable_print(p_symbol)) : hash;
		default:
			return p_symbol->node && p_symbol->node->kind == NodeKind::FunctionDecl
					? mix(hash, function_print(p_symbol->unit, static_cast<const FunctionDecl *>(p_symbol->node)))
					: hash;
	}
}

bool is_program_class(const Symbol *p_symbol) {
	return p_symbol && p_symbol->klass && p_symbol->unit && !p_symbol->unit->is_declaration;
}

} // namespace

void Fingerprints::forget(const std::vector<Symbol *> &p_declared) {
	for (const Symbol *symbol : p_declared) {
		auto found = prints_.find(symbol);
		if (found != prints_.end() && !found->second.kept) {
			found->second.kept = true;
			before_.emplace_back(symbol, found->second);
		}
	}
}

void Fingerprints::begin_round(const std::vector<Unit *> &p_units) {
	++round_;
	units_.clear();
	units_.insert(p_units.begin(), p_units.end());
}

const Prints &Fingerprints::class_prints(const Symbol *p_class) {
	Prints &prints = prints_[p_class];
	if (!units_.count(p_class->unit) || prints.classed == round_ || !visiting_.insert(p_class).second) {
		return prints; // Settled before this round, or printed already (a cycle is cut, so this ends).
	}
	const ClassInfo *info = p_class->klass;
	const auto supertype = [&](const Symbol *p_super, bool p_shape) -> uint64_t {
		if (!is_program_class(p_super)) {
			return pointer(p_super); // Declaration files never change in place.
		}
		const Prints &found = class_prints(p_super);
		return mix(pointer(p_super), p_shape ? found.shape : found.chain);
	};
	uint64_t chain = mix(mix(mix(prints.signature, info->broken), info->incomplete), supertype(info->base, false));
	for (const Symbol *interface : info->interfaces) {
		chain = mix(chain, supertype(interface, false));
	}
	uint64_t shape = mix(chain, supertype(info->base, true));
	for (const Symbol *interface : info->interfaces) {
		shape = mix(shape, supertype(interface, true));
	}
	for (const Symbol *member : p_class->unit->declared) {
		if (member->owner == p_class) {
			shape = mix(mix(shape, pointer(member)), prints_[member].signature);
		}
	}
	if (p_class->node) {
		shape = mix(shape, members_print(*p_class->unit, p_class->node));
	}
	Prints &stored = prints_[p_class]; // The map may have grown.
	stored.chain = chain;
	stored.shape = shape;
	stored.classed = round_;
	visiting_.erase(p_class);
	return stored;
}

void Fingerprints::emit(const Symbol *p_symbol, const Prints *p_before, const Prints *p_after,
		std::vector<Dependency> &r_keys) const {
	const bool changed = !p_before || !p_after;
	const Prints &any = p_after ? *p_after : *p_before;
	if (changed || p_before->signature != p_after->signature) {
		r_keys.push_back(on_symbol(p_symbol));
		if (p_symbol->owner) {
			r_keys.push_back(on_member(p_symbol->owner, any.constructor ? std::string_view() : std::string_view(p_symbol->name)));
		}
		if ((p_before && p_before->package) || (p_after && p_after->package)) {
			r_keys.push_back(on_name(p_symbol->name));
		}
	}
	// A package that gains its first definition, or loses its last, is found by an import of it all, or not.
	const Package *before = p_before ? p_before->package : nullptr;
	const Package *after = p_after ? p_after->package : nullptr;
	if (before != after) {
		for (const Package *package : { before, after }) {
			if (package) {
				r_keys.push_back(on_package(package->name));
			}
		}
	}
	if (p_symbol->klass) {
		if (changed || p_before->chain != p_after->chain) {
			r_keys.push_back(on_chain(p_symbol));
		}
		if (changed || p_before->shape != p_after->shape) {
			r_keys.push_back(on_shape(p_symbol));
		}
	}
}

void Fingerprints::finish_round(std::vector<Dependency> &r_keys) {
	std::vector<Symbol *> symbols;
	for (const Unit *unit : units_) {
		symbols.insert(symbols.end(), unit->declared.begin(), unit->declared.end());
	}
	for (Symbol *symbol : symbols) {
		Prints &prints = prints_[symbol];
		prints.kept = false;
		prints.printed = round_;
		prints.signature = signature_print(symbol);
		prints.package = !symbol->owner && !symbol->file_private ? symbol->package : nullptr;
		prints.constructor = symbol->owner && symbol->owner->klass && symbol->owner->klass->constructor == symbol;
	}
	for (Symbol *symbol : symbols) {
		if (symbol->klass) {
			class_prints(symbol);
		}
	}
	// What was printed before and again now is compared; what was printed before and not now is gone.
	for (const auto &[symbol, before] : before_) {
		auto now = prints_.find(symbol);
		if (now->second.printed == round_) {
			emit(symbol, &before, &now->second, r_keys);
			now->second.compared = round_;
		} else {
			emit(symbol, &before, nullptr, r_keys);
			prints_.erase(now);
		}
	}
	before_.clear();
	for (Symbol *symbol : symbols) {
		const Prints &prints = prints_[symbol];
		if (prints.compared != round_) {
			emit(symbol, nullptr, &prints, r_keys); // New.
		}
	}
}

} // namespace kas3
