// What `kas3c check --dump-names` prints.
#include "builtins.h"
#include "names.h"

#include <algorithm>

namespace kas3 {

std::string Compilation::dump_names() const {
	std::string result;
	int shown = 0;
	for (const Unit *file : files_) {
		const Unit &unit = *file;
		shown += unit.is_declaration ? 0 : 1;
	}
	for (const Unit *file : files_) {
		const Unit &unit = *file;
		if (unit.is_declaration) {
			continue;
		}
		if (shown > 1) {
			result += "# " + unit.source.path() + "\n";
		}
		std::vector<Reference> references = unit.references;
		std::stable_sort(references.begin(), references.end(),
				[](const Reference &p_a, const Reference &p_b) { return p_a.offset < p_b.offset; });
		for (const Reference &reference : references) {
			Position at = unit.source.position(reference.offset);
			const Symbol *symbol = reference.symbol;
			result += std::to_string(at.line) + ":" + std::to_string(at.column) + " ";
			result += reference.name;
			result += " -> ";
			result += symbol_kind_name(symbol->kind);
			if (symbol->kind != SymbolKind::Arguments) {
				result += " ";
				std::string name = qualified_name(symbol);
				// "X in this file" reads oddly here; the position says where it is.
				result += symbol->file_private ? std::string(symbol->name) : name;
			}
			if (!symbol->unit || symbol->unit->is_builtin) {
				result += " (built in)";
			} else if (symbol->unit->source.path() == GODOT_DECLARATIONS_PATH) {
				result += " (Godot)"; // Its line numbers change with every Godot version.
			} else {
				Position declared = symbol->unit->source.position(symbol->offset);
				result += " @";
				if (symbol->unit != &unit) {
					result += symbol->unit->source.path() + ":";
				}
				result += std::to_string(declared.line) + ":" + std::to_string(declared.column);
			}
			result += "\n";
		}
	}
	return result;
}

} // namespace kas3
