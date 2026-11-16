// What a syntax error makes doubtful. A file that did not parse may have lost a declaration, or read one
// wrongly (a modifier, a parameter, `extends`): an error that rests on what that file declares may be
// false. Those are dropped, and the rest stay.
#include "incremental.h"
#include "names.h"

#include "ast_walk.h"

#include <algorithm>

namespace kas3 {

namespace {

class Doubt {
	std::unordered_map<const Symbol *, bool> known_;

	bool touches(const TypeRef *p_type) {
		for (; p_type; p_type = p_type->argument) {
			if (touches(p_type->symbol)) {
				return true;
			}
		}
		return false;
	}

	bool rests_on_broken(const Symbol *p_symbol) {
		if (p_symbol->unit && p_symbol->unit->broken) {
			return true;
		}
		if (touches(p_symbol->owner) || touches(p_symbol->type)) {
			return true;
		}
		if (p_symbol->klass) {
			if (touches(p_symbol->klass->base)) {
				return true;
			}
			for (const Symbol *interface : p_symbol->klass->interfaces) {
				if (touches(interface)) {
					return true;
				}
			}
		}
		if (p_symbol->node && p_symbol->node->kind == NodeKind::FunctionDecl) {
			for (const Parameter &parameter : static_cast<const FunctionDecl *>(p_symbol->node)->parameters) {
				if (touches(parameter.type)) {
					return true;
				}
			}
		}
		return false;
	}

public:
	// True if the symbol is declared in a file with a syntax error, or what it is rests on one: its
	// class, a base class or an interface of it, its type, or the type of one of its parameters.
	bool touches(const Symbol *p_symbol) {
		if (!p_symbol) {
			return false;
		}
		auto [entry, added] = known_.try_emplace(p_symbol, false); // False while it is being worked out.
		if (added) {
			bool found = rests_on_broken(p_symbol);
			known_[p_symbol] = found;
			return found;
		}
		return entry->second;
	}

	bool touches(const Type *p_type) {
		for (; p_type; p_type = p_type->element) {
			if (touches(p_type->symbol)) {
				return true;
			}
		}
		return false;
	}

	// True if anything the file names, or the type of any value in it, rests on a broken file.
	bool affected(Unit &p_unit) {
		bool found = false;
		walk(p_unit.tree, [&](Node *p_node) {
			if (found) {
				return;
			}
			switch (p_node->kind) {
				case NodeKind::TypeRef:
					found = touches(static_cast<TypeRef *>(p_node)->symbol);
					break;
				case NodeKind::Import:
					found = touches(static_cast<Import *>(p_node)->symbol);
					break;
				case NodeKind::ClassDecl:
					found = touches(static_cast<ClassDecl *>(p_node)->symbol);
					break;
				case NodeKind::InterfaceDecl:
					found = touches(static_cast<InterfaceDecl *>(p_node)->symbol);
					break;
				case NodeKind::Name:
					found = touches(static_cast<Name *>(p_node)->symbol);
					break;
				case NodeKind::Member:
					found = touches(static_cast<Member *>(p_node)->resolved());
					break;
				default:
					break;
			}
			if (!found && is_expression(p_node->kind)) {
				found = touches(static_cast<Expr *>(p_node)->type);
			}
		});
		return found;
	}
};

} // namespace

// A view over what the passes said: a file judged before keeps what it was told while nothing it rests on
// changed, and is judged again from what the passes said when something did.
void Compilation::settle_doubt() {
	const bool any_broken = broken_files_ > 0 ||
			std::any_of(units_.begin(), units_.end(), [](const Unit &p_unit) { return p_unit.broken; });
	Doubt doubt;
	for (Unit &unit : units_) {
		if (unit.unchecked || unit.doubted_at == doubt_state_) {
			continue;
		}
		unit.doubted_at = doubt_state_;
		if (unit.undoubted) {
			unit.diagnostics = std::move(*unit.undoubted);
			unit.undoubted.reset();
		}
		if (!any_broken || unit.broken || !(unit.diagnostics.has_errors() || unit.diagnostics.has_warnings())) {
			continue; // A broken file's own were never taken; a file with none has none to drop.
		}
		unit.undoubted = unit.diagnostics;
		if (tracking_) {
			tracking_->counts.judged++;
		}
		// An import that found nothing may have changed what the file's other names mean.
		if (unit.failed_import || doubt.affected(unit)) {
			unit.diagnostics.clear();
		} else {
			unit.diagnostics.drop_unresolved();
		}
	}
}

} // namespace kas3
