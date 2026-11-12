// Rules between classes: an override repeats the signature it overrides, and a class has every method of
// the interfaces it implements. Runs after names are resolved, so that written types can be compared.
#include "names.h"

#include <algorithm>
#include <string>
#include <vector>

namespace kas3 {

namespace {

std::string quoted(std::string_view p_name) {
	return "'" + std::string(p_name) + "'";
}

Symbol *find_in(const std::unordered_map<std::string_view, Symbol *> &p_table, std::string_view p_name) {
	auto found = p_table.find(p_name);
	return found == p_table.end() ? nullptr : found->second;
}

class Rules {
	Compilation &compilation_;

	bool same_type(TypeRef *p_a, TypeRef *p_b) const {
		const Type *a = compilation_.type_of(p_a);
		const Type *b = compilation_.type_of(p_b);
		return a == b || a->kind == Type::Unknown || b->kind == Type::Unknown;
	}

	// AS3 wants the parameters and the return type repeated exactly.
	bool same_signature(const FunctionDecl *p_a, const FunctionDecl *p_b) const {
		if (p_a->parameters.count != p_b->parameters.count) {
			return false;
		}
		for (size_t i = 0; i < p_a->parameters.count; i++) {
			const Parameter &a = p_a->parameters[i];
			const Parameter &b = p_b->parameters[i];
			if (a.is_rest != b.is_rest || (a.default_value != nullptr) != (b.default_value != nullptr)) {
				return false;
			}
			if (!a.is_rest && !same_type(a.type, b.type)) {
				return false;
			}
		}
		// A return type that one of them leaves out is not compared.
		return !p_a->return_type || !p_b->return_type || same_type(p_a->return_type, p_b->return_type);
	}

	// The instance function of that name and kind that the class has or inherits. Private members of base
	// classes are not inherited.
	FunctionDecl *find_function(Symbol *p_class, std::string_view p_name, FunctionDecl::Accessor p_accessor,
			bool p_skip_own) const {
		for (Symbol *owner = p_skip_own ? p_class->klass->base : p_class; owner; owner = owner->klass->base) {
			Symbol *member = find_in(owner->klass->members, p_name);
			if (!member || (owner != p_class && (member->modifiers & Modifiers::Private))) {
				continue;
			}
			if (p_accessor == FunctionDecl::None) {
				return member->kind == SymbolKind::Method ? static_cast<FunctionDecl *>(member->node) : nullptr;
			}
			if (member->kind != SymbolKind::Property) {
				return nullptr;
			}
			// A class may declare one half and inherit the other.
			FunctionDecl *half = p_accessor == FunctionDecl::Getter ? member->getter : member->setter;
			if (half) {
				return half;
			}
		}
		return nullptr;
	}

	static std::string same_message(const FunctionDecl *p_function, Symbol *p_owner) {
		return quoted(p_function->name) + " must have the same parameters and return type as " +
				quoted(p_function->name) + " in " + std::string(p_owner->name);
	}

	void overrides(Symbol *p_class, ClassDecl *p_decl) {
		for (Node *node : p_decl->members) {
			if (node->kind != NodeKind::FunctionDecl) {
				continue;
			}
			FunctionDecl *function = static_cast<FunctionDecl *>(node);
			uint16_t bits = function->modifiers.bits;
			if (!(bits & Modifiers::Override) || (bits & Modifiers::Static)) {
				continue;
			}
			FunctionDecl *base = find_function(p_class, function->name, function->accessor, true);
			// Built-in members may be redefined freely: see check_overrides.
			if (!base || !base->symbol || !base->symbol->owner || base->symbol->owner->unit->is_builtin) {
				continue;
			}
			if (!same_signature(function, base)) {
				p_class->unit->diagnostics.error(function->name_offset, same_message(function, base->symbol->owner));
			}
		}
	}

	void interfaces(Symbol *p_class, ClassDecl *p_decl) {
		// Everything the class itself promises: the interfaces it names and all that those extend. What a
		// base class promised is the base class's to keep.
		std::vector<Symbol *> promised;
		if (p_class->klass->interfaces.size() != p_decl->implements.count) {
			return;
		}
		for (Symbol *interface : p_class->klass->interfaces) {
			if (!interface) {
				return; // One did not resolve; its error is enough.
			}
			if (std::find(promised.begin(), promised.end(), interface) == promised.end()) {
				promised.push_back(interface);
			}
		}
		for (size_t i = 0; i < promised.size(); i++) {
			if (promised[i]->klass->broken) {
				return;
			}
			for (Symbol *parent : promised[i]->klass->interfaces) {
				if (!parent) {
					return;
				}
				if (std::find(promised.begin(), promised.end(), parent) == promised.end()) {
					promised.push_back(parent);
				}
			}
		}
		Diagnostics &diagnostics = p_class->unit->diagnostics;
		for (Symbol *interface : promised) {
			for (FunctionDecl *method : static_cast<InterfaceDecl *>(interface->node)->methods) {
				FunctionDecl *found = find_function(p_class, method->name, method->accessor, false);
				const char *kind = method->accessor == FunctionDecl::Getter ? "the getter "
						: method->accessor == FunctionDecl::Setter		 ? "the setter "
																		 : "";
				if (!found) {
					diagnostics.error(p_decl->name_offset, quoted(p_class->name) + " must implement " + kind +
																   quoted(method->name) + " from " + std::string(interface->name));
					continue;
				}
				// Reported where the method is, if it is in this class; otherwise at the class.
				bool own = found->symbol && found->symbol->owner == p_class;
				uint32_t offset = own ? found->name_offset : p_decl->name_offset;
				if (!(found->modifiers.bits & Modifiers::Public)) {
					diagnostics.error(offset, quoted(method->name) + " must be public to implement " +
													  std::string(interface->name));
				} else if (!same_signature(found, method)) {
					diagnostics.error(offset, same_message(method, interface));
				}
			}
		}
	}

	void definition(Unit &p_unit, Node *p_node) {
		if (p_node->kind != NodeKind::ClassDecl) {
			return;
		}
		ClassDecl *decl = static_cast<ClassDecl *>(p_node);
		Symbol *symbol = decl->symbol;
		if (!symbol || !symbol->klass || symbol->node != decl || compilation_.hierarchy_broken(symbol)) {
			return;
		}
		overrides(symbol, decl);
		if (!p_unit.is_declaration) {
			interfaces(symbol, decl); // A class in a declaration file is taken at its word.
		}
	}

public:
	explicit Rules(Compilation &p_compilation) :
			compilation_(p_compilation) {}

	void run() {
		for (Unit &unit : compilation_.units()) {
			if (unit.is_builtin) {
				continue;
			}
			if (unit.tree->package) {
				for (Node *node : unit.tree->package->definitions) {
					definition(unit, node);
				}
			}
			for (Node *node : unit.tree->definitions) {
				definition(unit, node);
			}
		}
	}
};

} // namespace

void check_class_rules(Compilation &p_compilation) {
	Rules(p_compilation).run();
}

} // namespace kas3
