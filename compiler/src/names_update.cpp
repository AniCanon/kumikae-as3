// Checking a program as it is edited. The declaration layer (declare, link, signatures) of the files an edit
// touches is redone at once; the files whose fingerprinted reads changed are redone after them, round by
// round, until nothing more changes. Bodies wait until they are asked for: each file's read only what other
// files declare, so they are checked one file at a time, in any order.
#include "incremental.h"

#include "ast_walk.h"

#include <algorithm>
#include <cstdio>

namespace kas3 {

void resolve_unit_signatures(Compilation &p_compilation, Unit &p_unit); // names_signatures.cpp
void check_unit_bodies(Compilation &p_compilation, Unit &p_unit); // names_bodies.cpp
void declaration_dependencies(const Unit &p_unit, std::vector<Dependency> &r_keys); // names_dependencies.cpp
void body_dependencies(const Unit &p_unit, std::vector<Dependency> &r_keys);

namespace {

std::vector<Symbol *> classes_of(const std::vector<Unit *> &p_units) {
	std::vector<Symbol *> classes;
	for (Unit *unit : p_units) {
		classes.insert(classes.end(), unit->classes.begin(), unit->classes.end());
	}
	return classes;
}

void in_order(std::vector<Unit *> &r_units) {
	std::sort(r_units.begin(), r_units.end(), [](const Unit *p_a, const Unit *p_b) { return p_a->rank < p_b->rank; });
	r_units.erase(std::unique(r_units.begin(), r_units.end()), r_units.end());
}

// The names a file defines at package level: what another file's definition of the same name collides with.
void package_names(const Unit &p_unit, std::vector<Dependency> &r_keys) {
	for (const Symbol *symbol : p_unit.declared) {
		if (!symbol->owner && !symbol->file_private) {
			r_keys.push_back(on_defines(symbol->name));
		}
	}
}

// What a file says once its declarations and signatures are checked: its bodies, checked again alone,
// start from here.
void keep_declared(Unit &p_unit) {
	p_unit.declared_diagnostics = p_unit.diagnostics;
	p_unit.declared_references = p_unit.references.size();
	p_unit.declared_cited = p_unit.cited.size();
	p_unit.declared_failed_import = p_unit.failed_import;
}

} // namespace

void Compilation::declare_files(const std::vector<Unit *> &p_units) {
	for (Unit *unit : p_units) {
		declare_unit(*unit);
	}
	if (!object_) {
		auto object = top_level_->definitions.find("Object");
		object_ = object == top_level_->definitions.end() ? nullptr : object->second;
		auto vector = top_level_->definitions.find("Vector");
		vector_ = vector == top_level_->definitions.end() ? nullptr : vector->second;
	}
	for (Unit *unit : p_units) {
		link_imports(*unit);
	}
	const std::vector<Symbol *> classes = classes_of(p_units);
	link_bases(classes);
	settle_classes(classes);
	for (Unit *unit : p_units) {
		resolve_unit_signatures(*this, *unit);
		keep_declared(*unit);
	}
}

// The files of a cycle that the classes being redone are part of, and that are not being redone: the error
// goes to the class declared first, so the whole cycle is taken together. An interface cycle anywhere, then
// or now, takes every interface of the program: where it is cut depends on the order they are walked in.
std::vector<Unit *> Compilation::cycle_files(const std::vector<Symbol *> &p_classes, const std::vector<Unit *> &p_units) {
	std::vector<Unit *> more;
	const auto outside = [&](Unit *p_unit) {
		return !p_unit->is_declaration && std::find(p_units.begin(), p_units.end(), p_unit) == p_units.end() &&
				std::find(more.begin(), more.end(), p_unit) == more.end();
	};
	bool interfaces = false;
	for (Symbol *start : p_classes) {
		interfaces = interfaces || start->kind == SymbolKind::Interface;
		if (start->kind != SymbolKind::Class) {
			continue;
		}
		std::vector<Symbol *> walked;
		for (Symbol *at = start; at && at->klass; at = at->klass->base) {
			auto seen = std::find(walked.begin(), walked.end(), at);
			if (seen != walked.end()) {
				for (; seen != walked.end(); ++seen) {
					if (outside((*seen)->unit)) {
						more.push_back((*seen)->unit);
					}
				}
				break;
			}
			walked.push_back(at);
		}
	}
	if (interfaces && !interface_cycles_) {
		// A cycle through the new links: a walk from each interface being redone that comes back to itself.
		for (Symbol *start : p_classes) {
			if (start->kind != SymbolKind::Interface) {
				continue;
			}
			std::vector<Symbol *> pending{ start };
			for (size_t i = 0; i < pending.size() && !interface_cycles_; ++i) {
				for (Symbol *parent : pending[i]->klass->interfaces) {
					interface_cycles_ = interface_cycles_ || parent == start;
					if (parent && std::find(pending.begin(), pending.end(), parent) == pending.end()) {
						pending.push_back(parent);
					}
				}
			}
		}
	}
	if (interfaces && interface_cycles_) {
		for (Unit &unit : units_) {
			for (Symbol *symbol : unit.classes) {
				if (symbol->kind == SymbolKind::Interface && outside(&unit)) {
					more.push_back(&unit);
				}
			}
		}
	}
	return more;
}

void Compilation::redeclare(std::vector<Unit *> &r_units, std::vector<Dependency> &r_changed) {
	Tracking &tracking = *tracking_;
	std::vector<Symbol *> classes;
	std::vector<std::pair<Unit *, bool>> broken_before;
	std::vector<Dependency> names_before;
	for (;;) {
		in_order(r_units);
		for (Unit *unit : r_units) {
			if (std::none_of(broken_before.begin(), broken_before.end(), [&](const auto &p_entry) { return p_entry.first == unit; })) {
				broken_before.emplace_back(unit, unit->broken);
				package_names(*unit, names_before);
			}
			retract(*unit);
		}
		for (Unit *unit : r_units) {
			declare_unit(*unit);
		}
		for (Unit *unit : r_units) {
			link_imports(*unit);
		}
		classes = classes_of(r_units);
		link_bases(classes);
		std::vector<Unit *> more = cycle_files(classes, r_units);
		if (more.empty()) {
			break;
		}
		r_units.insert(r_units.end(), more.begin(), more.end());
		tracking.counts.cycles++;
	}
	if (interface_cycles_ && std::any_of(classes.begin(), classes.end(), [](const Symbol *p_class) { return p_class->kind == SymbolKind::Interface; })) {
		interface_cycles_ = false; // Every interface of the program is walked again: found anew.
	}
	settle_classes(classes);
	for (Unit *unit : r_units) {
		resolve_unit_signatures(*this, *unit);
		keep_declared(*unit);
	}
	if (broken_files_) {
		// Doubt follows a symbol through its types to their bases, further than any file records: every file
		// is judged again while a file does not parse.
		++doubt_state_;
	}
	// What was not declared again is gone.
	for (auto &[slot, symbols] : tracking.retracted) {
		for (Symbol *symbol : symbols) {
			// Its tree goes at the end of the update: nothing may reach it through the symbol after.
			symbol->node = nullptr;
			symbol->type = nullptr;
			symbol->getter = nullptr;
			symbol->setter = nullptr;
			tracking.unused.push_back(symbol);
		}
	}
	tracking.retracted.clear();
	tracking.prints.begin_round(r_units);
	tracking.prints.finish_round(r_changed);
	for (const auto &[unit, broken] : broken_before) {
		if (broken != unit->broken) {
			// "Is already defined" is said otherwise of a definition in a file that does not parse.
			r_changed.insert(r_changed.end(), names_before.begin(), names_before.end());
			package_names(*unit, r_changed);
		}
	}
	for (Unit *unit : r_units) {
		std::vector<Dependency> keys;
		declaration_dependencies(*unit, keys);
		tracking.index.set(unit->rank, DependencyIndex::Declarations, std::move(keys));
	}
	tracking.counts.declarations += r_units.size();
}

void Compilation::prepare() {
	tracking_ = std::make_unique<Tracking>();
	checked_ = true;
	std::vector<Unit *> all;
	std::vector<Unit *> program;
	for (Unit &unit : units_) {
		unit.broken = unit.diagnostics.has_errors();
		broken_files_ += unit.broken;
		if (unit.broken) {
			unit.diagnostics.mute();
		}
		unit.unchecked = true;
		tracking_->unchecked.insert(unit.rank);
		all.push_back(&unit);
		if (!unit.is_declaration) {
			program.push_back(&unit);
		}
	}
	declare_files(all);
	// Declaration files never change in place: checked once, now, so that what waits is the program's.
	for (Unit *unit : all) {
		if (unit->is_declaration) {
			check_bodies(*unit);
		}
	}
	std::vector<Dependency> ignored;
	tracking_->prints.begin_round(program);
	tracking_->prints.finish_round(ignored);
	for (Unit *unit : program) {
		std::vector<Dependency> keys;
		declaration_dependencies(*unit, keys);
		tracking_->index.set(unit->rank, DependencyIndex::Declarations, std::move(keys));
	}
}

bool Compilation::update(const std::vector<std::pair<Unit *, std::shared_ptr<Parsed>>> &p_changes) {
	if (!tracking_) {
		return false;
	}
	Tracking &tracking = *tracking_;
	std::vector<Dependency> changed;
	std::vector<Unit *> work;
	for (const auto &[unit, parsed] : p_changes) {
		if (unit->is_declaration) {
			return false;
		}
		tracking.replacing[unit] = parsed;
		changed.push_back(on_positions(unit));
		changed.push_back(on_halves(unit));
		work.push_back(unit);
	}
	return propagate(std::move(work), std::move(changed), {});
}

// Redoes the declarations of the files in p_work and of those that read a changed key in theirs, round by
// round, until nothing more changes. A file whose bodies alone read a changed key waits: its bodies are
// checked again, and its declarations redone with them. A file in p_done was redone in the round that made
// the change, and read the changed declarations already.
bool Compilation::propagate(std::vector<Unit *> p_work, std::vector<Dependency> p_changed, std::vector<Unit *> p_done) {
	Tracking &tracking = *tracking_;
	const auto done = [&](const Unit *p_unit) { return std::find(p_done.begin(), p_done.end(), p_unit) != p_done.end(); };
	// A cascade this long means a mistake in the dependencies, not a program: check again from the start.
	constexpr int MAX_ROUNDS = 64;
	for (int round = 0;; ++round) {
		std::vector<uint32_t> declarations;
		std::vector<uint32_t> bodies;
		for (const Dependency &key : p_changed) {
			tracking.index.readers(key, declarations, bodies);
		}
		for (uint32_t rank : declarations) {
			if (!done(&units_[rank])) {
				p_work.push_back(&units_[rank]);
			}
		}
		for (uint32_t rank : bodies) {
			Unit &reader = units_[rank];
			if (!done(&reader) && std::find(p_work.begin(), p_work.end(), &reader) == p_work.end()) {
				reader.bodies_only = true;
				if (!reader.unchecked) {
					reader.unchecked = true;
					tracking.unchecked.insert(reader.rank);
				}
			}
		}
		if (p_work.empty()) {
			break;
		}
		if (round == MAX_ROUNDS) {
			std::fprintf(stderr, "kas3c: an edit kept changing declarations; checking everything again\n");
			return false;
		}
		tracking.counts.cascades += round == 1 ? 1 : 0;
		p_changed.clear();
		redeclare(p_work, p_changed);
		p_done = std::move(p_work);
		p_work.clear();
	}
	tracking.retired.clear();
	return true;
}

Unit *Compilation::next_unchecked() {
	return !tracking_ || tracking_->unchecked.empty() ? nullptr : &units_[*tracking_->unchecked.begin()];
}

void Compilation::check_bodies(Unit &p_unit) {
	if (!p_unit.unchecked) {
		return;
	}
	if (p_unit.bodies_only) {
		// Back to where its signatures left it.
		p_unit.bodies_only = false;
		p_unit.diagnostics = p_unit.declared_diagnostics;
		p_unit.references.resize(p_unit.declared_references);
		p_unit.cited.resize(p_unit.declared_cited);
		p_unit.failed_import = p_unit.declared_failed_import; // A local function's import may fail or not now.
		p_unit.typed.clear();
		p_unit.local_scopes.clear();
		p_unit.body_symbols.clear();
		p_unit.undoubted.reset();
		// The imports a declared function's body writes belong to its signature: they keep what they found.
		std::vector<Symbol *> imported;
		for (const Import *import : p_unit.signature_imports) {
			imported.push_back(import->symbol);
		}
		clear_body_annotations(p_unit.tree);
		for (size_t i = 0; i < imported.size(); ++i) {
			p_unit.signature_imports[i]->symbol = imported[i];
		}
		tracking_->counts.bodies_only++;
	}
	check_unit_bodies(*this, p_unit);
	p_unit.unchecked = false;
	p_unit.doubted_at = 0;
	tracking_->unchecked.erase(p_unit.rank);
	if (!p_unit.is_declaration) {
		std::vector<Dependency> keys;
		body_dependencies(p_unit, keys);
		tracking_->index.set(p_unit.rank, DependencyIndex::Bodies, std::move(keys));
		tracking_->counts.bodies++;
	}
}

Compilation::Counts Compilation::counts() const {
	return tracking_ ? tracking_->counts : Counts();
}

} // namespace kas3
