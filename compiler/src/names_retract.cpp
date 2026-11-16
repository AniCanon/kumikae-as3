// Taking a file's declarations back before they are made again. They leave the package tables and wait in
// Tracking::retracted, from which the redo takes the same symbols: what other files hold of them stays
// valid, and a declaration that did not change is, to every other file, the same one.
#include "incremental.h"

#include "ast_walk.h"

#include <algorithm>

namespace kas3 {

void Compilation::remove_definition(Symbol *p_symbol) {
	Package *package = p_symbol->package;
	auto found = package->definitions.find(p_symbol->name);
	auto [begin, end] = package->repeated.equal_range(p_symbol->name);
	if (found == package->definitions.end() || found->second != p_symbol) {
		for (auto at = begin; at != end; ++at) {
			if (at->second == p_symbol) {
				package->repeated.erase(at);
				break;
			}
		}
		return;
	}
	// The first other file that defines the name has the definition now. The table's key is the name of the
	// symbol it holds, so it is put in again under the new one's.
	auto first = end;
	for (auto at = begin; at != end; ++at) {
		if (first == end || at->second->unit->rank < first->second->unit->rank) {
			first = at;
		}
	}
	package->definitions.erase(found);
	if (first != end) {
		tracking_->counts.firsts++;
		Symbol *next = first->second;
		package->repeated.erase(first);
		package->definitions.emplace(next->name, next);
	}
}

Symbol *Compilation::reused(SymbolKind p_kind, std::string_view p_name, Unit *p_unit) {
	if (!tracking_ || !p_unit) {
		return nullptr;
	}
	Symbol *symbol = nullptr;
	auto found = tracking_->retracted.find({ p_unit, reuse_owner_, p_kind, name_hash(p_name) });
	if (found != tracking_->retracted.end()) {
		std::vector<Symbol *> &waiting = found->second;
		auto same = std::find_if(waiting.begin(), waiting.end(), [&](const Symbol *p_symbol) { return p_symbol->name == p_name; });
		if (same != waiting.end()) {
			symbol = *same;
			waiting.erase(same); // The first written is the first taken: the order they were written in.
		}
	}
	if (!symbol && !tracking_->unused.empty()) {
		symbol = tracking_->unused.back();
		tracking_->unused.pop_back();
		tracking_->counts.reused++;
	}
	if (!symbol) {
		return nullptr;
	}
	ClassInfo *klass = symbol->kind == p_kind ? symbol->klass : nullptr;
	if (symbol->klass && !klass) {
		tracking_->unused_classes.push_back(symbol->klass);
	}
	*symbol = Symbol();
	if (klass) {
		*klass = ClassInfo();
		symbol->klass = klass;
	}
	return symbol;
}

void Compilation::retract(Unit &p_unit) {
	Tracking &tracking = *tracking_;
	tracking.prints.forget(p_unit.declared);
	for (Symbol *symbol : p_unit.declared) {
		if (!symbol->owner && !symbol->file_private && symbol->package) {
			remove_definition(symbol);
		}
		tracking.retracted[{ &p_unit, symbol->owner, symbol->kind, name_hash(symbol->name) }].push_back(symbol);
	}
	p_unit.declared.clear();
	p_unit.classes.clear();
	p_unit.helpers.clear();
	p_unit.references.clear();
	p_unit.typed.clear();
	p_unit.local_scopes.clear();
	p_unit.function_imports.clear();
	p_unit.signature_imports.clear();
	p_unit.body_symbols.clear();
	p_unit.block_imports = {};
	p_unit.outer_imports = {};
	p_unit.failed_import = false;
	p_unit.cited.clear();
	p_unit.halves_from.clear();
	p_unit.undoubted.reset();
	p_unit.doubted_at = 0;
	auto replacing = tracking.replacing.find(&p_unit);
	if (replacing != tracking.replacing.end()) {
		tracking.retired.push_back(std::move(p_unit.parsed));
		p_unit.parsed = std::move(replacing->second);
		tracking.replacing.erase(replacing);
		if (p_unit.parsed->used) {
			clear_annotations(p_unit.parsed->tree);
		}
		p_unit.parsed->used = true;
		p_unit.source = p_unit.parsed->source;
		p_unit.tree = p_unit.parsed->tree;
	} else {
		clear_annotations(p_unit.tree);
	}
	p_unit.diagnostics = p_unit.parsed->diagnostics;
	const bool broken = p_unit.diagnostics.has_errors();
	if (broken != p_unit.broken) {
		++doubt_state_; // Which files are broken changed: every judgement goes.
		broken ? ++broken_files_ : --broken_files_;
	}
	p_unit.broken = broken;
	p_unit.bodies_only = false;
	if (broken) {
		p_unit.diagnostics.mute();
	}
	if (!p_unit.unchecked) {
		p_unit.unchecked = true;
		tracking.unchecked.insert(p_unit.rank);
	}
}

} // namespace kas3
