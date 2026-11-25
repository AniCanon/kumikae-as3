// Package-level lookup: where a name is looked up from, what it may see, and the type a written type names.
#include "names.h"

namespace kas3 {

Context Compilation::context_of(Unit &p_unit, bool p_in_block) {
	Context context;
	context.unit = &p_unit;
	context.package = p_unit.package;
	context.imports = p_in_block ? &p_unit.block_imports : &p_unit.outer_imports;
	return context;
}

Context Compilation::context_of(Symbol *p_class) {
	Context context = context_of(*p_class->unit, !p_class->file_private);
	context.imports = &p_class->klass->imports;
	context.klass = p_class;
	return context;
}

bool Compilation::visible(const Symbol *p_definition, const Context &p_from) const {
	// A declaration file describes code that was compiled without this program: nothing the program defines
	// can change what its names mean.
	if (p_from.unit->is_declaration && !p_definition->unit->is_declaration) {
		return false;
	}
	if (p_definition->file_private) {
		return p_definition->unit == p_from.unit;
	}
	if (p_definition->modifiers & Modifiers::Public) {
		return true;
	}
	return p_definition->package == p_from.package;
}

static void add_match(const Compilation &p_compilation, const Context &p_from, Symbol *p_symbol, Found &r_found) {
	if (!p_symbol || p_symbol == r_found.first || p_symbol == r_found.second) {
		return;
	}
	if (!p_compilation.visible(p_symbol, p_from)) {
		if (!r_found.hidden) {
			r_found.hidden = p_symbol;
		}
	} else if (!r_found.first) {
		r_found.first = p_symbol;
	} else if (!r_found.second) {
		r_found.second = p_symbol;
	}
}

// AS3 opens every namespace in reach at once: the file's helpers, the current package, the top-level package,
// and every import. Two different matches are an ambiguity, never a shadowing.
//
// A declaration file is different: its own package comes first, then its imports, then the top level, so that
// a library's `Object` is the library's.
Found Compilation::lookup_package_level(const Context &p_from, std::string_view p_name) const {
	Found found;
	if (p_from.unit->is_builtin && (p_name == "float" || p_name == "byte")) {
		found.first = builtin(p_name);
		return found;
	}
	bool ordered = p_from.unit->is_declaration;
	add_match(*this, p_from, find_in(p_from.unit->helpers, p_name), found);
	add_match(*this, p_from, find_in(p_from.package->definitions, p_name), found);
	if (ordered && found.first) {
		return found;
	}
	if (!ordered) {
		add_match(*this, p_from, find_in(top_level_->definitions, p_name), found);
	}
	for (const ImportScope *scope = p_from.imports; scope; scope = scope->parent) {
		for (const ResolvedImport &import : scope->imports) {
			if (import.symbol) {
				if (import.symbol->name == p_name) {
					add_match(*this, p_from, import.symbol, found);
				}
			} else {
				add_match(*this, p_from, find_in(import.package->definitions, p_name), found);
			}
		}
	}
	if (ordered && !found.first) {
		add_match(*this, p_from, find_in(top_level_->definitions, p_name), found);
	}
	if (!found.first) {
		for (const ImportScope *scope = p_from.imports; scope && !found.suppressed; scope = scope->parent) {
			found.suppressed = scope->failed_wildcard;
			for (std::string_view failed : scope->failed) {
				found.suppressed |= failed == p_name;
			}
		}
	}
	if (!found.first && !found.hidden && !found.suppressed && (p_name == "float" || p_name == "byte")) {
		found.first = builtin(p_name);
	}
	return found;
}

Found Compilation::lookup_qualified(const Context &p_from, const std::string &p_package,
		std::string_view p_name) const {
	Found found;
	auto package = packages_.find(p_package);
	if (package != packages_.end()) {
		add_match(*this, p_from, find_in(package->second.definitions, p_name), found);
	}
	return found;
}

std::string Compilation::ambiguous_message(std::string_view p_name, const Found &p_found) const {
	return quoted(p_name) + " is ambiguous: it could be " + qualified_name(p_found.first) + " or " +
			qualified_name(p_found.second);
}

Symbol *Compilation::resolve_type(const Context &p_from, TypeRef *p_type) {
	if (p_type && p_type->form == TypeRef::FixedArray) {
		resolve_type(p_from, p_type->argument);
		if (p_type->argument->form == TypeRef::Void || p_type->argument->form == TypeRef::Callback) {
			p_from.unit->diagnostics.error(p_type->argument->offset, "a fixed array element must be a storable value type");
		}
		return nullptr;
	}
	if (p_type && p_type->form == TypeRef::Callback) {
		auto component = [&](TypeRef *p_part, bool p_result) {
			Symbol *symbol = resolve_type(p_from, p_part);
			if ((p_result && p_part->form == TypeRef::Void) ||
					(!p_result && p_part->form == TypeRef::Named && symbol && symbol->is_type() &&
							symbol->unit && !symbol->unit->is_declaration)) {
				return;
			}
			for (std::string_view name : { "int", "uint", "byte", "Int64", "float", "Number", "Boolean", "String" }) {
				if (symbol && symbol == builtin(name)) {
					return;
				}
			}
			p_from.unit->diagnostics.error(p_part->offset, "callback parameters require a scalar, String or script type; results require a scalar, String or void");
		};
		for (TypeRef *part : p_type->parameters) {
			component(part, false);
		}
		component(p_type->argument, true);
		return p_type->symbol = builtin("Function");
	}
	if (!p_type || p_type->form == TypeRef::Any || p_type->form == TypeRef::Void || p_type->name.count == 0) {
		return nullptr;
	}
	// Type arguments nest at most Parser::MAX_TYPE_DEPTH deep, so this recursion is shallow.
	if (p_type->form == TypeRef::Applied) {
		resolve_type(p_from, p_type->argument);
	}
	Diagnostics &diagnostics = p_from.unit->diagnostics;
	uint32_t last = p_type->name.count - 1;
	if (last == 0 && p_type->form == TypeRef::Named && p_from.klass == vector_ && p_type->name[0] == "T") {
		if (!element_) {
			element_ = new_symbol(SymbolKind::Class, "T", p_from.unit, 0);
		}
		p_type->symbol = element_;
		return element_;
	}
	std::string shown = dotted_name(p_type->name, p_type->name.count);
	Found found = last == 0 ? lookup_package_level(p_from, p_type->name[0])
							: lookup_qualified(p_from, dotted_name(p_type->name, last), p_type->name[last]);
	if (!found.first) {
		if (found.hidden && last > 0) {
			diagnostics.unresolved(p_type->offset, quoted(shown) + " is not public");
		} else if (found.suppressed) {
			// A failed import has been reported, and may be where this name was to come from.
		} else {
			diagnostics.unresolved(p_type->offset, "unknown type " + quoted(shown));
		}
		return nullptr;
	}
	if (found.second) {
		diagnostics.error(p_type->offset, ambiguous_message(shown, found));
		return nullptr;
	}
	if (!found.first->is_type()) {
		diagnostics.error(p_type->offset, quoted(shown) + " is not a type");
		return nullptr;
	}
	if (p_type->form == TypeRef::Applied && found.first != vector_) {
		diagnostics.error(p_type->offset, "only Vector takes a type parameter");
		return nullptr;
	}
	p_type->symbol = found.first;
	p_from.unit->references.push_back({ p_type->name_offsets[last], p_type->name[last], found.first });
	return found.first;
}

} // namespace kas3
