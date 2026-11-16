// Checking a program as it is edited. The declaration layer (declare, link, signatures) of the files an edit
// touches is redone at once; the files whose fingerprinted reads changed are redone after them, round by
// round, until nothing more changes. Bodies wait until they are asked for: each file's read only what other
// files declare, so they are checked one file at a time, in any order.
#include "incremental.h"

#include "ast_walk.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

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
		// Marked as it is walked, so that a long chain is walked once for each class, not once for each pair.
		const uint32_t mark = ++cycle_mark_;
		for (Symbol *at = start; at && at->klass; at = at->klass->base) {
			if (at->klass->mark == mark) {
				// Come round to a class of this walk: every class from it back to it is the cycle.
				Symbol *member = at;
				do {
					if (outside(member->unit)) {
						more.push_back(member->unit);
					}
					member = member->klass->base;
				} while (member != at);
				break;
			}
			at->klass->mark = mark;
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
		for (Unit *unit : files_) {
			for (Symbol *symbol : unit->classes) {
				if (symbol->kind == SymbolKind::Interface && outside(unit)) {
					more.push_back(unit);
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
	std::vector<Unit *> gone;
	for (;;) {
		in_order(r_units);
		for (Unit *unit : r_units) {
			if (std::none_of(broken_before.begin(), broken_before.end(), [&](const auto &p_entry) { return p_entry.first == unit; })) {
				broken_before.emplace_back(unit, unit->broken);
				package_names(*unit, names_before);
			}
			retract(*unit);
		}
		// A file that left has nothing to declare: what it declared is gone with the retraction.
		r_units.erase(std::remove_if(r_units.begin(), r_units.end(), [&](Unit *p_unit) {
			if (p_unit->removed) {
				leave(*p_unit);
				gone.push_back(p_unit);
			}
			return p_unit->removed;
		}), r_units.end());
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
	std::vector<Unit *> printed = r_units;
	printed.insert(printed.end(), gone.begin(), gone.end()); // What a file that left declared is compared too: gone.
	tracking.prints.begin_round(printed);
	tracking.prints.finish_round(r_changed);
	settle_packages(r_changed);
	if (broken_files_ && !r_changed.empty()) {
		// Doubt follows a symbol through its types to their bases, further than any file records: while a file
		// does not parse, every file is judged again when a declaration changed.
		++doubt_state_;
	}
	for (Unit *unit : r_units) {
		moved(*unit, r_changed);
	}
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
		tracking.index.set(unit->id, DependencyIndex::Declarations, std::move(keys));
	}
	for (Unit *unit : gone) {
		tracking.index.set(unit->id, DependencyIndex::Declarations, {});
		tracking.index.set(unit->id, DependencyIndex::Bodies, {});
		tracking.left.push_back(unit);
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
		tracking_->unchecked.insert(unit.id);
		all.push_back(&unit);
		if (!unit.is_declaration) {
			program.push_back(&unit);
		}
	}
	declare_files(all);
	tracking_->packages.clear(); // Every package is new in a first check: none appeared or went.
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
		moved(*unit, ignored);
	}
	for (Unit *unit : program) {
		std::vector<Dependency> keys;
		declaration_dependencies(*unit, keys);
		tracking_->index.set(unit->id, DependencyIndex::Declarations, std::move(keys));
	}
}

// A package is known while a file's block names it, as in a first check, where one that no file names does
// not exist. One that appears or goes changes what a dotted path or an import of it all finds.
void Compilation::settle_packages(std::vector<Dependency> &r_changed) {
	for (const auto &[name, known] : tracking_->packages) {
		auto found = packages_.find(name);
		const bool now = found != packages_.end() && found->second.files > 0;
		if (found != packages_.end() && !now && &found->second != top_level_) {
			packages_.erase(found);
		}
		if (known == now) {
			continue;
		}
		r_changed.push_back(on_package(name));
		for (size_t begin = 0; begin <= name.size();) {
			const size_t dot = std::min(name.find('.', begin), name.size());
			r_changed.push_back(on_name(std::string_view(name).substr(begin, dot - begin)));
			begin = dot + 1;
		}
	}
	tracking_->packages.clear();
}

// What is left of a file that left the program: its place, for a file that joins later.
void Compilation::leave(Unit &p_unit) {
	if (p_unit.broken) {
		--broken_files_;
		++doubt_state_;
	}
	tracking_->unchecked.erase(p_unit.id);
	files_.erase(std::remove(files_.begin(), files_.end(), &p_unit), files_.end());
	renumber();
	const uint32_t id = p_unit.id;
	p_unit = Unit(std::make_shared<Parsed>(std::string(), std::string()));
	p_unit.id = id;
	p_unit.removed = true;
}

void Compilation::renumber() {
	for (size_t i = 0; i < files_.size(); ++i) {
		files_[i]->rank = (uint32_t)i;
	}
}

Unit *Compilation::insert(std::shared_ptr<Parsed> p_parsed, const Unit *p_before) {
	Unit *unit = add(std::move(p_parsed));
	auto before = std::find(files_.begin(), files_.end(), p_before);
	if (before != files_.end()) {
		files_.pop_back();
		files_.insert(before, unit);
		renumber();
	}
	tracking_->joining.push_back(unit);
	return unit;
}

void Compilation::remove(Unit &p_unit) {
	tracking_->leaving.push_back(&p_unit);
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
		// A half of a property points into the old tree, which goes at the end of the update.
		changed.push_back(on_halves(unit));
		work.push_back(unit);
	}
	for (Unit *unit : tracking.leaving) {
		// Its tree goes too, and its declarations are nowhere now for a message to say where.
		tracking.retired.push_back(unit->parsed);
		changed.push_back(on_halves(unit));
		changed.push_back(on_positions(unit));
		unit->removed = true;
		work.push_back(unit);
	}
	work.insert(work.end(), tracking.joining.begin(), tracking.joining.end());
	tracking.leaving.clear();
	tracking.joining.clear();
	return propagate(std::move(work), std::move(changed), {});
}

// Redoes the declarations of the files in p_work and of those that read a changed key in theirs, round by
// round, until nothing more changes. A file whose bodies alone read a changed key waits: its bodies are
// checked again, and its declarations redone with them. A file in p_done was redone in the round that made
// the change, and read the changed declarations already.
bool Compilation::propagate(std::vector<Unit *> p_work, std::vector<Dependency> p_changed, std::vector<Unit *> p_done) {
	Tracking &tracking = *tracking_;
	const auto done = [&](const Unit *p_unit) { return std::find(p_done.begin(), p_done.end(), p_unit) != p_done.end(); };
	// A file redone this many times over in one edit means a mistake in the dependencies, not a program: check
	// again from the start.
	constexpr int MAX_REDOS = 8;
	std::unordered_map<const Unit *, int> redone;
	for (int round = 0;; ++round) {
		std::vector<uint32_t> declarations;
		std::vector<uint32_t> bodies;
		// A class whose shape changed changes the chain and the shape of every class below it, whatever they
		// declare: they are taken in this round, with what reads them, not a class a round.
		std::unordered_set<const Unit *> below;
		for (size_t i = 0; i < p_changed.size(); ++i) {
			if (p_changed[i].kind != DependencyKind::Shape) {
				continue;
			}
			std::vector<uint32_t> subtypes;
			std::vector<uint32_t> unused;
			tracking.index.readers(p_changed[i], subtypes, unused);
			for (uint32_t id : subtypes) {
				if (!done(&units_[id]) && below.insert(&units_[id]).second) {
					for (const Symbol *klass : units_[id].classes) {
						p_changed.push_back(on_shape(klass));
						p_changed.push_back(on_chain(klass));
					}
				}
			}
		}
		for (const Dependency &key : p_changed) {
			tracking.index.readers(key, declarations, bodies);
		}
		for (uint32_t id : declarations) {
			if (!done(&units_[id])) {
				p_work.push_back(&units_[id]);
			}
		}
		for (uint32_t id : bodies) {
			Unit &reader = units_[id];
			if (!done(&reader) && std::find(p_work.begin(), p_work.end(), &reader) == p_work.end()) {
				reader.bodies_only = true;
				if (!reader.unchecked) {
					reader.unchecked = true;
					tracking.unchecked.insert(reader.id);
				}
			}
		}
		if (p_work.empty()) {
			break;
		}
		in_order(p_work); // Each file once, however many changed keys it read.
		if (std::any_of(p_work.begin(), p_work.end(), [&](const Unit *p_unit) { return ++redone[p_unit] > MAX_REDOS; })) {
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
	tracking_->unchecked.erase(p_unit.id);
	if (!p_unit.is_declaration) {
		std::vector<Dependency> keys;
		body_dependencies(p_unit, keys);
		tracking_->index.set(p_unit.id, DependencyIndex::Bodies, std::move(keys));
		tracking_->counts.bodies++;
	}
}

// A file whose declarations moved to other lines or columns changes the messages that quote where they are.
void Compilation::moved(Unit &p_unit, std::vector<Dependency> &r_changed) {
	uint64_t positions = 0x9E3779B97F4A7C15ull;
	for (const Symbol *symbol : p_unit.declared) {
		const Position at = p_unit.source.position(symbol->offset);
		positions = (positions ^ (((uint64_t)at.line << 32) | (uint32_t)at.column)) * 0xFF51AFD7ED558CCDull;
	}
	if (positions != p_unit.positions) {
		p_unit.positions = positions;
		r_changed.push_back(on_positions(&p_unit));
	}
}

Compilation::Counts Compilation::counts() const {
	Counts counts = tracking_ ? tracking_->counts : Counts();
	counts.packages = packages_.size();
	return counts;
}

} // namespace kas3
